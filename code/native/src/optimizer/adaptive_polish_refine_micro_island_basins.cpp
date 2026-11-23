/*
 * Refine micro-island basins and fill standard adaptive polish attempts from remaining sources.
 * Epoch polish owns the attempt context; run_ supplies incumbent rows, fitness, timing and RNG.
 * Preserve source traversal, retry limits and acceptance order before committing polished rows.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

void AdaptiveSession::refine_micro_island_basins(const RefineMicroIslandBasinsContext& frame_) {
    auto& outer = frame_.outer;
    auto& summary = frame_.summary;
    auto& micro_effective_variable_count = frame_.micro_effective_variable_count;
    auto& resolved_micro_population = frame_.resolved_micro_population;
    auto& source_basins = frame_.source_basins;
    auto& micro_retired_keys = frame_.micro_retired_keys;
    auto& micro_retired_scaled_rows = frame_.micro_retired_scaled_rows;
    auto& micro_retire_allow_similarity = frame_.micro_retire_allow_similarity;

for (const BasinElite& basin : source_basins) {
                if (options.max_evaluations > 0 &&
                    run_.timing.evaluated_candidates >= options.max_evaluations) {
                    break;
                }
                if (basin.scaled_values.size() != static_cast<std::size_t>(run_.dim)) {
                    continue;
                }

                const std::string family_key =
                    this->active_lifecycle_key_for_basin(basin);
                const std::vector<double> anchor_scaled = basin.scaled_values;
                const std::vector<double> anchor_physical =
                    scaled_to_physical(owner.variables_, anchor_scaled, run_.internal_mode);
                LensSystem anchor_system =
                    apply_mixed_variables(owner.base_, owner.variables_, anchor_physical, owner.glass_search_);
                std::vector<VariableSpec> micro_variables = owner.variables_;
                for (int j = 0; j < run_.dim; ++j) {
                    micro_variables[static_cast<std::size_t>(j)].initial =
                        anchor_physical[static_cast<std::size_t>(j)];
                }

                const int target_seed_rows =
                    std::max(0, resolved_micro_population - 1);
                std::vector<std::vector<double>> seed_scaled_rows =
                    this->make_micro_local_seed_rows(
                        anchor_system,
                        anchor_physical,
                        target_seed_rows);
                PopulationGenerationOptions micro_generation =
                    options.refresh_generation;
                micro_generation.mode =
                    PopulationGenerationMode::RestrictedPerturbation;
                micro_generation.trace_feasible_gate = false;
                micro_generation.target_count = 0;
                micro_generation.max_candidates = 0;
                micro_generation.max_seconds = 0.0;
                micro_generation.trace_archive_enabled = false;
                micro_generation.adaptive_trigger_enabled = false;
                micro_generation.seed_pool_path.clear();
                micro_generation.seed_pool_values.clear();
                micro_generation.seed_pool_fraction = 0.0;
                micro_generation.seed_pool_perturbation = 0.0;

                AdaptiveHybridDeDlsOptions micro_options = options;
                micro_options.trace = {};
                micro_options.write_adaptive_trace = false;
                micro_options.micro_island_enabled = false;
                micro_options.micro_island_replace_polish = false;
                const int micro_outer_epochs =
                    std::max(1, options.micro_island_outer_epochs);
                const bool micro_hybrid_enabled = micro_outer_epochs > 1;
                auto half_global_count = [](int configured, int fallback) {
                    if (configured >= 0) {
                        return configured;
                    }
                    return static_cast<int>(std::ceil(
                        0.5 * static_cast<double>(std::max(0, fallback))));
                };
                const int global_polish_candidate_base =
                    options.polish_candidate_count > 0
                        ? options.polish_candidate_count
                        : std::max(0, options.parent_count);
                const int global_polish_refill_base =
                    options.polish_refill_max_extra >= 0
                        ? options.polish_refill_max_extra
                        : global_polish_candidate_base;
                const int resolved_micro_polish_candidates =
                    micro_hybrid_enabled
                        ? std::max(
                              1,
                              half_global_count(
                                  options.micro_island_polish_candidate_count,
                                  global_polish_candidate_base))
                        : 0;
                const int resolved_micro_polish_refill_max_extra =
                    micro_hybrid_enabled
                        ? std::max(
                              0,
                              half_global_count(
                                  options.micro_island_polish_refill_max_extra,
                                  global_polish_refill_base))
                        : 0;
                micro_options.outer_epochs = micro_outer_epochs;
                micro_options.polish_enabled = micro_hybrid_enabled;
                micro_options.polish_candidate_count =
                    resolved_micro_polish_candidates;
                micro_options.polish_refill_max_extra =
                    resolved_micro_polish_refill_max_extra;
                micro_options.variant =
                    AdaptiveDeVariant::JadePBestArchiveLpsrFeas;
                micro_options.refresh_fraction = 0.0;
                micro_options.de.population_size =
                    resolved_micro_population;
                micro_options.de.use_popsize_multiplier = false;
                micro_options.de.max_generations =
                    std::max(1, options.micro_island_de_generations);
                micro_options.de.progress_every = 0;
                micro_options.de.seed = static_cast<unsigned int>(run_.rng());
                micro_options.initial_popsize_multiplier =
                    micro_options.de.population_size;
                micro_options.minimum_popsize_multiplier = 1;
                micro_options.minimum_population_size =
                    std::clamp(
                        static_cast<int>(std::ceil(
                            4.0 * static_cast<double>(micro_effective_variable_count))),
                        5,
                        micro_options.de.population_size);
                micro_options.use_lpsr = true;
                micro_options.lpsr_horizon_generations =
                    std::max(
                        1,
                        micro_options.de.max_generations *
                            micro_options.outer_epochs);
                micro_options.pbest_scope = PBestGuideScope::Mixed;
                micro_options.pbest_basin_rate = 0.0;
                micro_options.pbest_same_key_rate = 0.0;
                micro_options.pbest_global_rate = 1.0;
                micro_options.pbest_global_fallback = true;
                micro_options.de_mating_scope = DeMatingScope::Global;
                micro_options.de_mating_local_rate = 0.0;
                micro_options.de_mating_min_pool = 4;
                micro_options.de_mating_strict_local = false;
                micro_options.refresh_generation = micro_generation;
                micro_options.refresh_generation.trace_archive_enabled = false;
                micro_options.refresh_generation.trace_archive_capacity = 0;
                micro_options.refresh_generation.adaptive_trigger_enabled = false;
                micro_options.basins = options.basins;
                micro_options.basins.enabled = false;
                micro_options.basins.max_basins = 0;
                micro_options.basins.output_count = 0;
                micro_options.basins.active_diversity = false;
                micro_options.basins.active_count = 0;
                micro_options.basins.active_max_slots_per_family = 0;
                micro_options.basins.active_mating_match_mode = "exact_key";
                micro_options.basins.active_family_parameter_merge_enabled = false;
                micro_options.basins.active_slot_strict_frozen_family = false;
                micro_options.basins.active_local_cohort = false;
                micro_options.basins.active_local_cohort_size = 0;
                micro_options.basins.active_local_cohort_fraction = 0.0;
                micro_options.basins.active_local_cohort_max_distance = 0.0;
                micro_options.basins.active_mating_quota = false;
                micro_options.basins.active_refresh_rebalance = false;
                micro_options.basins.active_refresh_max_family_fraction = 0.0;
                micro_options.basins.active_refresh_min_local_fraction = 1.0;
                micro_options.basins.active_plateau_lifecycle = false;
                micro_options.basins.active_plateau_retire_evict = false;
                micro_options.basins.active_plateau_retire_frontier_on_first_update = false;
                micro_options.basins.balanced_refresh = false;
                micro_options.early_stop_enabled =
                    options.micro_island_early_stop_enabled &&
                    micro_outer_epochs > 1;
                micro_options.early_stop_metric = "best_merit";
                micro_options.early_stop_min_epochs = std::max(
                    1,
                    options.micro_island_early_stop_min_epochs);
                micro_options.early_stop_window = std::max(
                    1,
                    options.micro_island_early_stop_window);
                micro_options.early_stop_min_topologies = 0;
                micro_options.early_stop_min_rel_improve = std::max(
                    0.0,
                    options.micro_island_early_stop_min_rel_improve);
                micro_options.early_stop_min_abs_improve = std::max(
                    0.0,
                    options.micro_island_early_stop_min_abs_improve);
                micro_options.early_stop_min_lpsr_progress = 0.0;
                micro_options.early_stop_tail_epochs = 0;
                micro_options.early_stop_require_topology_plateau = false;
                micro_options.early_stop_best_merit_max = 0.0;
                micro_options.early_stop_use_qualified_topologies = false;
                if (options.max_evaluations > 0) {
                    const long long remaining_evaluations =
                        std::max<long long>(
                            0,
                            static_cast<long long>(options.max_evaluations) -
                                run_.timing.evaluated_candidates);
                    micro_options.max_evaluations =
                        remaining_evaluations >
                                static_cast<long long>(
                                    std::numeric_limits<int>::max())
                            ? std::numeric_limits<int>::max()
                            : static_cast<int>(remaining_evaluations);
                } else {
                    micro_options.max_evaluations = 0;
                }

                micro_options.de.initial_population_values.clear();
                micro_options.de.initial_population_values.reserve(
                    static_cast<std::size_t>(target_seed_rows));
                for (int row = 0;
                     row < target_seed_rows &&
                     row < static_cast<int>(seed_scaled_rows.size());
                     ++row) {
                    micro_options.de.initial_population_values.push_back(
                        scaled_to_physical(
                            owner.variables_,
                            seed_scaled_rows[static_cast<std::size_t>(row)],
                            run_.internal_mode));
                }

                DifferentialEvolution micro_optimizer(
                    anchor_system,
                    micro_variables,
                    owner.merit_config_);
                micro_optimizer.set_glass_search_context(owner.glass_search_);
                const auto island_start = Clock::now();
                OptimizationResult micro_result =
                    micro_optimizer.run_adaptive_hybrid_de_dls(micro_options, {});
                const double island_seconds = seconds_since(island_start);

                summary.used_cuda = summary.used_cuda || micro_result.used_cuda;
                summary.seconds += island_seconds;
                summary.de_seconds += micro_result.de_seconds;
                summary.polish_seconds += micro_result.polish_seconds;
                summary.seed_count += 1 +
                    static_cast<int>(
                        micro_options.de.initial_population_values.size());
                ++summary.runs;
                const int island_index = summary.runs;
                this->accumulate_micro_timing(micro_result.timing);
                const int planned_micro_generations =
                    std::max(1, micro_options.outer_epochs) *
                    std::max(1, micro_options.de.max_generations);
                const bool micro_local_budget_exhausted =
                    micro_options.max_evaluations > 0 &&
                    micro_result.timing.evaluated_candidates >=
                        micro_options.max_evaluations;
                const bool global_budget_exhausted =
                    options.max_evaluations > 0 &&
                    run_.timing.evaluated_candidates >= options.max_evaluations;
                const bool micro_early_stopped =
                    micro_options.early_stop_enabled &&
                    micro_result.generations < planned_micro_generations &&
                    !micro_local_budget_exhausted &&
                    !global_budget_exhausted;
                bool micro_retired_this_island = false;
                MicroRetireDecision retire_decision;
                retire_decision.action =
                    micro_early_stopped ? "early_stop_no_retire" : "not_early_stopped";
                retire_decision.key = family_key;
                if (micro_early_stopped) {
                    ++summary.early_stopped;
                    if (options.micro_island_retire_on_early_stop) {
                        retire_decision = this->add_micro_retired_basin(outer, micro_retired_keys, micro_retired_scaled_rows, micro_retire_allow_similarity, basin);
                        if (retire_decision.retired) {
                            ++summary.retired;
                        }
                        micro_retired_this_island =
                            retire_decision.retired ||
                            retire_decision.action == "duplicate_global_retired" ||
                            retire_decision.action == "duplicate_in_micro_batch";
                    } else {
                        retire_decision.action = "retire_on_early_stop_disabled";
                    }
                }

                auto make_micro_record = [&](int survivor_rank) {
                    MicroIslandTraceRecord record;
                    record.epoch = outer;
                    record.island_index = island_index;
                    record.source_basin_id = basin.id;
                    record.source_key = family_key;
                    record.source_topology_key = basin.topology_key;
                    record.source_descriptor_key = basin.descriptor_key;
                    record.source_similarity_family_id =
                        basin.similarity_family_id;
                    record.source_merit = basin.fitness.score.merit;
                    record.planned_generations = planned_micro_generations;
                    record.actual_generations = micro_result.generations;
                    record.early_stopped = micro_early_stopped;
                    record.retired = micro_retired_this_island;
                    record.retired_unique = retire_decision.unique;
                    record.retire_action = retire_decision.action;
                    record.retire_key = retire_decision.key;
                    record.survivor_rank = survivor_rank;
                    return record;
                };

                if (micro_result.final_population_size <= 0 ||
                    micro_result.final_population_dim != run_.dim ||
                    micro_result.final_population_fitness.empty()) {
                    MicroIslandTraceRecord record = make_micro_record(-1);
                    record.basin_archive_action = "no_final_population";
                    summary.records.push_back(std::move(record));
                    continue;
                }

                std::vector<int> survivor_order = sorted_fitness_indices(
                    micro_result.final_population_fitness,
                    options);
                const int survivor_limit = std::min(
                    std::max(1, options.micro_island_survivor_count),
                    static_cast<int>(survivor_order.size()));
                std::vector<int> replacement_targets =
                    sorted_fitness_indices(run_.fitness, options);
                std::reverse(replacement_targets.begin(), replacement_targets.end());

                std::vector<int> used_targets;
                used_targets.reserve(static_cast<std::size_t>(survivor_limit));
                bool replaced_population = false;
                for (int order_pos = 0; order_pos < survivor_limit; ++order_pos) {
                    const int survivor_index =
                        survivor_order[static_cast<std::size_t>(order_pos)];
                    if (survivor_index < 0 ||
                        survivor_index >= micro_result.final_population_size ||
                        survivor_index >=
                            static_cast<int>(micro_result.final_population_fitness.size())) {
                        continue;
                    }

                    std::vector<double> survivor_physical(
                        static_cast<std::size_t>(run_.dim),
                        0.0);
                    for (int j = 0; j < run_.dim; ++j) {
                        survivor_physical[static_cast<std::size_t>(j)] =
                            micro_result.final_population_values[
                                static_cast<std::size_t>(survivor_index) * run_.dim + j];
                    }
                    const std::vector<double> survivor_scaled =
                        physical_to_scaled(
                            owner.variables_,
                            survivor_physical,
                            run_.internal_mode);
                    CandidateFitness survivor_fitness =
                        micro_result.final_population_fitness[
                            static_cast<std::size_t>(survivor_index)];
                    survivor_fitness.source = "micro_island";

                    int target_index = -1;
                    if (!micro_retired_this_island) {
                        for (int candidate_target : replacement_targets) {
                            if (candidate_target < 0 || candidate_target >= run_.pop_size ||
                                std::find(
                                    used_targets.begin(),
                                    used_targets.end(),
                                    candidate_target) != used_targets.end()) {
                                continue;
                            }
                            if (candidate_better(
                                    survivor_fitness,
                                    run_.fitness[static_cast<std::size_t>(candidate_target)],
                                    options)) {
                                target_index = candidate_target;
                                break;
                            }
                        }
                    }

                    const std::string survivor_source =
                        micro_retired_this_island
                            ? "micro_island_retired_archive"
                            : target_index >= 0
                            ? "micro_island_population"
                            : "micro_island_archive";
                    CandidateFitness archived_fitness = survivor_fitness;
                    archived_fitness.population_index = -1;
                    archived_fitness.source = survivor_source;
                    BasinConsiderResult archive_result = this->consider_basin(
                        survivor_scaled,
                        archived_fitness,
                        survivor_source,
                        false);
                    this->add_traceable_refresh_archive(
                        survivor_scaled,
                        archived_fitness,
                        survivor_source);
                    ++summary.archived;

                    MicroIslandTraceRecord record =
                        make_micro_record(order_pos + 1);
                    record.survivor_merit =
                        survivor_fitness.score.merit;
                    record.survivor_trace_feasible =
                        survivor_fitness.trace_feasible;
                    record.survivor_topology_key =
                        survivor_fitness.topology_key;
                    record.survivor_descriptor_key =
                        survivor_fitness.descriptor_key;
                    record.archive_source = survivor_source;
                    record.basin_archive_action = archive_result.action;
                    record.basin_archive_id = archive_result.basin_id;
                    record.nearest_basin_id = archive_result.nearest_basin_id;
                    record.nearest_basin_distance =
                        archive_result.nearest_distance;
                    record.previous_basin_source =
                        archive_result.previous_source;
                    record.population_replaced = target_index >= 0;
                    record.population_target_index = target_index;
                    summary.records.push_back(std::move(record));

                    if (target_index < 0) {
                        continue;
                    }

                    this->stage_traceable_refresh_archive(
                        population_row(run_.population, target_index, run_.dim),
                        run_.fitness[static_cast<std::size_t>(target_index)],
                        "micro_island_replaced");
                    set_population_row(
                        run_.population,
                        target_index,
                        survivor_scaled,
                        owner.variables_,
                        run_.internal_mode);
                    survivor_fitness.population_index = target_index;
                    survivor_fitness.source = "micro_island";
                    run_.fitness[static_cast<std::size_t>(target_index)] =
                        survivor_fitness;
                    used_targets.push_back(target_index);
                    ++summary.improvements;
                    replaced_population = true;
                }

                if (replaced_population) {
                    this->sync_merits();
                    this->update_best_from_population();
                }
            }
}

void AdaptiveSession::run_standard_polish_phase(const RunStandardPolishPhaseContext& frame_) {
    auto& all_polish_candidates = frame_.all_polish_candidates;
    auto& attempted_polish_population_indices = frame_.attempted_polish_population_indices;
    auto& attempted_polish_scaled_rows = frame_.attempted_polish_scaled_rows;
    auto& epoch_polish_improvements = frame_.epoch_polish_improvements;
    auto& epoch_polish_merit_strict_improvements = frame_.epoch_polish_merit_strict_improvements;
    auto& epoch_polish_merit_neutral = frame_.epoch_polish_merit_neutral;
    auto& epoch_polish_merit_worse = frame_.epoch_polish_merit_worse;
    auto& epoch_polish_used_cuda = frame_.epoch_polish_used_cuda;
    auto& epoch_requested_polish_count = frame_.epoch_requested_polish_count;
    auto& epoch_max_polish_attempts = frame_.epoch_max_polish_attempts;
    auto& run_standard_polish = frame_.run_standard_polish;

if (run_standard_polish) {
            
    this->fill_standard_polish_attempts(FillStandardPolishAttemptsContext{all_polish_candidates, attempted_polish_population_indices, attempted_polish_scaled_rows, epoch_polish_improvements, epoch_polish_merit_strict_improvements, epoch_polish_merit_neutral, epoch_polish_merit_worse, epoch_polish_used_cuda, epoch_requested_polish_count, epoch_max_polish_attempts});

        }
}

void AdaptiveSession::fill_standard_polish_attempts(const FillStandardPolishAttemptsContext& frame_) {
    auto& all_polish_candidates = frame_.all_polish_candidates;
    auto& attempted_polish_population_indices = frame_.attempted_polish_population_indices;
    auto& attempted_polish_scaled_rows = frame_.attempted_polish_scaled_rows;
    auto& epoch_polish_improvements = frame_.epoch_polish_improvements;
    auto& epoch_polish_merit_strict_improvements = frame_.epoch_polish_merit_strict_improvements;
    auto& epoch_polish_merit_neutral = frame_.epoch_polish_merit_neutral;
    auto& epoch_polish_merit_worse = frame_.epoch_polish_merit_worse;
    auto& epoch_polish_used_cuda = frame_.epoch_polish_used_cuda;
    auto& epoch_requested_polish_count = frame_.epoch_requested_polish_count;
    auto& epoch_max_polish_attempts = frame_.epoch_max_polish_attempts;

while (epoch_polish_improvements < epoch_requested_polish_count &&
                   static_cast<int>(all_polish_candidates.size()) < epoch_max_polish_attempts) {
                const int remaining_polish_accepts =
                    epoch_requested_polish_count - epoch_polish_improvements;
                const int remaining_polish_attempts =
                    epoch_max_polish_attempts -
                    static_cast<int>(all_polish_candidates.size());
                const std::vector<AdaptivePolishCandidate> polish_candidates =
                    this->select_polish_candidates(
                        std::min(remaining_polish_accepts, remaining_polish_attempts),
                        attempted_polish_population_indices,
                        attempted_polish_scaled_rows);
                if (polish_candidates.empty()) {
                    break;
                }
                all_polish_candidates.insert(
                    all_polish_candidates.end(),
                    polish_candidates.begin(),
                    polish_candidates.end());
                for (const auto& candidate : polish_candidates) {
                    this->remember_polish_attempt(attempted_polish_population_indices, attempted_polish_scaled_rows, candidate);
                }

                const std::vector<AdaptivePolishResult> polish_results =
                    this->run_polish_batch(polish_candidates);
                for (const auto& polished : polish_results) {
                    if (polished.error) {
                        std::rethrow_exception(polished.error);
                    }
                }
                run_.polish_runs += static_cast<int>(polish_candidates.size());
                if (!options.batched_polish && !polish_candidates.empty()) {
                    ++run_.timing.evaluation_calls;
                    run_.timing.evaluated_candidates +=
                        static_cast<long long>(polish_candidates.size());
                }

                for (const auto& polished : polish_results) {
                    if (!polished.completed) {
                        continue;
                    }
                    run_.timing.selection_family_gate_seconds +=
                        polished.polished.timing.selection_family_gate_seconds;
                    run_.timing.selection_compare_seconds +=
                        polished.polished.timing.selection_compare_seconds;
                    run_.timing.selection_update_seconds +=
                        polished.polished.timing.selection_update_seconds;
                    run_.timing.selection_basin_seconds +=
                        polished.polished.timing.selection_basin_seconds;
                    run_.timing.selection_finalize_seconds +=
                        polished.polished.timing.selection_finalize_seconds;
                    run_.timing.basin_consider_seconds +=
                        polished.polished.timing.basin_consider_seconds;
                    run_.timing.basin_diversity_match_seconds +=
                        polished.polished.timing.basin_diversity_match_seconds;
                    run_.timing.basin_distance_seconds +=
                        polished.polished.timing.basin_distance_seconds;
                    run_.timing.basin_make_elite_seconds +=
                        polished.polished.timing.basin_make_elite_seconds;
                    run_.timing.basin_trim_seconds +=
                        polished.polished.timing.basin_trim_seconds;
                    run_.timing.basin_archive_stage_seconds +=
                        polished.polished.timing.basin_archive_stage_seconds;
                    run_.timing.basin_new_count +=
                        polished.polished.timing.basin_new_count;
                    run_.timing.basin_replace_count +=
                        polished.polished.timing.basin_replace_count;
                    run_.timing.basin_trim_count +=
                        polished.polished.timing.basin_trim_count;
                    run_.timing.dls_iterations +=
                        polished.polished.timing.dls_iterations;
                    run_.timing.dls_accepted_steps +=
                        polished.polished.timing.dls_accepted_steps;
                    run_.timing.dls_rejected_steps +=
                        polished.polished.timing.dls_rejected_steps;
                    run_.timing.dls_jacobian_candidates +=
                        polished.polished.timing.dls_jacobian_candidates;
                    run_.timing.dls_trial_evaluations +=
                        polished.polished.timing.dls_trial_evaluations;
                    run_.timing.dls_jacobian_seconds +=
                        polished.polished.timing.dls_jacobian_seconds;
                    run_.timing.dls_linear_solve_seconds +=
                        polished.polished.timing.dls_linear_solve_seconds;
                    run_.timing.dls_trial_score_seconds +=
                        polished.polished.timing.dls_trial_score_seconds;
                    run_.timing.dls_trial_residual_seconds +=
                        polished.polished.timing.dls_trial_residual_seconds;
                    const AdaptivePolishCandidate& candidate =
                        polish_candidates[polished.candidate_index];
                    epoch_polish_used_cuda =
                        epoch_polish_used_cuda || polished.polished.used_cuda;
                    run_.used_cuda = run_.used_cuda || polished.polished.used_cuda;
                    CandidateFitness polished_fitness = polished.fitness;
                    classify_polish_merit_delta(
                        candidate.fitness.score.merit,
                        polished_fitness.score.merit,
                        epoch_polish_merit_strict_improvements,
                        epoch_polish_merit_neutral,
                        epoch_polish_merit_worse);
                    const bool accept =
                        !options.rollback ||
                        (options.polish_accept_merit_only &&
                         polish_merit_not_worse(polished_fitness, candidate.fitness)) ||
                        candidate_better(polished_fitness, candidate.fitness, options);
                    if (!accept) {
                        continue;
                    }
                    int target_index = candidate.population_index;
                    if (target_index < 0 || target_index >= run_.pop_size) {
                        const auto order = sorted_fitness_indices(run_.fitness, options);
                        target_index = order.empty() ? 0 : order.back();
                        if (candidate_better(
                                run_.fitness[static_cast<std::size_t>(target_index)],
                                polished_fitness,
                                options)) {
                            continue;
                        }
                    }
                    set_population_row(
                        run_.population,
                        target_index,
                        polished.polished_scaled,
                        owner.variables_,
                        run_.internal_mode);
                    polished_fitness.population_index = target_index;
                    run_.fitness[static_cast<std::size_t>(target_index)] =
                        polished_fitness;
                    this->remember_polish_population_index(attempted_polish_population_indices, target_index);
                    this->remember_polish_scaled_row(attempted_polish_scaled_rows, polished.polished_scaled);
                    this->sync_merits();
                    this->consider_basin(
                        polished.polished_scaled,
                        polished_fitness,
                        "polish",
                        true);
                    if (candidate_better(polished_fitness, run_.best_fitness, options)) {
                        run_.best_idx = target_index;
                        run_.best_scaled = polished.polished_scaled;
                        run_.best_fitness = polished_fitness;
                    }
                    ++run_.polish_improvements;
                    ++epoch_polish_improvements;
                    if (epoch_polish_improvements >= epoch_requested_polish_count) {
                        break;
                    }
                }
            }
}

} } // namespace optiland::optimizer_detail
