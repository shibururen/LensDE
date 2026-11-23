/*
 * Prepare local micro-island seed rows and track attempted adaptive polish sources.
 * Polish phases share the variable transform, attempt identity sets and cumulative timing in run_.
 * Preserve seed/attempt order so retries and duplicate suppression select the same candidates.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

std::vector<std::vector<double> > AdaptiveSession::make_micro_local_seed_rows(const LensSystem& system, const std::vector<double>& anchor_physical, int target_seed_rows) {
                    std::vector<std::vector<double>> rows;
                    rows.reserve(static_cast<std::size_t>(std::max(0, target_seed_rows)));
                    const double seed_scale =
                        std::max(0.0, options.micro_island_seed_scale);
                    int duplicate_rounds = 0;
                    while (static_cast<int>(rows.size()) < target_seed_rows) {
                        std::vector<double> scaled(
                            static_cast<std::size_t>(run_.dim),
                            0.0);
                        for (int j = 0; j < run_.dim; ++j) {
                            const VariableSpec& var =
                                owner.variables_[static_cast<std::size_t>(j)];
                            const double base_value =
                                j < static_cast<int>(anchor_physical.size())
                                    ? anchor_physical[static_cast<std::size_t>(j)]
                                    : var.initial;
                            const double perturbed =
                                this->local_funnel_perturb_physical(
                                    system,
                                    var,
                                    base_value,
                                    seed_scale);
                            scaled[static_cast<std::size_t>(j)] =
                                physical_to_internal(var, perturbed, run_.internal_mode);
                        }
                        if (this->add_unique_scaled_row(rows, scaled)) {
                            duplicate_rounds = 0;
                            continue;
                        }
                        ++duplicate_rounds;
                        if (duplicate_rounds < 3) {
                            continue;
                        }
                        rows.push_back(std::move(scaled));
                        duplicate_rounds = 0;
                    }
                    return rows;
                }

void AdaptiveSession::accumulate_micro_timing(const DeTimingStats& src) {
                run_.timing.evaluation_calls += src.evaluation_calls;
                run_.timing.evaluated_candidates += src.evaluated_candidates;
                run_.timing.setup_seconds += src.setup_seconds;
                run_.timing.initial_evaluation_seconds += src.initial_evaluation_seconds;
                run_.timing.trial_generation_seconds += src.trial_generation_seconds;
                run_.timing.trial_mating_pool_seconds += src.trial_mating_pool_seconds;
                run_.timing.trial_mating_plan_seconds += src.trial_mating_plan_seconds;
                run_.timing.trial_pbest_seconds += src.trial_pbest_seconds;
                run_.timing.trial_mating_select_seconds +=
                    src.trial_mating_select_seconds;
                run_.timing.trial_mutation_seconds += src.trial_mutation_seconds;
                run_.timing.topology_similarity_cache_hits +=
                    src.topology_similarity_cache_hits;
                run_.timing.topology_similarity_cache_misses +=
                    src.topology_similarity_cache_misses;
                run_.timing.topology_shape_diff_cache_hits +=
                    src.topology_shape_diff_cache_hits;
                run_.timing.topology_shape_diff_cache_misses +=
                    src.topology_shape_diff_cache_misses;
                run_.timing.physical_conversion_seconds += src.physical_conversion_seconds;
                run_.timing.evaluation_seconds += src.evaluation_seconds;
                run_.timing.selection_seconds += src.selection_seconds;
                run_.timing.selection_family_gate_seconds +=
                    src.selection_family_gate_seconds;
                run_.timing.selection_compare_seconds +=
                    src.selection_compare_seconds;
                run_.timing.selection_update_seconds +=
                    src.selection_update_seconds;
                run_.timing.selection_basin_seconds +=
                    src.selection_basin_seconds;
                run_.timing.selection_finalize_seconds +=
                    src.selection_finalize_seconds;
                run_.timing.basin_consider_seconds +=
                    src.basin_consider_seconds;
                run_.timing.basin_diversity_match_seconds +=
                    src.basin_diversity_match_seconds;
                run_.timing.basin_distance_seconds +=
                    src.basin_distance_seconds;
                run_.timing.basin_make_elite_seconds +=
                    src.basin_make_elite_seconds;
                run_.timing.basin_trim_seconds +=
                    src.basin_trim_seconds;
                run_.timing.basin_archive_stage_seconds +=
                    src.basin_archive_stage_seconds;
                run_.timing.basin_new_count += src.basin_new_count;
                run_.timing.basin_replace_count += src.basin_replace_count;
                run_.timing.basin_trim_count += src.basin_trim_count;
                run_.timing.best_rescore_seconds += src.best_rescore_seconds;
                run_.timing.attach_population_seconds += src.attach_population_seconds;
                run_.timing.dls_iterations += src.dls_iterations;
                run_.timing.dls_accepted_steps += src.dls_accepted_steps;
                run_.timing.dls_rejected_steps += src.dls_rejected_steps;
                run_.timing.dls_jacobian_candidates += src.dls_jacobian_candidates;
                run_.timing.dls_trial_evaluations += src.dls_trial_evaluations;
                run_.timing.dls_jacobian_seconds += src.dls_jacobian_seconds;
                run_.timing.dls_linear_solve_seconds += src.dls_linear_solve_seconds;
                run_.timing.dls_trial_score_seconds += src.dls_trial_score_seconds;
                run_.timing.dls_trial_residual_seconds +=
                    src.dls_trial_residual_seconds;
                run_.timing.topology_key_builds += src.topology_key_builds;
                run_.timing.topology_descriptor_builds += src.topology_descriptor_builds;
                run_.timing.topology_descriptor_selector_calls +=
                    src.topology_descriptor_selector_calls;
                run_.timing.topology_key_seconds += src.topology_key_seconds;
                run_.timing.topology_descriptor_seconds += src.topology_descriptor_seconds;
                run_.timing.topology_descriptor_selector_seconds +=
                    src.topology_descriptor_selector_seconds;
                run_.timing.cuda.calls += src.cuda.calls;
                run_.timing.cuda.candidates += src.cuda.candidates;
                run_.timing.cuda.context_seconds += src.cuda.context_seconds;
                run_.timing.cuda.host_setup_seconds += src.cuda.host_setup_seconds;
                run_.timing.cuda.host_to_device_seconds += src.cuda.host_to_device_seconds;
                run_.timing.cuda.kernel_seconds += src.cuda.kernel_seconds;
                run_.timing.cuda.device_to_host_seconds += src.cuda.device_to_host_seconds;
                run_.timing.cuda.cleanup_seconds += src.cuda.cleanup_seconds;
                run_.timing.cuda.total_seconds += src.cuda.total_seconds;
            }

void AdaptiveSession::remember_polish_attempt(std::vector<int>& attempted_polish_population_indices, std::vector<std::vector<double> >& attempted_polish_scaled_rows, const AdaptivePolishCandidate& candidate) {
            this->remember_polish_population_index(attempted_polish_population_indices, candidate.population_index);
            this->remember_polish_scaled_row(attempted_polish_scaled_rows, candidate.scaled);
        }

void AdaptiveSession::remember_polish_population_index(std::vector<int>& attempted_polish_population_indices, int population_index) {
            if (population_index < 0) {
                return;
            }
            if (std::find(
                    attempted_polish_population_indices.begin(),
                    attempted_polish_population_indices.end(),
                    population_index) == attempted_polish_population_indices.end()) {
                attempted_polish_population_indices.push_back(population_index);
            }
        }

void AdaptiveSession::remember_polish_scaled_row(std::vector<std::vector<double> >& attempted_polish_scaled_rows, const std::vector<double>& scaled) {
            const bool exists = std::any_of(
                attempted_polish_scaled_rows.begin(),
                attempted_polish_scaled_rows.end(),
                [&](const std::vector<double>& existing) {
                    return this->scaled_rows_equivalent(existing, scaled);
                });
            if (!exists) {
                attempted_polish_scaled_rows.push_back(scaled);
            }
        }

} } // namespace optiland::optimizer_detail
