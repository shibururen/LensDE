/*
 * Prepare and execute local-polish work for selected HybridSession parents.
 * The outer epoch shares ordered parent rows and session timing with batch-owned worker contexts.
 * Keep context lifetimes, worker-result correspondence and polish acceptance in selected-parent order.
 */
#include "hybrid_session.hpp"

namespace optiland { namespace optimizer_detail {

PolishThreadContext HybridSession::make_polish_context(int idx) {
            PolishThreadContext context;
            context.optimizer = &owner;
            context.options = &polish_options;
            context.variables = &variables_;
            context.index = idx;
            context.before_merit = merits[static_cast<std::size_t>(idx)];
            context.rollback = options.rollback;
            const std::vector<double> before_scaled = population_row(population, idx, dim);
            context.before_values = scaled_to_physical(variables_, before_scaled, internal_mode);
            return context;
        }

void HybridSession::run_polish(int outer, double epoch_start_merit) {
parent_indices = options.polish_enabled
                ? select_diverse_indices(
                      variables_,
                      population,
                      merits,
                      dim,
                      parent_count,
                      std::max(0.0, options.diversity),
                      options.distance_metric,
                      internal_mode)
                : std::vector<int>{};

        

        const double before_polish_merit = best_score.merit;
        const auto polish_start = Clock::now();
        std::vector<PolishWorkResult> polish_results;
        polish_results.reserve(parent_indices.size());
        if (options.batched_polish) {
            std::vector<MicroDlsInput> micro_inputs;
            micro_inputs.reserve(parent_indices.size());
            for (int idx : parent_indices) {
                micro_inputs.push_back({
                    idx,
                    population_row(population, idx, dim),
                    merits[static_cast<std::size_t>(idx)]});
            }
            auto evaluate_micro_scores = [&](const std::vector<double>& rows,
                                             int row_count,
                                             bool* micro_used_cuda) {
                return evaluate_population_scores_scaled(
                    rows,
                    row_count,
                    internal_mode,
                    polish_options.use_cuda,
                    micro_used_cuda,
                    &timing);
            };
            polish_results = run_batched_micro_dls_scaled(
                base_,
                variables_,
                merit_config_,
                micro_inputs,
                polish_options,
                internal_mode,
                options.rollback,
                evaluate_micro_scores);
        } else {
            const int hardware_workers = available_worker_count();
            const bool polish_batch_cuda =
                polish_options.use_cuda && polish_uses_batch_merit(polish_options);
            const int requested_polish_workers =
                options.polish_workers > 0
                    ? std::min(options.polish_workers, hardware_workers)
                    : hardware_workers;
            const int max_workers = (options.parallel_polish && !polish_batch_cuda)
                                        ? std::max(1, std::min<int>(
                                                          static_cast<int>(parent_indices.size()),
                                                          std::min(requested_polish_workers, 64)))
                                        : 1;
            if (max_workers <= 1) {
                for (int idx : parent_indices) {
                    PolishThreadContext context = make_polish_context(idx);
                    polish_results.push_back(run_polish_context(context));
                }
            } else {
#ifdef _WIN32
                for (std::size_t begin = 0; begin < parent_indices.size();) {
                    const std::size_t end = std::min(
                        parent_indices.size(),
                        begin + static_cast<std::size_t>(max_workers));
                    std::vector<PolishThreadContext> contexts;
                    contexts.reserve(end - begin);
                    for (std::size_t pos = begin; pos < end; ++pos) {
                        contexts.push_back(make_polish_context(parent_indices[pos]));
                    }
                    std::vector<HANDLE> handles;
                    handles.reserve(contexts.size());
                    for (auto& context : contexts) {
                        HANDLE handle = CreateThread(
                            nullptr,
                            0,
                            run_polish_thread,
                            &context,
                            0,
                            nullptr);
                        if (handle) {
                            handles.push_back(handle);
                        } else {
                            context.result = run_polish_context(context);
                        }
                    }
                    if (!handles.empty()) {
                        WaitForMultipleObjects(
                            static_cast<DWORD>(handles.size()),
                            handles.data(),
                            TRUE,
                            INFINITE);
                        for (HANDLE handle : handles) {
                            CloseHandle(handle);
                        }
                    }
                    for (auto& context : contexts) {
                        if (context.error) {
                            std::rethrow_exception(context.error);
                        }
                        polish_results.push_back(std::move(context.result));
                    }
                    begin = end;
                }
#else
                for (int idx : parent_indices) {
                    PolishThreadContext context = make_polish_context(idx);
                    polish_results.push_back(run_polish_context(context));
                }
#endif
            }
        }
        const double epoch_polish_seconds = seconds_since(polish_start);
        polish_seconds += epoch_polish_seconds;
        polish_runs += static_cast<int>(parent_indices.size());

        epoch_polish_improvements = 0;
        epoch_polish_merit_strict_improvements = 0;
        epoch_polish_merit_neutral = 0;
        epoch_polish_merit_worse = 0;
        epoch_polish_used_cuda = false;
        for (const auto& polished : polish_results) {
            epoch_polish_used_cuda = epoch_polish_used_cuda || polished.used_cuda;
            classify_polish_merit_delta(
                polished.before_merit,
                polished.score.merit,
                epoch_polish_merit_strict_improvements,
                epoch_polish_merit_neutral,
                epoch_polish_merit_worse);
            if (polished.improved) {
                set_population_row(
                    population,
                    polished.index,
                    polished.polished_scaled,
                    variables_,
                    internal_mode);
                merits[static_cast<std::size_t>(polished.index)] = polished.score.merit;
                ++polish_improvements;
                ++epoch_polish_improvements;
                if (polished.score.merit < best_score.merit) {
                    best_scaled = polished.polished_scaled;
                    best_score = polished.score;
                }
            }
        }
        emit_hybrid_trace(
            "polish_end",
            outer,
            epoch_polish_seconds,
            epoch_start_merit,
            before_polish_merit,
            best_score,
            best_scaled,
            0,
            static_cast<int>(parent_indices.size()),
            epoch_polish_improvements,
            0,
            epoch_polish_used_cuda,
            epoch_polish_merit_strict_improvements,
            epoch_polish_merit_neutral,
            epoch_polish_merit_worse);
        used_cuda = used_cuda || epoch_polish_used_cuda;
}

} }  // namespace optiland::optimizer_detail
