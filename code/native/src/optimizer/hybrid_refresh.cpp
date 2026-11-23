/*
 * Refresh the HybridSession population after DE and local polish in an outer epoch.
 * The session retains incumbent protection, variable transforms and the same seeded random stream.
 * Preserve refresh-slot order, generated-row draws and scoring before committing replacements.
 */
#include "hybrid_session.hpp"

namespace optiland { namespace optimizer_detail {

void HybridSession::run_refresh(int outer, double epoch_start_merit) {
refreshed_count = 0;
        if (refresh_count > 0 && outer < outer_epochs) {
            const auto refresh_start = Clock::now();
            const double before_refresh_merit = best_score.merit;
            bool refresh_used_cuda = false;
            const auto order = sorted_population_indices(merits);
            std::vector<int> refresh_indices;
            refresh_indices.reserve(static_cast<std::size_t>(refresh_count));
            for (auto it = order.rbegin(); it != order.rend() &&
                                       static_cast<int>(refresh_indices.size()) < refresh_count;
                 ++it) {
                const int idx = *it;
                if (idx == 0) {
                    continue;
                }
                if (std::find(parent_indices.begin(), parent_indices.end(), idx) != parent_indices.end()) {
                    continue;
                }
                refresh_indices.push_back(idx);
            }
            std::vector<int> refresh_parent_indices;
            refresh_parent_indices.reserve(static_cast<std::size_t>(
                std::max(1, options.refresh_generation.parent_count)));
            for (int idx : order) {
                if (std::find(refresh_indices.begin(), refresh_indices.end(), idx) !=
                    refresh_indices.end()) {
                    continue;
                }
                refresh_parent_indices.push_back(idx);
                if (static_cast<int>(refresh_parent_indices.size()) >=
                    std::max(1, options.refresh_generation.parent_count)) {
                    break;
                }
            }
            if (refresh_parent_indices.empty()) {
                refresh_parent_indices.push_back(0);
            }
            if (!refresh_indices.empty() && options.refresh_generation.trace_feasible_gate) {
                const int target = static_cast<int>(refresh_indices.size());
                const int max_attempts =
                    population_generation_candidate_limit(options.refresh_generation, target);
                int attempts = 0;
                std::vector<double> accepted_rows;
                std::vector<Score> accepted_scores;
                accepted_rows.reserve(static_cast<std::size_t>(target) * dim);
                accepted_scores.reserve(static_cast<std::size_t>(target));
                while (static_cast<int>(accepted_scores.size()) < target &&
                       attempts < max_attempts &&
                       !population_generation_time_exhausted(refresh_start, options.refresh_generation)) {
                    const int remaining_attempts = max_attempts - attempts;
                    const int remaining_target =
                        target - static_cast<int>(accepted_scores.size());
                    const int batch_floor = std::min(64, std::max(8, target));
                    const int batch = std::min(
                        remaining_attempts,
                        std::max(remaining_target, batch_floor));
                    std::vector<double> candidate_rows =
                        make_population_generation_rows(
                            base_,
                            variables_,
                            population,
                            dim,
                            refresh_parent_indices,
                            batch,
                            options.refresh_generation,
                            internal_mode,
                            rng,
                            uni);
                    attempts += batch;
                    for (int row = 0; row < batch &&
                                      static_cast<int>(accepted_scores.size()) < target;
                         ++row) {
                        std::vector<double> scaled = population_row(candidate_rows, row, dim);
                        const auto eval_start = Clock::now();
                        Score score = evaluate_score_for_scaled(scaled);
                        ++timing.evaluation_calls;
                        ++timing.evaluated_candidates;
                        timing.evaluation_seconds += seconds_since(eval_start);
                        if (!score_trace_feasible_for_generation(score, merit_config_)) {
                            continue;
                        }
                        accepted_rows.insert(accepted_rows.end(), scaled.begin(), scaled.end());
                        accepted_scores.push_back(std::move(score));
                    }
                }
                refreshed_count = static_cast<int>(accepted_scores.size());
                for (int row = 0; row < refreshed_count; ++row) {
                    const int idx = refresh_indices[static_cast<std::size_t>(row)];
                    const std::vector<double> scaled =
                        population_row(accepted_rows, row, dim);
                    set_population_row(population, idx, scaled, variables_, internal_mode);
                    merits[static_cast<std::size_t>(idx)] =
                        accepted_scores[static_cast<std::size_t>(row)].merit;
                }
                if (refreshed_count > 0) {
                    auto [refreshed_best_scaled, refreshed_best_score] = update_best_from_population();
                    if (refreshed_best_score.merit < best_score.merit) {
                        best_scaled = std::move(refreshed_best_scaled);
                        best_score = std::move(refreshed_best_score);
                    }
                }
            } else if (!refresh_indices.empty()) {
                std::vector<double> refresh_rows =
                    make_population_generation_rows(
                        base_,
                        variables_,
                        population,
                        dim,
                        refresh_parent_indices,
                        static_cast<int>(refresh_indices.size()),
                        options.refresh_generation,
                        internal_mode,
                        rng,
                        uni);
                for (std::size_t row = 0; row < refresh_indices.size(); ++row) {
                    const int idx = refresh_indices[row];
                    set_population_row(
                        population,
                        idx,
                        population_row(refresh_rows, static_cast<int>(row), dim),
                        variables_,
                        internal_mode);
                }
                refreshed_count = static_cast<int>(refresh_indices.size());
                phase_start = Clock::now();
                physical_population =
                    scaled_population_to_physical_flat(
                        variables_,
                        population,
                        pop_size,
                        internal_mode);
                timing.physical_conversion_seconds += seconds_since(phase_start);
                phase_start = Clock::now();
                merits = evaluate_population(
                    physical_population,
                    pop_size,
                    de_options.use_cuda,
                    &eval_used_cuda,
                    &timing);
                used_cuda = used_cuda || eval_used_cuda;
                refresh_used_cuda = refresh_used_cuda || eval_used_cuda;
                auto [refreshed_best_scaled, refreshed_best_score] = update_best_from_population();
                if (refreshed_best_score.merit < best_score.merit) {
                    best_scaled = std::move(refreshed_best_scaled);
                    best_score = std::move(refreshed_best_score);
                }
            }
            const double epoch_refresh_seconds = seconds_since(refresh_start);
            de_seconds += epoch_refresh_seconds;
            emit_hybrid_trace(
                "refresh_end",
                outer,
                epoch_refresh_seconds,
                epoch_start_merit,
                before_refresh_merit,
                best_score,
                best_scaled,
                0,
                0,
                0,
                refreshed_count,
                refresh_used_cuda);
        }
}

} }  // namespace optiland::optimizer_detail
