/*
 * Sequence HybridSession epochs and assemble the final physical population and optimizer result.
 * The public hybrid entry delegates DE, polish, incumbent reinjection and refresh through this controller.
 * Preserve phase/progress order and retain RNG state across all outer epochs.
 */
#include "hybrid_session.hpp"

namespace optiland { namespace optimizer_detail {

void HybridSession::run_epoch(int outer) {
const auto epoch_start_time = Clock::now();
const double epoch_start_merit = best_score.merit;
emit_hybrid_trace(
            "epoch_start",
            outer,
            0.0,
            epoch_start_merit,
            epoch_start_merit,
            best_score,
            best_scaled,
            0,
            0,
            0,
            0,
            used_cuda);
if (options.inject_best && best_scaled.size() == static_cast<std::size_t>(dim)) {
            set_population_row(population, 0, best_scaled, variables_, internal_mode);
            merits[0] = best_score.merit;
        }
    run_de(outer, epoch_start_merit);
    run_polish(outer, epoch_start_merit);
if (options.inject_best && best_scaled.size() == static_cast<std::size_t>(dim)) {
            set_population_row(population, 0, best_scaled, variables_, internal_mode);
            merits[0] = best_score.merit;
        }
    run_refresh(outer, epoch_start_merit);
emit_hybrid_trace(
            "epoch_end",
            outer,
            seconds_since(epoch_start_time),
            epoch_start_merit,
            epoch_start_merit,
            best_score,
            best_scaled,
            inner_generations,
            static_cast<int>(parent_indices.size()),
            epoch_polish_improvements,
            refreshed_count,
            used_cuda,
            epoch_polish_merit_strict_improvements,
            epoch_polish_merit_neutral,
            epoch_polish_merit_worse);
if (progress) {
            progress(outer, best_score, scaled_to_physical(variables_, best_scaled, internal_mode));
        }
}

OptimizationResult HybridSession::run() {
    initialize();
    for (int outer = 1; outer <= outer_epochs; ++outer) run_epoch(outer);
OptimizationResult result;
    result.best_values = scaled_to_physical(variables_, best_scaled, internal_mode);
    result.best_score = best_score;
    result.generations = total_de_generations;
    result.used_cuda = used_cuda;
    result.polish_seconds = polish_seconds;
    result.de_seconds = de_seconds;
    result.polish_runs = polish_runs;
    result.polish_improvements = polish_improvements;
    phase_start = Clock::now();
    attach_final_population(result, variables_, population, pop_size, merits, internal_mode);
    timing.attach_population_seconds += seconds_since(phase_start);
    result.timing = timing;
    return result;
}

} }  // namespace optiland::optimizer_detail
