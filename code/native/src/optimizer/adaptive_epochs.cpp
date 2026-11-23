/*
 * Drive adaptive outer epochs, including per-epoch counters, lifecycle work, polish and refresh.
 * AdaptiveSession::execute calls this after initialization; run_ owns the persistent state and random stream.
 * Preserve phase order and reset boundaries because later generations consume the resulting archives and RNG.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

void AdaptiveSession::run_search_epochs() {
for (int outer = 1; outer <= run_.outer_epochs; ++outer) {
        if (options.max_evaluations > 0 &&
            run_.timing.evaluated_candidates >= options.max_evaluations) {
            break;
        }
        run_.current_outer_epoch = outer;
        this->clear_active_lifecycle_match_caches();
        const auto epoch_start_time = Clock::now();
        PBestGuideStats epoch_pbest_stats;
        DeMatingStats epoch_mating_stats;
        run_.last_pbest_stats = PBestGuideStats{};
        run_.last_mating_stats = DeMatingStats{};
        run_.last_active_refresh_rebalance_family_count = 0;
        run_.last_active_refresh_rebalance_family_limit = 0;
        run_.last_active_refresh_rebalance_max_family_size = 0;
        run_.last_active_refresh_rebalance_candidate_count = 0;
        run_.last_active_refresh_rebalance_selected_count = 0;
        run_.last_active_plateau_new_cooldown_count = 0;
        run_.last_active_plateau_retired_family_count = 0;
        run_.last_active_plateau_active_niche_tracked_count = 0;
        run_.last_active_plateau_evict_count = 0;
        run_.last_active_plateau_evict_candidate_count = 0;
        run_.last_active_plateau_evict_cap_count = 0;
        run_.last_active_plateau_evict_distance_min =
            std::numeric_limits<double>::quiet_NaN();
        run_.last_active_plateau_evict_distance_median =
            std::numeric_limits<double>::quiet_NaN();
        run_.last_active_plateau_evict_distance_max =
            std::numeric_limits<double>::quiet_NaN();
        run_.last_active_plateau_refill_basin_count = 0;
        run_.last_active_plateau_refill_population_count = 0;
        run_.last_active_plateau_refill_refresh_count = 0;
        run_.last_active_plateau_refill_initial_seed_count = 0;
        run_.last_active_plateau_refill_generated_count = 0;
        run_.last_active_plateau_refill_lhs_count = 0;
        run_.last_active_plateau_refill_clone_fallback_count = 0;
        run_.last_active_plateau_refill_rejected_trace_count = 0;
        run_.last_active_plateau_refill_rejected_retired_count = 0;
        run_.last_active_plateau_retired_trial_reject_count = 0;
        run_.last_active_focus_ready_count = 0;
        run_.last_active_focus_cooldown_family_count =
            this->count_active_focus_cooldown_families();
        run_.last_active_focus_stopped_family_count = 0;
        run_.epoch_active_focus_remap_count = 0;
        run_.epoch_active_focus_reset_count = 0;
        run_.epoch_active_focus_selection_count = 0;
        run_.epoch_active_focus_rotation_blocked_count = 0;
        run_.epoch_active_focus_rotation_fallback_count = 0;
        run_.epoch_active_niche_hard_cap_release_count = 0;
        run_.epoch_active_niche_soft_fill_candidate_count = 0;
        run_.epoch_active_niche_soft_fill_move_count = 0;
        run_.epoch_active_niche_soft_fill_anchor_count = 0;
        run_.epoch_active_focus_soft_fill_candidate_count = 0;
        run_.epoch_active_focus_soft_fill_move_count = 0;
        run_.epoch_active_focus_soft_fill_anchor_count = 0;
        run_.epoch_active_focus_stop_release_candidate_count = 0;
        run_.epoch_active_focus_stop_release_move_count = 0;
        run_.epoch_efl_sign_priority_promoted_trial_count = 0;
        run_.epoch_efl_sign_priority_blocked_trial_count = 0;
        run_.epoch_cra_worsening_guard_blocked_trial_count = 0;
        run_.epoch_joint_constraint_anchor_activated_count = 0;
        run_.epoch_joint_constraint_anchor_updated_count = 0;
        run_.epoch_joint_constraint_anchor_blocked_cra_count = 0;
        run_.epoch_joint_constraint_anchor_blocked_distortion_count = 0;
        run_.epoch_joint_constraint_anchor_blocked_both_count = 0;
        run_.last_active_focus_rel_improvement =
            std::numeric_limits<double>::quiet_NaN();
        this->update_adaptive_glass_tolerance(outer, run_.total_de_generations);
        const double epoch_start_merit = run_.best_fitness.score.merit;
        this->emit_trace(
            "adaptive_epoch_start",
            outer,
            0.0,
            epoch_start_merit,
            epoch_start_merit,
            0,
            0,
            0,
            0,
            0,
            run_.used_cuda);
        if (this->adaptive_glass_tolerance_stop_ready()) {
            this->emit_trace(
                "adaptive_early_stop_glass_bridge_ready",
                outer,
                0.0,
                epoch_start_merit,
                run_.adaptive_glass_tolerance.current,
                0,
                0,
                0,
                0,
                0,
                run_.used_cuda);
            break;
        }

        if (options.inject_best && run_.best_scaled.size() == static_cast<std::size_t>(run_.dim) &&
            !this->active_plateau_fitness_matches_retired(run_.best_scaled, run_.best_fitness)) {
            set_population_row(run_.population, 0, run_.best_scaled, owner.variables_, run_.internal_mode);
            run_.fitness[0] = run_.best_fitness;
            run_.fitness[0].population_index = 0;
            
            this->sync_merits();
        }
        if (this->force_retire_initial_active_plateau_frontier(outer)) {
            this->emit_trace(
                "adaptive_plateau_retire_evict",
                outer,
                0.0,
                epoch_start_merit,
                run_.best_fitness.score.merit,
                0,
                0,
                0,
                0,
                0,
                run_.used_cuda);
            run_.last_active_plateau_new_cooldown_count = 0;
            run_.last_active_plateau_retired_family_count = 0;
            run_.last_active_plateau_active_niche_tracked_count = 0;
            run_.last_active_plateau_evict_count = 0;
            run_.last_active_plateau_evict_candidate_count = 0;
            run_.last_active_plateau_evict_cap_count = 0;
            run_.last_active_plateau_evict_distance_min =
                std::numeric_limits<double>::quiet_NaN();
            run_.last_active_plateau_evict_distance_median =
                std::numeric_limits<double>::quiet_NaN();
            run_.last_active_plateau_evict_distance_max =
                std::numeric_limits<double>::quiet_NaN();
            run_.last_active_plateau_refill_basin_count = 0;
            run_.last_active_plateau_refill_population_count = 0;
            run_.last_active_plateau_refill_refresh_count = 0;
            run_.last_active_plateau_refill_initial_seed_count = 0;
            run_.last_active_plateau_refill_generated_count = 0;
            run_.last_active_plateau_refill_lhs_count = 0;
            run_.last_active_plateau_refill_clone_fallback_count = 0;
            run_.last_active_plateau_refill_rejected_trace_count = 0;
            run_.last_active_plateau_refill_rejected_retired_count = 0;
        }
        this->inject_active_basins();
        this->emit_trace(
            "adaptive_active_reinject",
            outer,
            0.0,
            epoch_start_merit,
            epoch_start_merit,
            0,
            0,
            0,
            0,
            0,
            run_.used_cuda);

        const auto de_start = Clock::now();
        int epoch_de_generations = 0;
        int epoch_accepted_trials = 0;
        int epoch_retired_trial_reject_count = 0;
        const bool family_escape_configured =
            options.geometry_memory_family_escape_pulse &&
            run_.geometry_memory_enabled &&
            options.geometry_memory_difference_key_trigger &&
            options.geometry_memory_difference_key_similarity_threshold <
                1.0 - 1.0e-12;
        const bool exact_escape_configured =
            options.geometry_memory_exact_escape_pulse &&
            run_.geometry_memory_enabled &&
            options.geometry_memory_difference_key_trigger;
        int exact_escape_resident_generations = 0;
        int exact_escape_pulse_remaining = 0;
        int exact_escape_cooldown_remaining = 0;
        std::string exact_escape_resident_key;
        int family_escape_stagnant_generations = 0;
        int family_escape_pulse_remaining = 0;
        int family_escape_cooldown_remaining = 0;
        double family_escape_reference_merit = run_.best_fitness.score.merit;
        
        std::vector<DeferredNicheCandidate> deferred_niche_candidates;
        
    this->run_epoch_de_generations(RunEpochDeGenerationsContext{outer, epoch_pbest_stats, epoch_mating_stats, epoch_de_generations, epoch_accepted_trials, epoch_retired_trial_reject_count, family_escape_configured, exact_escape_configured, exact_escape_resident_generations, exact_escape_pulse_remaining, exact_escape_cooldown_remaining, exact_escape_resident_key, family_escape_stagnant_generations, family_escape_pulse_remaining, family_escape_cooldown_remaining, family_escape_reference_merit, deferred_niche_candidates});

        const double epoch_de_seconds = seconds_since(de_start);
        run_.de_seconds += epoch_de_seconds;
        run_.last_pbest_stats = epoch_pbest_stats;
        run_.last_mating_stats = epoch_mating_stats;
        run_.last_active_plateau_retired_trial_reject_count =
            epoch_retired_trial_reject_count;
        this->emit_trace(
            "adaptive_de_end",
            outer,
            epoch_de_seconds,
            epoch_start_merit,
            epoch_start_merit,
            epoch_de_generations,
            0,
            0,
            0,
            epoch_accepted_trials,
            run_.used_cuda);

        

        

        

        const double before_polish_merit = run_.best_fitness.score.merit;
        const auto polish_start = Clock::now();
        
        

        std::vector<AdaptivePolishCandidate> all_polish_candidates;
        std::vector<int> attempted_polish_population_indices;
        std::vector<std::vector<double>> attempted_polish_scaled_rows;
        
        
        

        

        

        int epoch_micro_island_runs = 0;
        int epoch_micro_island_improvements = 0;
        int epoch_micro_island_archived = 0;
        int epoch_micro_island_early_stopped = 0;
        int epoch_micro_island_retired = 0;
        int epoch_micro_island_seed_count = 0;
        int epoch_micro_island_requested_runs = 0;
        bool epoch_micro_island_used_cuda = false;
        double epoch_micro_island_seconds = 0.0;
        double epoch_micro_island_de_seconds = 0.0;
        double epoch_micro_island_polish_seconds = 0.0;
        const double before_micro_island_merit = run_.best_fitness.score.merit;
        if (options.micro_island_enabled) {
            const MicroIslandSummary micro_summary = this->run_micro_island_stage(outer);
            epoch_micro_island_requested_runs = micro_summary.requested_runs;
            epoch_micro_island_runs = micro_summary.runs;
            epoch_micro_island_improvements = micro_summary.improvements;
            epoch_micro_island_archived = micro_summary.archived;
            epoch_micro_island_early_stopped = micro_summary.early_stopped;
            epoch_micro_island_retired = micro_summary.retired;
            epoch_micro_island_seed_count = micro_summary.seed_count;
            epoch_micro_island_used_cuda = micro_summary.used_cuda;
            epoch_micro_island_seconds = micro_summary.seconds;
            epoch_micro_island_de_seconds = micro_summary.de_seconds;
            epoch_micro_island_polish_seconds = micro_summary.polish_seconds;
            run_.de_seconds += epoch_micro_island_seconds;
            this->emit_trace(
                "adaptive_micro_island_end",
                outer,
                epoch_micro_island_seconds,
                epoch_start_merit,
                before_micro_island_merit,
                0,
                0,
                0,
                0,
                0,
                epoch_micro_island_used_cuda,
                0,
                0,
                0,
                0,
                -1,
                false,
                std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN(),
                epoch_micro_island_runs,
                epoch_micro_island_improvements,
                epoch_micro_island_archived,
                epoch_micro_island_seed_count,
                epoch_micro_island_seconds,
                epoch_micro_island_de_seconds,
                epoch_micro_island_polish_seconds,
                epoch_micro_island_early_stopped,
                epoch_micro_island_retired,
                &micro_summary.records);
        }

        int epoch_polish_improvements = 0;
        int epoch_polish_merit_strict_improvements = 0;
        int epoch_polish_merit_neutral = 0;
        int epoch_polish_merit_worse = 0;
        bool epoch_polish_used_cuda = false;
        const int micro_island_missing_runs =
            options.micro_island_enabled && options.micro_island_replace_polish
                ? std::max(
                      0,
                      epoch_micro_island_requested_runs - epoch_micro_island_runs)
                : 0;
        const bool run_full_standard_polish =
            !options.micro_island_enabled || !options.micro_island_replace_polish;
        const int epoch_requested_polish_count =
            run_full_standard_polish
                ? run_.requested_polish_count
                : std::min(run_.requested_polish_count, micro_island_missing_runs);
        const int epoch_polish_refill_max_extra =
            run_full_standard_polish
                ? run_.polish_refill_max_extra
                : std::min(run_.polish_refill_max_extra, epoch_requested_polish_count);
        const int epoch_max_polish_attempts =
            epoch_requested_polish_count > 0
                ? epoch_requested_polish_count + epoch_polish_refill_max_extra
                : 0;
        const bool run_standard_polish = epoch_requested_polish_count > 0;
        
    this->run_standard_polish_phase(RunStandardPolishPhaseContext{all_polish_candidates, attempted_polish_population_indices, attempted_polish_scaled_rows, epoch_polish_improvements, epoch_polish_merit_strict_improvements, epoch_polish_merit_neutral, epoch_polish_merit_worse, epoch_polish_used_cuda, epoch_requested_polish_count, epoch_max_polish_attempts, run_standard_polish});

        const double epoch_polish_seconds =
            run_standard_polish ? seconds_since(polish_start) : 0.0;
        run_.polish_seconds += epoch_polish_seconds;
        this->emit_trace(
            "adaptive_polish_end",
            outer,
            epoch_polish_seconds,
            epoch_start_merit,
            before_polish_merit,
            0,
            static_cast<int>(all_polish_candidates.size()),
            epoch_polish_improvements,
            0,
            0,
            epoch_polish_used_cuda,
            epoch_polish_merit_strict_improvements,
            epoch_polish_merit_neutral,
            epoch_polish_merit_worse);

        int refreshed_count = 0;
        int refresh_archive_used = 0;
        int epoch_refresh_effective_count = 0;
        bool epoch_refresh_adaptive_skipped = false;
        double epoch_refresh_acceptance_rate =
            std::numeric_limits<double>::quiet_NaN();
        double epoch_refresh_rel_improvement =
            std::numeric_limits<double>::quiet_NaN();
        
    this->refresh_epoch_population(RefreshEpochPopulationContext{outer, epoch_mating_stats, epoch_start_merit, epoch_de_generations, epoch_accepted_trials, attempted_polish_population_indices, refreshed_count, refresh_archive_used, epoch_refresh_effective_count, epoch_refresh_adaptive_skipped, epoch_refresh_acceptance_rate, epoch_refresh_rel_improvement});


        for (int i = 0; i < run_.pop_size; ++i) {
            this->consider_basin(population_row(run_.population, i, run_.dim), run_.fitness[static_cast<std::size_t>(i)],
                           "epoch_end", false);
        }
        this->update_best_from_population();
        this->update_active_basin_lifecycle(outer);
        this->update_active_focus_lifecycle(outer);
        const HybridTraceEvent epoch_trace = this->emit_trace(
            "adaptive_epoch_end",
            outer,
            seconds_since(epoch_start_time),
            epoch_start_merit,
            epoch_start_merit,
            epoch_de_generations,
            static_cast<int>(all_polish_candidates.size()),
            epoch_polish_improvements,
            refreshed_count,
            epoch_accepted_trials,
            run_.used_cuda,
                    epoch_polish_merit_strict_improvements,
                    epoch_polish_merit_neutral,
                    epoch_polish_merit_worse,
                    refresh_archive_used,
                    epoch_refresh_effective_count,
                    epoch_refresh_adaptive_skipped,
                    epoch_refresh_acceptance_rate,
                    epoch_refresh_rel_improvement,
                    epoch_micro_island_runs,
                    epoch_micro_island_improvements,
                    epoch_micro_island_archived,
                    epoch_micro_island_seed_count,
                    epoch_micro_island_seconds,
                    epoch_micro_island_de_seconds,
                    epoch_micro_island_polish_seconds,
                    epoch_micro_island_early_stopped,
                    epoch_micro_island_retired);
        if (progress) {
            progress(
                run_.total_de_generations,
                run_.best_fitness.score,
                scaled_to_physical(owner.variables_, run_.best_scaled, run_.internal_mode));
        }
        const int clear_stop_target =
            std::max(0, options.basins.active_plateau_retire_stop_after_clears);
        if (clear_stop_target > 0 &&
            run_.active_plateau_retire_clear_count >= clear_stop_target) {
            this->emit_trace(
                "adaptive_early_stop_plateau_retire_clear_count",
                outer,
                0.0,
                epoch_start_merit,
                static_cast<double>(run_.active_plateau_retire_clear_count),
                0,
                0,
                0,
                0,
                0,
                run_.used_cuda);
            break;
        }
        const int focus_clear_stop_target =
            std::max(0, options.basins.active_focus_stop_after_clears);
        if (focus_clear_stop_target > 0 &&
            run_.active_focus_stop_clear_count >= focus_clear_stop_target) {
            this->sync_adaptive_glass_tolerance_trigger_state(outer);
            if (run_.adaptive_glass_tolerance.bridge_active) {
                if (this->adaptive_glass_tolerance_stop_ready()) {
                    this->emit_trace(
                        "adaptive_early_stop_glass_bridge_ready",
                        outer,
                        0.0,
                        epoch_start_merit,
                        run_.adaptive_glass_tolerance.current,
                        0,
                        0,
                        0,
                        0,
                        0,
                        run_.used_cuda);
                    break;
                }
                this->emit_trace(
                    "adaptive_focus_clear_stop_deferred_glass_bridge",
                    outer,
                    0.0,
                    epoch_start_merit,
                    static_cast<double>(run_.active_focus_stop_clear_count),
                    0,
                    0,
                    0,
                    0,
                    0,
                    run_.used_cuda);
            } else {
                this->emit_trace(
                    "adaptive_early_stop_focus_stop_clear_count",
                    outer,
                    0.0,
                    epoch_start_merit,
                    static_cast<double>(run_.active_focus_stop_clear_count),
                    0,
                    0,
                    0,
                    0,
                    0,
                    run_.used_cuda);
                break;
            }
        }
        if (options.early_stop_enabled) {
            const double criterion = this->trace_metric_value(epoch_trace);
            const bool use_qualified_topology_count =
                run_.early_stop_topology_discovery && options.early_stop_use_qualified_topologies;
            const int topology_count_for_stop =
                run_.early_stop_basin_discovery
                    ? epoch_trace.basin_count
                    : (use_qualified_topology_count
                           ? epoch_trace.basin_qualified_topology_count
                           : epoch_trace.basin_topology_count);
            run_.early_stop_history.push_back(
                {outer, topology_count_for_stop, criterion});
            run_.early_stop_best_topology_count =
                std::max(run_.early_stop_best_topology_count, topology_count_for_stop);
            const int min_epochs = std::max(1, options.early_stop_min_epochs);
            const int window = std::max(1, options.early_stop_window);
            if (outer >= min_epochs &&
                static_cast<int>(run_.early_stop_history.size()) > window) {
                const EarlyStopPoint& previous =
                    run_.early_stop_history[run_.early_stop_history.size() - 1U - static_cast<std::size_t>(window)];
                if (run_.early_stop_tail_active) {
                    --run_.early_stop_tail_remaining;
                    if (run_.early_stop_tail_remaining <= 0) {
                        this->emit_trace(
                            "adaptive_early_stop_tail_end",
                            outer,
                            0.0,
                            epoch_start_merit,
                            criterion,
                            0,
                            0,
                            0,
                            0,
                            0,
                            run_.used_cuda);
                        break;
                    }
                    continue;
                }
                auto emit_early_stop_or_defer = [&](double diagnostic_value) {
                    if (outer <= run_.active_plateau_retire_defer_until_epoch) {
                        this->emit_trace(
                            "adaptive_early_stop_deferred_plateau_retire",
                            outer,
                            0.0,
                            epoch_start_merit,
                            diagnostic_value,
                            0,
                            0,
                            0,
                            0,
                            0,
                            run_.used_cuda);
                        return false;
                    }
                    if (!this->early_stop_lpsr_ready()) {
                        this->emit_trace(
                            "adaptive_early_stop_deferred_lpsr",
                            outer,
                            0.0,
                            epoch_start_merit,
                            diagnostic_value,
                            0,
                            0,
                            0,
                            0,
                            0,
                            run_.used_cuda);
                        return false;
                    }
                    if (!this->adaptive_glass_tolerance_ready()) {
                        this->emit_trace(
                            "adaptive_early_stop_deferred_glass_tolerance",
                            outer,
                            0.0,
                            epoch_start_merit,
                            diagnostic_value,
                            0,
                            0,
                            0,
                            0,
                            0,
                            run_.used_cuda);
                        return false;
                    }
                    const int tail_epochs = std::max(0, options.early_stop_tail_epochs);
                    if (tail_epochs > 0) {
                        run_.early_stop_tail_active = true;
                        run_.early_stop_tail_remaining = tail_epochs;
                        this->emit_trace(
                            "adaptive_early_stop_tail_start",
                            outer,
                            0.0,
                            epoch_start_merit,
                            diagnostic_value,
                            0,
                            0,
                            0,
                            0,
                            0,
                            run_.used_cuda);
                        return false;
                    }
                    this->emit_trace(
                        "adaptive_early_stop",
                        outer,
                        0.0,
                        epoch_start_merit,
                        diagnostic_value,
                        0,
                        0,
                        0,
                        0,
                        0,
                        run_.used_cuda);
                    return true;
                };
                bool topology_ready =
                    run_.early_stop_best_merit ||
                    options.early_stop_min_topologies <= 0 ||
                    run_.early_stop_best_topology_count >= options.early_stop_min_topologies;
                if (!run_.early_stop_best_merit &&
                    options.early_stop_require_topology_plateau) {
                    topology_ready = topology_ready &&
                                     topology_count_for_stop <= previous.topology_count;
                }
                if (run_.early_stop_discovery_count) {
                    const double best_merit_max = options.early_stop_best_merit_max;
                    const bool best_merit_ready =
                        !(std::isfinite(best_merit_max) && best_merit_max > 0.0) ||
                        (std::isfinite(epoch_trace.best_score.merit) &&
                         epoch_trace.best_score.merit < best_merit_max);
                    const int previous_count = std::max(0, previous.topology_count);
                    const int current_count = std::max(0, topology_count_for_stop);
                    const int added_count = std::max(0, current_count - previous_count);
                    const double discovery_growth =
                        previous_count > 0
                            ? static_cast<double>(added_count) /
                                  static_cast<double>(previous_count)
                            : (current_count > 0
                                  ? std::numeric_limits<double>::infinity()
                                  : 0.0);
                    const double discovery_threshold =
                        std::max(
                            std::max(0.0, options.early_stop_min_abs_improve),
                            static_cast<double>(previous_count) *
                                std::max(0.0, options.early_stop_min_rel_improve));
                    if (best_merit_ready &&
                        topology_ready &&
                        static_cast<double>(added_count) < discovery_threshold) {
                        if (emit_early_stop_or_defer(discovery_growth)) {
                            break;
                        }
                    }
                    continue;
                }
                const double abs_improvement =
                    this->absolute_improvement(previous.criterion, criterion);
                const double improvement_threshold =
                    this->early_stop_minimum_improvement(previous.criterion);
                if (topology_ready &&
                    std::isfinite(abs_improvement) &&
                    abs_improvement < improvement_threshold) {
                    if (emit_early_stop_or_defer(criterion)) {
                        break;
                    }
                }
            }
        }
    }
}

} } // namespace optiland::optimizer_detail
