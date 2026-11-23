/*
 * Materialize the adaptive incumbent, final population, basin archive and timing into OptimizationResult.
 * AdaptiveSession::execute calls this after the epoch loop using the final shared run_ state.
 * Preserve final rescoring, coordinate conversion and archive/publication order before returning.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

OptimizationResult AdaptiveSession::finish_result() {


    this->update_best_from_population();
    if (run_.scalar_merit_fast_path) {
        run_.best_fitness = this->score_fitness_for_scaled(
            run_.best_scaled,
            run_.total_de_generations,
            run_.best_idx,
            "final",
            run_.best_fitness.polished);
    }
    this->sort_basins();
    if (options.basins.output_count > 0 &&
        (static_cast<int>(run_.basins.size()) > options.basins.output_count ||
         run_.topology_family_similarity_enabled)) {
        this->trim_basins_to_limit(
            options.basins.output_count,
            run_.topology_family_similarity_enabled);
    }
    for (BasinElite& basin : run_.basins) {
        basin.active_reject_reason = this->active_basin_reject_reason(basin);
        basin.active_eligible = basin.active_reject_reason.empty();
    }

    OptimizationResult result;
    result.best_values = scaled_to_physical(owner.variables_, run_.best_scaled, run_.internal_mode);
    result.best_score = run_.best_fitness.score;
    result.generations = run_.total_de_generations;
    result.used_cuda = run_.used_cuda;
    result.de_seconds = run_.de_seconds;
    result.polish_seconds = run_.polish_seconds;
    result.polish_runs = run_.polish_runs;
    result.polish_improvements = run_.polish_improvements;
    run_.phase_start = Clock::now();
    this->sync_merits();
    attach_final_population(result, owner.variables_, run_.population, run_.pop_size, run_.merits, run_.internal_mode);
    result.final_population_fitness = run_.fitness;
    result.basin_elites = run_.basins;
    run_.timing.attach_population_seconds += seconds_since(run_.phase_start);
    result.timing = run_.timing;
    return result;

}

} } // namespace optiland::optimizer_detail
