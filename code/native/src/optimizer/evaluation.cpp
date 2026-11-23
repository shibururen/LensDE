/*
 * Own DifferentialEvolution input setup and dispatch population evaluation to CPU, CUDA or a broker.
 * All optimizer sessions use this layer with their variable order, material context and scaled-row batches.
 * Preserve preparation, row mapping and CPU-fallback semantics when switching evaluation backends.
 */
#include "support.hpp"

namespace optiland {
using namespace optimizer_detail;

DifferentialEvolution::DifferentialEvolution(
    LensSystem base,
    std::vector<VariableSpec> variables,
    MeritConfig merit_config) : base_(std::move(base)),
      variables_(std::move(variables)),
      merit_config_(std::move(merit_config)) {
    configure_variable_thickness_constraints(&merit_config_, variables_);
}

void DifferentialEvolution::set_glass_search_context(GlassSearchContext context) {
    glass_search_ = std::move(context);
}

void DifferentialEvolution::set_population_evaluation_broker(
    std::shared_ptr<PopulationEvaluationBroker> broker,
    int island_index) {
    population_evaluation_broker_ = std::move(broker);
    population_evaluation_island_index_ = island_index;
    population_evaluation_logical_call_index_ = 0;
}

std::vector<double> DifferentialEvolution::evaluate_population_cpu(
    const std::vector<double>& flat_population,
    int population_size) const {
    const int dim = static_cast<int>(variables_.size());
    std::vector<double> merits(population_size, merit_config_.fail_merit);
    const int workers = std::min(population_size, available_worker_count());
    if (workers <= 1 || population_size < 2) {
        for (int i = 0; i < population_size; ++i) {
            evaluate_population_candidate(
                base_,
                variables_,
                glass_search_,
                merit_config_,
                flat_population,
                dim,
                i,
                merits);
        }
        return merits;
    }

#ifdef _WIN32
    CpuPopulationEvalThreadContext context;
    context.base = &base_;
    context.variables = &variables_;
    context.glass_search = &glass_search_;
    context.merit_config = &merit_config_;
    context.flat_population = &flat_population;
    context.merits = &merits;
    context.dim = dim;
    context.population_size = population_size;
    InitializeCriticalSection(&context.error_lock);
    std::vector<HANDLE> handles;
    handles.reserve(static_cast<std::size_t>(workers));
    for (int w = 0; w < workers; ++w) {
        HANDLE handle = CreateThread(
            nullptr,
            0,
            run_cpu_population_eval_thread,
            &context,
            0,
            nullptr);
        if (handle) {
            handles.push_back(handle);
        }
    }
    if (handles.empty()) {
        DeleteCriticalSection(&context.error_lock);
        for (int i = 0; i < population_size; ++i) {
            evaluate_population_candidate(
                base_,
                variables_,
                glass_search_,
                merit_config_,
                flat_population,
                dim,
                i,
                merits);
        }
        return merits;
    }
    WaitForMultipleObjects(
        static_cast<DWORD>(handles.size()),
        handles.data(),
        TRUE,
        INFINITE);
    for (HANDLE handle : handles) {
        CloseHandle(handle);
    }
    std::exception_ptr first_error = context.first_error;
    DeleteCriticalSection(&context.error_lock);
    if (first_error) {
        std::rethrow_exception(first_error);
    }
#else
    for (int i = 0; i < population_size; ++i) {
        evaluate_population_candidate(
            base_,
            variables_,
            glass_search_,
            merit_config_,
            flat_population,
            dim,
            i,
            merits);
    }
#endif
    return merits;
}

std::vector<double> DifferentialEvolution::evaluate_population(
    const std::vector<double>& flat_population,
    int population_size,
    bool prefer_cuda,
    bool* used_cuda,
    DeTimingStats* timing) const {
    if (used_cuda) {
        *used_cuda = false;
    }
    const auto evaluation_start = Clock::now();
    if (timing) {
        ++timing->evaluation_calls;
        timing->evaluated_candidates += population_size;
    }

    if (prefer_cuda && distortion_model_allows_cuda(merit_config_) &&
        (population_evaluation_broker_ || evaluate_population_cuda_available())) {
        std::vector<double> merits;
        bool evaluation_ok = false;
        std::string broker_error;
        if (population_evaluation_broker_) {
            PopulationEvaluationResult broker_result =
                population_evaluation_broker_->evaluate(
                    population_evaluation_island_index_,
                    population_evaluation_logical_call_index_++,
                    flat_population,
                    population_size,
                    false);
            evaluation_ok = broker_result.success;
            broker_error = std::move(broker_result.error);
            merits = std::move(broker_result.merits);
        } else {
            evaluation_ok = evaluate_population_cuda(
                base_,
                variables_,
                merit_config_,
                flat_population,
                population_size,
                &merits,
                timing ? &timing->cuda : nullptr,
                &glass_search_);
        }
        if (evaluation_ok) {
            if (used_cuda) {
                *used_cuda = true;
            }
            if (timing) {
                timing->evaluation_seconds += seconds_since(evaluation_start);
            }
            return merits;
        }
        if (de_cuda_required()) {
            throw std::runtime_error(
                "DE-CUDA required, but CUDA merit evaluation rejected this batch" +
                (broker_error.empty() ? std::string{} : ": " + broker_error));
        }
    } else if (prefer_cuda && de_cuda_required()) {
        throw std::runtime_error(
            "DE-CUDA required, but the merit model or CUDA runtime is unavailable");
    }
    std::vector<double> merits = evaluate_population_cpu(flat_population, population_size);
    if (timing) {
        timing->evaluation_seconds += seconds_since(evaluation_start);
    }
    return merits;
}

std::vector<Score> DifferentialEvolution::evaluate_population_scores_scaled(
    const std::vector<double>& scaled_population,
    int population_size,
    InternalScalingMode internal_mode,
    bool prefer_cuda,
    bool* used_cuda,
    DeTimingStats* timing) const {
    if (used_cuda) {
        *used_cuda = false;
    }
    std::vector<Score> scores(static_cast<std::size_t>(std::max(0, population_size)));
    if (population_size <= 0) {
        return scores;
    }
    const auto evaluation_start = Clock::now();
    if (timing) {
        ++timing->evaluation_calls;
        timing->evaluated_candidates += population_size;
    }

    if (prefer_cuda && distortion_model_allows_cuda(merit_config_) &&
        (population_evaluation_broker_ || evaluate_population_cuda_available())) {
        const auto conversion_start = Clock::now();
        const std::vector<double> physical_population =
            scaled_population_to_physical_flat(
                variables_,
                scaled_population,
                population_size,
                internal_mode);
        if (timing) {
            timing->physical_conversion_seconds += seconds_since(conversion_start);
        }
        std::vector<double> merits;
        std::vector<CudaScoreSummary> summaries;
        bool evaluation_ok = false;
        std::string broker_error;
        if (population_evaluation_broker_) {
            PopulationEvaluationResult broker_result =
                population_evaluation_broker_->evaluate(
                    population_evaluation_island_index_,
                    population_evaluation_logical_call_index_++,
                    physical_population,
                    population_size,
                    true);
            evaluation_ok = broker_result.success;
            broker_error = std::move(broker_result.error);
            merits = std::move(broker_result.merits);
            summaries = std::move(broker_result.summaries);
        } else {
            evaluation_ok = evaluate_population_cuda_scores(
                base_,
                variables_,
                merit_config_,
                physical_population,
                population_size,
                &merits,
                &summaries,
                timing ? &timing->cuda : nullptr,
                &glass_search_);
        }
        if (evaluation_ok &&
            static_cast<int>(summaries.size()) == population_size) {
            if (used_cuda) {
                *used_cuda = true;
            }
            for (int i = 0; i < population_size; ++i) {
                Score score = score_from_cuda_summary(
                    summaries[static_cast<std::size_t>(i)],
                    merit_config_);
                if (i < static_cast<int>(merits.size())) {
                    score.merit = finite_or(
                        merits[static_cast<std::size_t>(i)],
                        score.merit);
                }
                scores[static_cast<std::size_t>(i)] = std::move(score);
            }
            if (timing) {
                timing->evaluation_seconds += seconds_since(evaluation_start);
            }
            return scores;
        }
        if (de_cuda_required()) {
            throw std::runtime_error(
                "DE-CUDA required, but CUDA score evaluation rejected this batch" +
                (broker_error.empty() ? std::string{} : ": " + broker_error));
        }
    } else if (prefer_cuda && de_cuda_required()) {
        throw std::runtime_error(
            "DE-CUDA required, but the merit model or CUDA runtime is unavailable");
    }

    const int dim = static_cast<int>(variables_.size());
    auto evaluate_score_row = [&](int i) {
        std::vector<double> values;
        values.reserve(static_cast<std::size_t>(dim));
        const double* row =
            scaled_population.data() + static_cast<std::size_t>(i) * dim;
        physical_values_from_scaled_row(
            variables_,
            row,
            static_cast<std::size_t>(dim),
            internal_mode,
            &values);
        LensSystem lens = prepare_for_merit(
            apply_mixed_variables_row(
                base_,
                variables_,
                values.data(),
                static_cast<std::size_t>(dim),
                glass_search_),
            merit_config_);
        scores[static_cast<std::size_t>(i)] = evaluate_merit(lens, merit_config_);
    };
    const int workers = std::min(population_size, available_worker_count());
    if (workers <= 1 || population_size < 2) {
        for (int i = 0; i < population_size; ++i) {
            evaluate_score_row(i);
        }
    } else {
#ifdef _WIN32
        volatile LONG next_index = 0;
        std::vector<FunctionThreadContext> contexts(static_cast<std::size_t>(workers));
        std::vector<HANDLE> handles;
        handles.reserve(static_cast<std::size_t>(workers));
        for (int worker = 0; worker < workers; ++worker) {
            contexts[static_cast<std::size_t>(worker)].fn = [&]() {
                while (true) {
                    const int i = static_cast<int>(InterlockedIncrement(&next_index)) - 1;
                    if (i >= population_size) {
                        break;
                    }
                    evaluate_score_row(i);
                }
            };
            HANDLE handle = CreateThread(
                nullptr,
                0,
                run_function_thread,
                &contexts[static_cast<std::size_t>(worker)],
                0,
                nullptr);
            if (handle) {
                handles.push_back(handle);
            } else {
                contexts[static_cast<std::size_t>(worker)].fn();
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
        for (const auto& context : contexts) {
            if (context.error) {
                std::rethrow_exception(context.error);
            }
        }
#else
        for (int i = 0; i < population_size; ++i) {
            evaluate_score_row(i);
        }
#endif
    }
    if (timing) {
        timing->evaluation_seconds += seconds_since(evaluation_start);
    }
    return scores;
}

}  // namespace optiland
