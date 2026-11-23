// Translate basin, niche, active-focus, and plateau CLI controls into adaptive options.
// OptimizationCommand prepares these policies before constructing the adaptive native run.
// Defaults and aliases affect family occupancy and lifecycle decisions; preserve their precedence.

#include "optimization_command.hpp"

namespace lensde { namespace cli { namespace detail {

int OptimizationCommand::adaptive_basin_options() {
    adaptive_options.basins.enabled = !has_arg(argc, argv, "--basin-disable");
    adaptive_options.topology_summary_enabled =
        !has_arg(argc, argv, "--adaptive-no-topology-summary") &&
        !has_arg(argc, argv, "--no-topology-summary");
    adaptive_options.basins.max_basins =
        int_arg(argc, argv, "--basin-max", adaptive_options.basins.max_basins);
    adaptive_options.basins.radius =
        double_arg(argc, argv, "--basin-radius", adaptive_options.basins.radius);
    adaptive_options.basins.output_count =
        int_arg(argc, argv, "--basin-output-count",
            adaptive_options.basins.output_count);
    adaptive_options.basins.include_near_feasible =
        !has_arg(argc, argv, "--basin-feasible-only");
    adaptive_options.basins.polish_basins =
        !has_arg(argc, argv, "--basin-no-polish");
    adaptive_options.basins.distance_metric = distance_metric_from_string(
        string_arg(argc, argv, "--basin-distance-metric",
            distance_metric_name(adaptive_options.basins.distance_metric)));
    adaptive_options.basins.distance_include_conic =
        has_arg(argc, argv, "--basin-distance-include-conic");
    adaptive_options.basins.air_thickness_distance_cap =
        double_arg(argc, argv, "--basin-air-thickness-distance-cap",
            adaptive_options.basins.air_thickness_distance_cap);
    if (has_arg(argc, argv, "--basin-active-diversity")) {
        adaptive_options.basins.active_diversity = true;
    }
    if (has_arg(argc, argv, "--basin-no-active-diversity")) {
        adaptive_options.basins.active_diversity = false;
    }
    adaptive_options.basins.active_count =
        int_arg(argc, argv, "--basin-active-count",
            adaptive_options.basins.active_count);
    adaptive_options.basins.active_selection_mode =
        string_arg(argc, argv, "--basin-active-selection-mode",
            string_arg(argc, argv, "--basin-active-selection",
                adaptive_options.basins.active_selection_mode));
    adaptive_options.basins.protected_fraction =
        double_arg(argc, argv, "--basin-protected-fraction",
            adaptive_options.basins.protected_fraction);
    if (has_arg(argc, argv, "--basin-active-quality-gate")) {
        adaptive_options.basins.active_quality_gate = true;
    }
    if (has_arg(argc, argv, "--basin-no-active-quality-gate")) {
        adaptive_options.basins.active_quality_gate = false;
    }
    if (has_arg(argc, argv, "--basin-active-require-trace-feasible")) {
        adaptive_options.basins.active_require_trace_feasible = true;
    }
    if (has_arg(argc, argv, "--basin-active-allow-trace-infeasible")) {
        adaptive_options.basins.active_require_trace_feasible = false;
    }
    adaptive_options.basins.active_min_edge =
        double_arg(argc, argv, "--basin-active-min-edge",
            adaptive_options.basins.active_min_edge);
    adaptive_options.basins.active_max_merit_ratio =
        double_arg(argc, argv, "--basin-active-max-merit-ratio",
            adaptive_options.basins.active_max_merit_ratio);
    adaptive_options.basins.active_max_merit_delta =
        double_arg(argc, argv, "--basin-active-max-merit-delta",
            adaptive_options.basins.active_max_merit_delta);
    adaptive_options.basins.active_max_slots_per_family =
        int_arg(argc, argv, "--basin-active-max-slots-per-family",
            adaptive_options.basins.active_max_slots_per_family);
    adaptive_options.basins.active_mating_match_mode =
        string_arg(argc, argv, "--basin-active-mating-match-mode",
            adaptive_options.basins.active_mating_match_mode);
    adaptive_options.basins.active_family_similarity_threshold =
        std::clamp(
            double_arg(
                argc,
                argv,
                "--basin-active-family-similarity-threshold",
                adaptive_options.basins.active_family_similarity_threshold),
            0.0,
            1.0);
    adaptive_options.basins.active_family_max_shape_diff =
        std::max(
            -1,
            int_arg(
                argc,
                argv,
                "--basin-active-family-max-shape-diff",
                adaptive_options.basins.active_family_max_shape_diff));
    if (has_arg(argc, argv, "--basin-active-family-parameter-merge")) {
        adaptive_options.basins.active_family_parameter_merge_enabled = true;
    }
    if (has_arg(argc, argv, "--basin-no-active-family-parameter-merge")) {
        adaptive_options.basins.active_family_parameter_merge_enabled = false;
    }
    adaptive_options.basins.active_family_parameter_merge_distance =
        std::max(
            0.0,
            double_arg(
                argc,
                argv,
                "--basin-active-family-parameter-merge-distance",
                adaptive_options.basins.active_family_parameter_merge_distance));
    adaptive_options.basins.active_family_parameter_merge_metric =
        distance_metric_from_string(
            string_arg(
                argc,
                argv,
                "--basin-active-family-parameter-merge-metric",
                distance_metric_name(
                    adaptive_options.basins.active_family_parameter_merge_metric)));
    if (has_arg(argc, argv, "--basin-active-archive-only")) {
        adaptive_options.basins.active_archive_only = true;
    }
    if (has_arg(argc, argv, "--basin-no-active-archive-only")) {
        adaptive_options.basins.active_archive_only = false;
    }
    return kContinueCommand;
}

int OptimizationCommand::adaptive_niche_options() {
    if (has_arg(argc, argv, "--basin-active-slot-strict-frozen-family")) {
        adaptive_options.basins.active_slot_strict_frozen_family = true;
    }
    if (has_arg(argc, argv, "--basin-no-active-slot-strict-frozen-family")) {
        adaptive_options.basins.active_slot_strict_frozen_family = false;
    }
    if (has_arg(argc, argv, "--basin-active-local-cohort")) {
        adaptive_options.basins.active_local_cohort = true;
    }
    if (has_arg(argc, argv, "--basin-no-active-local-cohort")) {
        adaptive_options.basins.active_local_cohort = false;
    }
    adaptive_options.basins.active_local_cohort_size =
        int_arg(argc, argv, "--basin-active-local-cohort-size",
            adaptive_options.basins.active_local_cohort_size);
    adaptive_options.basins.active_local_cohort_fraction =
        double_arg(argc, argv, "--basin-active-local-cohort-fraction",
            adaptive_options.basins.active_local_cohort_fraction);
    adaptive_options.basins.active_local_cohort_max_distance =
        double_arg(argc, argv, "--basin-active-local-cohort-max-distance",
            adaptive_options.basins.active_local_cohort_max_distance);
    if (has_arg(argc, argv, "--basin-active-mating-quota")) {
        adaptive_options.basins.active_mating_quota = true;
    }
    if (has_arg(argc, argv, "--basin-no-active-mating-quota")) {
        adaptive_options.basins.active_mating_quota = false;
    }
    if (has_arg(argc, argv, "--basin-active-niche-tracking")) {
        adaptive_options.basins.active_niche_tracking = true;
    }
    if (has_arg(argc, argv, "--basin-no-active-niche-tracking")) {
        adaptive_options.basins.active_niche_tracking = false;
    }
    adaptive_options.basins.active_niche_start_epoch =
        std::max(
            1,
            int_arg(
                argc,
                argv,
                "--basin-active-niche-start-epoch",
                adaptive_options.basins.active_niche_start_epoch));
    if (has_arg(argc, argv, "--basin-active-niche-survival")) {
        adaptive_options.basins.active_niche_survival = true;
        adaptive_options.basins.active_niche_tracking = true;
    }
    if (has_arg(argc, argv, "--basin-no-active-niche-survival")) {
        adaptive_options.basins.active_niche_survival = false;
    }
    adaptive_options.basins.active_niche_min_quota =
        std::max(
            0,
            int_arg(
                argc,
                argv,
                "--basin-active-niche-min-quota",
                adaptive_options.basins.active_niche_min_quota));
    adaptive_options.basins.active_niche_target_quota =
        std::max(
            0,
            int_arg(
                argc,
                argv,
                "--basin-active-niche-target-quota",
                adaptive_options.basins.active_niche_target_quota));
    adaptive_options.basins.active_niche_family_cap_fraction =
        std::clamp(
            double_arg(
                argc,
                argv,
                "--basin-active-niche-family-cap-fraction",
                adaptive_options.basins.active_niche_family_cap_fraction),
            0.0,
            1.0);
    adaptive_options.basins.active_niche_repair_interval =
        std::max(
            1,
            int_arg(
                argc,
                argv,
                "--basin-active-niche-repair-interval",
                adaptive_options.basins.active_niche_repair_interval));
    adaptive_options.basins.active_niche_repair_fraction =
        std::clamp(
            double_arg(
                argc,
                argv,
                "--basin-active-niche-repair-fraction",
                adaptive_options.basins.active_niche_repair_fraction),
            0.0,
            1.0);
    if (has_arg(argc, argv, "--basin-active-niche-rebalance")) {
        adaptive_options.basins.active_niche_rebalance = true;
        adaptive_options.basins.active_niche_survival = true;
        adaptive_options.basins.active_niche_tracking = true;
    }
    if (has_arg(argc, argv, "--basin-no-active-niche-rebalance")) {
        adaptive_options.basins.active_niche_rebalance = false;
    }
    adaptive_options.basins.active_niche_rebalance_interval =
        std::max(
            1,
            int_arg(
                argc,
                argv,
                "--basin-active-niche-rebalance-interval",
                adaptive_options.basins.active_niche_rebalance_interval));
    adaptive_options.basins.active_niche_rebalance_fraction =
        std::clamp(
            double_arg(
                argc,
                argv,
                "--basin-active-niche-rebalance-fraction",
                adaptive_options.basins.active_niche_rebalance_fraction),
            0.0,
            1.0);
    if (has_arg(argc, argv, "--basin-active-niche-soft-fill")) {
        adaptive_options.basins.active_niche_soft_fill = true;
        adaptive_options.basins.active_niche_survival = true;
        adaptive_options.basins.active_niche_tracking = true;
    }
    if (has_arg(argc, argv, "--basin-no-active-niche-soft-fill")) {
        adaptive_options.basins.active_niche_soft_fill = false;
    }
    adaptive_options.basins.active_niche_elite_seed_count =
        std::max(
            1,
            int_arg(
                argc,
                argv,
                "--basin-active-niche-elite-seed-count",
                adaptive_options.basins.active_niche_elite_seed_count));
    return kContinueCommand;
}

int OptimizationCommand::adaptive_focus_options() {
    if (has_arg(argc, argv, "--basin-active-focus-rebalance")) {
        adaptive_options.basins.active_focus_rebalance = true;
        adaptive_options.basins.active_niche_rebalance = true;
        adaptive_options.basins.active_niche_survival = true;
        adaptive_options.basins.active_niche_tracking = true;
    }
    if (has_arg(argc, argv, "--basin-no-active-focus-rebalance")) {
        adaptive_options.basins.active_focus_rebalance = false;
    }
    adaptive_options.basins.active_focus_start_epoch =
        std::max(
            1,
            int_arg(
                argc,
                argv,
                "--basin-active-focus-start-epoch",
                adaptive_options.basins.active_focus_start_epoch));
    if (has_arg(argc, argv, "--basin-active-focus-initial-best-family")) {
        adaptive_options.basins.active_focus_initial_best_family = true;
    }
    if (has_arg(argc, argv, "--basin-no-active-focus-initial-best-family")) {
        adaptive_options.basins.active_focus_initial_best_family = false;
    }
    adaptive_options.basins.active_focus_family_count =
        std::max(
            1,
            int_arg(
                argc,
                argv,
                "--basin-active-focus-family-count",
                adaptive_options.basins.active_focus_family_count));
    adaptive_options.basins.active_focus_population_fraction =
        std::clamp(
            double_arg(
                argc,
                argv,
                "--basin-active-focus-population-fraction",
                adaptive_options.basins.active_focus_population_fraction),
            0.0,
            1.0);
    adaptive_options.basins.active_focus_min_quota =
        std::max(
            0,
            int_arg(
                argc,
                argv,
                "--basin-active-focus-min-quota",
                adaptive_options.basins.active_focus_min_quota));
    if (has_arg(argc, argv, "--basin-active-focus-soft-fill")) {
        adaptive_options.basins.active_focus_soft_fill = true;
    }
    if (has_arg(argc, argv, "--basin-no-active-focus-soft-fill")) {
        adaptive_options.basins.active_focus_soft_fill = false;
    }
    adaptive_options.basins.active_focus_elite_seed_count =
        std::max(
            1,
            int_arg(
                argc,
                argv,
                "--basin-active-focus-elite-seed-count",
                adaptive_options.basins.active_focus_elite_seed_count));
    adaptive_options.basins.active_monitor_family_cap =
        std::max(
            0,
            int_arg(
                argc,
                argv,
                "--basin-active-monitor-family-cap",
                adaptive_options.basins.active_monitor_family_cap));
    adaptive_options.basins.active_focus_stop_rel_improvement =
        std::max(
            0.0,
            double_arg(
                argc,
                argv,
                "--basin-active-focus-stop-rel-improvement",
                adaptive_options.basins.active_focus_stop_rel_improvement));
    adaptive_options.basins.active_focus_ready_fraction =
        std::clamp(
            double_arg(
                argc,
                argv,
                "--basin-active-focus-ready-fraction",
                adaptive_options.basins.active_focus_ready_fraction),
            0.0,
            1.0);
    adaptive_options.basins.active_focus_cooldown_epochs =
        std::max(
            0,
            int_arg(
                argc,
                argv,
                "--basin-active-focus-cooldown-epochs",
                adaptive_options.basins.active_focus_cooldown_epochs));
    adaptive_options.basins.active_focus_stop_after_clears =
        std::max(
            0,
            int_arg(
                argc,
                argv,
                "--basin-active-focus-stop-after-clears",
                adaptive_options.basins.active_focus_stop_after_clears));
    if (has_arg(argc, argv, "--basin-active-focus-rotation")) {
        adaptive_options.basins.active_focus_rotation = true;
    }
    if (has_arg(argc, argv, "--basin-no-active-focus-rotation")) {
        adaptive_options.basins.active_focus_rotation = false;
    }
    adaptive_options.basins.active_focus_rotation_history_epochs =
        std::max(
            0,
            int_arg(
                argc,
                argv,
                "--basin-active-focus-rotation-history-epochs",
                adaptive_options.basins.active_focus_rotation_history_epochs));
    adaptive_options.basins.active_focus_rotation_similarity_threshold =
        std::clamp(
            double_arg(
                argc,
                argv,
                "--basin-active-focus-rotation-similarity-threshold",
                adaptive_options.basins.active_focus_rotation_similarity_threshold),
            0.0,
            1.0);
    if (has_arg(argc, argv, "--basin-active-focus-stop-release")) {
        adaptive_options.basins.active_focus_stop_release = true;
    }
    if (has_arg(argc, argv, "--basin-no-active-focus-stop-release")) {
        adaptive_options.basins.active_focus_stop_release = false;
    }
    adaptive_options.basins.active_focus_stop_release_fraction =
        std::clamp(
            double_arg(
                argc,
                argv,
                "--basin-active-focus-stop-release-fraction",
                adaptive_options.basins.active_focus_stop_release_fraction),
            0.0,
            1.0);
    return kContinueCommand;
}

int OptimizationCommand::adaptive_plateau_options() {
    if (has_arg(argc, argv, "--basin-active-refresh-rebalance")) {
        adaptive_options.basins.active_refresh_rebalance = true;
    }
    if (has_arg(argc, argv, "--basin-no-active-refresh-rebalance")) {
        adaptive_options.basins.active_refresh_rebalance = false;
    }
    adaptive_options.basins.active_refresh_max_family_fraction =
        double_arg(argc, argv, "--basin-active-refresh-max-family-fraction",
            adaptive_options.basins.active_refresh_max_family_fraction);
    adaptive_options.basins.active_refresh_min_local_fraction =
        double_arg(argc, argv, "--basin-active-refresh-min-local-fraction",
            adaptive_options.basins.active_refresh_min_local_fraction);
    if (has_arg(argc, argv, "--basin-active-plateau-lifecycle")) {
        adaptive_options.basins.active_plateau_lifecycle = true;
    }
    if (has_arg(argc, argv, "--basin-no-active-plateau-lifecycle")) {
        adaptive_options.basins.active_plateau_lifecycle = false;
    }
    if (has_arg(argc, argv, "--basin-active-plateau-track-active-niches")) {
        adaptive_options.basins.active_plateau_track_active_niches = true;
        adaptive_options.basins.active_plateau_lifecycle = true;
    }
    if (has_arg(argc, argv, "--basin-no-active-plateau-track-active-niches")) {
        adaptive_options.basins.active_plateau_track_active_niches = false;
    }
    adaptive_options.basins.active_plateau_window =
        int_arg(argc, argv, "--basin-active-plateau-window",
            adaptive_options.basins.active_plateau_window);
    adaptive_options.basins.active_plateau_min_rel_improvement =
        double_arg(argc, argv, "--basin-active-plateau-min-rel-improvement",
            adaptive_options.basins.active_plateau_min_rel_improvement);
    adaptive_options.basins.active_plateau_local_quota_scale =
        double_arg(argc, argv, "--basin-active-plateau-local-quota-scale",
            adaptive_options.basins.active_plateau_local_quota_scale);
    adaptive_options.basins.active_plateau_external_probe_rate =
        double_arg(argc, argv, "--basin-active-plateau-external-probe-rate",
            adaptive_options.basins.active_plateau_external_probe_rate);
    adaptive_options.basins.active_plateau_retire_after =
        int_arg(argc, argv, "--basin-active-plateau-retire-after",
            adaptive_options.basins.active_plateau_retire_after);
    adaptive_options.basins.active_plateau_cooldown_epochs =
        int_arg(argc, argv, "--basin-active-plateau-cooldown-epochs",
            adaptive_options.basins.active_plateau_cooldown_epochs);
    adaptive_options.basins.active_plateau_frontier_count =
        int_arg(argc, argv, "--basin-active-plateau-frontier-count",
            adaptive_options.basins.active_plateau_frontier_count);
    adaptive_options.basins.active_plateau_frontier_multiplier =
        double_arg(argc, argv, "--basin-active-plateau-frontier-multiplier",
            adaptive_options.basins.active_plateau_frontier_multiplier);
    if (has_arg(argc, argv, "--basin-active-plateau-retire-evict")) {
        adaptive_options.basins.active_plateau_retire_evict = true;
    }
    if (has_arg(argc, argv, "--basin-no-active-plateau-retire-evict")) {
        adaptive_options.basins.active_plateau_retire_evict = false;
    }
    adaptive_options.basins.active_plateau_retire_evict_fraction =
        double_arg(argc, argv, "--basin-active-plateau-retire-evict-fraction",
            adaptive_options.basins.active_plateau_retire_evict_fraction);
    adaptive_options.basins.active_plateau_retire_evict_max_fraction =
        double_arg(argc, argv, "--basin-active-plateau-retire-evict-max-fraction",
            adaptive_options.basins.active_plateau_retire_evict_max_fraction);
    adaptive_options.basins.active_plateau_retire_evict_min =
        int_arg(argc, argv, "--basin-active-plateau-retire-evict-min",
            adaptive_options.basins.active_plateau_retire_evict_min);
    if (has_arg(argc, argv, "--basin-active-plateau-retire-evict-parameter-only")) {
        adaptive_options.basins.active_plateau_retire_evict_parameter_only = true;
    }
    if (has_arg(argc, argv, "--basin-no-active-plateau-retire-evict-parameter-only")) {
        adaptive_options.basins.active_plateau_retire_evict_parameter_only = false;
    }
    adaptive_options.basins.active_plateau_retire_evict_parameter_distance =
        double_arg(argc, argv, "--basin-active-plateau-retire-evict-parameter-distance",
            adaptive_options.basins.active_plateau_retire_evict_parameter_distance);
    if (has_arg(argc, argv, "--basin-active-plateau-retire-frontier-on-first-update")) {
        adaptive_options.basins.active_plateau_retire_frontier_on_first_update = true;
    }
    if (has_arg(argc, argv, "--basin-no-active-plateau-retire-frontier-on-first-update")) {
        adaptive_options.basins.active_plateau_retire_frontier_on_first_update = false;
    }
    adaptive_options.basins.active_plateau_retire_defer_early_stop_epochs =
        int_arg(argc, argv, "--basin-active-plateau-retire-defer-early-stop-epochs",
            adaptive_options.basins.active_plateau_retire_defer_early_stop_epochs);
    adaptive_options.basins.active_plateau_retire_stop_after_clears =
        int_arg(argc, argv, "--basin-active-plateau-retire-stop-after-clears",
            adaptive_options.basins.active_plateau_retire_stop_after_clears);
    if (has_arg(argc, argv, "--basin-balanced-refresh")) {
        adaptive_options.basins.balanced_refresh = true;
    }
    if (has_arg(argc, argv, "--basin-no-balanced-refresh")) {
        adaptive_options.basins.balanced_refresh = false;
    }
    if (has_arg(argc, argv, "--basin-semantic-diversity")) {
        adaptive_options.basins.semantic_diversity = true;
    }
    if (has_arg(argc, argv, "--basin-no-semantic-diversity")) {
        adaptive_options.basins.semantic_diversity = false;
    }
    adaptive_options.basins.semantic_max_per_topology =
        int_arg(argc, argv, "--basin-semantic-max-per-topology",
            adaptive_options.basins.semantic_max_per_topology);
    {
        TopologyDescriptorOptions& topo =
            adaptive_options.basins.topology_descriptor;
        topo.mode = topology_diversity_mode_from_string(
            string_arg(
                argc,
                argv,
                "--topology-diversity-mode",
                string_arg(
                    argc,
                    argv,
                    "--basin-topology-diversity-mode",
                    topology_diversity_mode_name(topo.mode))));
        topo.window_lmin = std::max(
            1,
            int_arg(argc, argv, "--topology-descriptor-window-lmin", topo.window_lmin));
        topo.window_lmax = std::max(
            topo.window_lmin,
            int_arg(argc, argv, "--topology-descriptor-window-lmax", topo.window_lmax));
        topo.max_active_axes = std::max(
            0,
            int_arg(argc, argv, "--topology-descriptor-max-active-axes", topo.max_active_axes));
        topo.audit_interval_generations = std::max(
            1,
            int_arg(
                argc,
                argv,
                "--topology-descriptor-audit-interval",
                topo.audit_interval_generations));
        topo.weak_tau_low = std::max(
            0.0,
            double_arg(argc, argv, "--topology-descriptor-weak-tau-low", topo.weak_tau_low));
        topo.weak_tau_high = std::max(
            topo.weak_tau_low,
            double_arg(
                argc,
                argv,
                "--topology-descriptor-weak-tau-high",
                topo.weak_tau_high));
        topo.similarity_threshold = std::clamp(
            double_arg(
                argc,
                argv,
                "--topology-similarity-threshold",
                topo.similarity_threshold),
            0.0,
            1.0);
        if (has_arg(argc, argv, "--topology-descriptor-eager-diagnostic")) {
            topo.lazy_diagnostic_strings = false;
        }
        if (has_arg(argc, argv, "--topology-descriptor-lazy-diagnostic")) {
            topo.lazy_diagnostic_strings = true;
        }
        if (has_arg(argc, argv, "--topology-descriptor-local-distance")) {
            topo.use_local_continuous_distance = true;
        }
        if (has_arg(argc, argv, "--topology-descriptor-no-local-distance")) {
            topo.use_local_continuous_distance = false;
        }
    }
    return kContinueCommand;
}

} } }
