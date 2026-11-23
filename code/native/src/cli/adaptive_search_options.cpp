// Configure adaptive DE budgets, constraint handling, scaling, and early-stop policies.
// The assembled options are consumed by run_adaptive_hybrid_de_dls rather than ordinary DE.
// Keep total-generation horizons, optimizer seeds, and constraint-mode defaults at their original scope.

#include "optimization_command.hpp"

namespace lensde { namespace cli { namespace detail {

int OptimizationCommand::adaptive_de_options() {
    // adaptive_options is owned by this command stage.
    adaptive_options.de.population_size = population;
    adaptive_options.de.popsize_multiplier =
        int_arg(argc, argv, "--adaptive-initial-popsize-multiplier",
            int_arg(argc, argv, "--popsize", adaptive_options.initial_popsize_multiplier));
    adaptive_options.initial_popsize_multiplier = adaptive_options.de.popsize_multiplier;
    adaptive_options.de.use_popsize_multiplier =
        !has_arg(argc, argv, "--pop") || has_arg(argc, argv, "--popsize") ||
        has_arg(argc, argv, "--adaptive-initial-popsize-multiplier");
    adaptive_options.allow_small_initial_population =
        has_arg(argc, argv, "--adaptive-allow-small-initial-population");
    adaptive_options.de.max_generations = int_arg(argc, argv, "--hybrid-de-gen",
        std::max(1, int_arg(argc, argv, "--de-inner-gen", std::max(1, generations / 6))));
    adaptive_options.de.mutation_min =
        double_arg(argc, argv, "--mutation-min", adaptive_options.de.mutation_min);
    adaptive_options.de.mutation_max =
        double_arg(argc, argv, "--mutation-max", adaptive_options.de.mutation_max);
    adaptive_options.de.recombination =
        double_arg(argc, argv, "--recombination", adaptive_options.de.recombination);
    adaptive_options.de.tolerance = 0.0;
    adaptive_options.de.absolute_tolerance = 0.0;
    adaptive_options.de.seed = static_cast<unsigned int>(int_arg(argc, argv, "--seed", 20260515));
    adaptive_options.de.use_cuda = allow_cuda_for_merit;
    adaptive_options.de.cuda_min_batch_size = de_cuda_min_batch_size;
    if (glass_search.enabled) {
        adaptive_options.de.glass_mutation_rate =
            double_arg(argc, argv, "--glass-mutation-rate", glass_search.mutation_rate);
        adaptive_options.de.glass_random_rate =
            double_arg(argc, argv, "--glass-random-rate", glass_search.random_rate);
        adaptive_options.de.glass_pbest_inherit_rate =
            double_arg(argc, argv, "--glass-pbest-inherit-rate", glass_search.pbest_inherit_rate);
        adaptive_options.de.glass_neighbor_count =
            int_arg(argc, argv, "--glass-neighbor-count", glass_search.neighbor_count);
        adaptive_options.de.glass_local_search =
            glass_search.local_search && !has_arg(argc, argv, "--glass-no-local-search");
    }
    adaptive_options.de.progress_every =
        int_arg(argc, argv, "--progress-every", std::max(1, generations / 8));
    adaptive_options.de.initial_population_values = initial_population_values;
    adaptive_options.de.internal_scaling = internal_scaling;
    apply_initial_generation_args(argc, argv, adaptive_options.de.initial_generation);
    apply_population_generation_target_defaults(
        adaptive_options.de.initial_generation,
        merit,
        lens);
    adaptive_options.outer_epochs = int_arg(argc, argv, "--hybrid-outer",
        std::max(1, int_arg(argc, argv, "--outer", 6)));
    const long long expected_total_de_generations_raw =
        static_cast<long long>(std::max(1, adaptive_options.de.max_generations)) *
        static_cast<long long>(std::max(1, adaptive_options.outer_epochs));
    expected_total_de_generations = static_cast<int>(
                    std::min<long long>(
                        expected_total_de_generations_raw,
                        std::numeric_limits<int>::max()));
    adaptive_checkpoint_every = std::max(
                    1,
                    int_arg(
                        argc,
                        argv,
                        "--adaptive-checkpoint-every",
                        std::max(1, expected_total_de_generations / 2)));
    adaptive_options.parent_count = int_arg(argc, argv, "--hybrid-parents",
        int_arg(argc, argv, "--parents", adaptive_options.parent_count));
    adaptive_options.polish_candidate_count = int_arg(argc, argv, "--polish-candidates",
        int_arg(argc, argv, "--adaptive-polish-candidates",
            adaptive_options.polish_candidate_count));
    adaptive_options.polish_refill_max_extra =
        std::max(-1, int_arg(argc, argv, "--polish-refill-max-extra",
            int_arg(argc, argv, "--adaptive-polish-refill-max-extra",
                adaptive_options.polish_refill_max_extra)));
    {
        const std::string polish_selection = normalized_option_token(
            string_arg(argc, argv, "--polish-selection",
                string_arg(argc, argv, "--adaptive-polish-selection", "merit")));
        adaptive_options.polish_topology_balanced =
            polish_selection == "topology" ||
            polish_selection == "topology_balanced" ||
            polish_selection == "balanced_topology" ||
            polish_selection == "semantic" ||
            polish_selection == "semantic_balanced";
        if (has_arg(argc, argv, "--polish-topology-balanced")) {
            adaptive_options.polish_topology_balanced = true;
        }
        if (has_arg(argc, argv, "--polish-merit-selection")) {
            adaptive_options.polish_topology_balanced = false;
        }
    }
    adaptive_options.variant = adaptive_de_variant_from_string(
        string_arg(argc, argv, "--de-variant",
            adaptive_de_variant_name(adaptive_options.variant)));
    return kContinueCommand;
}

int OptimizationCommand::adaptive_constraint_options() {
    adaptive_options.constraint_handling = constraint_handling_from_string(
        string_arg(argc, argv, "--constraint-handling",
            constraint_handling_name(adaptive_options.constraint_handling)));
    if (has_arg(argc, argv, "--de-efl-sign-priority") ||
        has_arg(argc, argv, "--adaptive-efl-sign-priority")) {
        adaptive_options.efl_sign_priority = true;
    }
    if (has_arg(argc, argv, "--de-no-efl-sign-priority") ||
        has_arg(argc, argv, "--adaptive-no-efl-sign-priority")) {
        adaptive_options.efl_sign_priority = false;
    }
    if (has_arg(argc, argv, "--de-cra-worsening-guard") ||
        has_arg(argc, argv, "--adaptive-cra-worsening-guard")) {
        adaptive_options.cra_worsening_guard_enabled = true;
    }
    if (has_arg(argc, argv, "--de-no-cra-worsening-guard") ||
        has_arg(argc, argv, "--adaptive-no-cra-worsening-guard")) {
        adaptive_options.cra_worsening_guard_enabled = false;
    }
    adaptive_options.cra_worsening_guard_target_deg =
        double_arg(argc, argv, "--de-cra-guard-target-deg",
            adaptive_options.cra_worsening_guard_target_deg);
    adaptive_options.cra_worsening_guard_tolerance_deg =
        double_arg(argc, argv, "--de-cra-guard-tolerance-deg",
            adaptive_options.cra_worsening_guard_tolerance_deg);
    adaptive_options.cra_worsening_guard_deadband_deg =
        double_arg(argc, argv, "--de-cra-guard-deadband-deg",
            adaptive_options.cra_worsening_guard_deadband_deg);
    if (adaptive_options.cra_worsening_guard_enabled &&
        (!std::isfinite(adaptive_options.cra_worsening_guard_target_deg) ||
         !std::isfinite(adaptive_options.cra_worsening_guard_tolerance_deg) ||
         adaptive_options.cra_worsening_guard_tolerance_deg < 0.0 ||
         !std::isfinite(adaptive_options.cra_worsening_guard_deadband_deg) ||
         adaptive_options.cra_worsening_guard_deadband_deg < 0.0)) {
        throw std::runtime_error(
            "CRA worsening guard requires finite target/deadband and non-negative "
            "tolerance/deadband");
    }
    if (has_arg(argc, argv, "--de-joint-constraint-anchor-guard") ||
        has_arg(argc, argv, "--adaptive-joint-constraint-anchor-guard")) {
        adaptive_options.joint_constraint_anchor_guard_enabled = true;
    }
    if (has_arg(argc, argv, "--de-no-joint-constraint-anchor-guard") ||
        has_arg(argc, argv, "--adaptive-no-joint-constraint-anchor-guard")) {
        adaptive_options.joint_constraint_anchor_guard_enabled = false;
    }
    adaptive_options.joint_constraint_anchor_cra_target_deg =
        double_arg(argc, argv, "--de-joint-anchor-cra-target-deg",
            adaptive_options.joint_constraint_anchor_cra_target_deg);
    adaptive_options.joint_constraint_anchor_cra_tolerance_deg =
        double_arg(argc, argv, "--de-joint-anchor-cra-tolerance-deg",
            adaptive_options.joint_constraint_anchor_cra_tolerance_deg);
    adaptive_options.joint_constraint_anchor_cra_loss_budget_deg =
        double_arg(argc, argv, "--de-joint-anchor-cra-loss-budget-deg",
            adaptive_options.joint_constraint_anchor_cra_loss_budget_deg);
    adaptive_options.joint_constraint_anchor_distortion_target =
        double_arg(argc, argv, "--de-joint-anchor-distortion-target",
            adaptive_options.joint_constraint_anchor_distortion_target);
    adaptive_options.joint_constraint_anchor_distortion_tolerance =
        double_arg(argc, argv, "--de-joint-anchor-distortion-tolerance",
            adaptive_options.joint_constraint_anchor_distortion_tolerance);
    adaptive_options.joint_constraint_anchor_distortion_loss_budget =
        double_arg(argc, argv, "--de-joint-anchor-distortion-loss-budget",
            adaptive_options.joint_constraint_anchor_distortion_loss_budget);
    if (adaptive_options.joint_constraint_anchor_guard_enabled &&
        (!std::isfinite(adaptive_options.joint_constraint_anchor_cra_target_deg) ||
         !std::isfinite(adaptive_options.joint_constraint_anchor_cra_tolerance_deg) ||
         !std::isfinite(adaptive_options.joint_constraint_anchor_cra_loss_budget_deg) ||
         !std::isfinite(adaptive_options.joint_constraint_anchor_distortion_target) ||
         !std::isfinite(adaptive_options.joint_constraint_anchor_distortion_tolerance) ||
         !std::isfinite(adaptive_options.joint_constraint_anchor_distortion_loss_budget) ||
         adaptive_options.joint_constraint_anchor_cra_target_deg < 0.0 ||
         adaptive_options.joint_constraint_anchor_cra_tolerance_deg < 0.0 ||
         adaptive_options.joint_constraint_anchor_cra_loss_budget_deg < 0.0 ||
         adaptive_options.joint_constraint_anchor_distortion_target < 0.0 ||
         adaptive_options.joint_constraint_anchor_distortion_tolerance < 0.0 ||
         adaptive_options.joint_constraint_anchor_distortion_loss_budget < 0.0)) {
        throw std::runtime_error(
            "joint constraint anchor guard requires finite non-negative "
            "targets, tolerances, and loss budgets");
    }
    adaptive_options.max_evaluations =
        int_arg(argc, argv, "--max-evaluations", adaptive_options.max_evaluations);
    adaptive_options.pbest_fraction =
        double_arg(argc, argv, "--pbest-fraction", adaptive_options.pbest_fraction);
    adaptive_options.pbest_scope = pbest_guide_scope_from_string(
        string_arg(argc, argv, "--pbest-scope",
            pbest_guide_scope_name(adaptive_options.pbest_scope)));
    adaptive_options.pbest_basin_rate = std::clamp(
        double_arg(argc, argv, "--pbest-basin-rate", adaptive_options.pbest_basin_rate),
        0.0,
        1.0);
    adaptive_options.pbest_same_key_rate = std::clamp(
        double_arg(argc, argv, "--pbest-same-key-rate", adaptive_options.pbest_same_key_rate),
        0.0,
        1.0);
    adaptive_options.pbest_global_rate = std::clamp(
        double_arg(argc, argv, "--pbest-global-rate", adaptive_options.pbest_global_rate),
        0.0,
        1.0);
    adaptive_options.pbest_global_fallback =
        !has_arg(argc, argv, "--pbest-no-global-fallback");
    if (has_arg(argc, argv, "--pbest-global-fallback")) {
        adaptive_options.pbest_global_fallback = true;
    }
    adaptive_options.pbest_basin_nearest_fallback =
        has_arg(argc, argv, "--pbest-basin-nearest-fallback");
    adaptive_options.track_population_diversity =
        has_arg(argc, argv, "--track-population-diversity") ||
        has_arg(argc, argv, "--adaptive-track-population-diversity");
    if (has_arg(argc, argv, "--no-track-population-diversity") ||
        has_arg(argc, argv, "--adaptive-no-track-population-diversity")) {
        adaptive_options.track_population_diversity = false;
    }
    adaptive_options.de_mating_scope = de_mating_scope_from_string(
        string_arg(argc, argv, "--de-mating-scope",
            de_mating_scope_name(adaptive_options.de_mating_scope)));
    adaptive_options.de_mating_local_rate = std::clamp(
        double_arg(argc, argv, "--de-mating-local-rate",
            adaptive_options.de_mating_local_rate),
        0.0,
        1.0);
    adaptive_options.de_mating_min_pool = std::max(
        4,
        int_arg(argc, argv, "--de-mating-min-pool",
            adaptive_options.de_mating_min_pool));
    adaptive_options.de_mating_strict_local =
        has_arg(argc, argv, "--de-mating-strict-local");
    if (has_arg(argc, argv, "--de-mating-no-strict-local")) {
        adaptive_options.de_mating_strict_local = false;
    }
    adaptive_options.archive_size_ratio =
        double_arg(argc, argv, "--archive-size-ratio", adaptive_options.archive_size_ratio);
    adaptive_options.mu_f = double_arg(argc, argv, "--adaptive-mu-f", adaptive_options.mu_f);
    adaptive_options.mu_cr = double_arg(argc, argv, "--adaptive-mu-cr", adaptive_options.mu_cr);
    adaptive_options.adaptation_rate =
        double_arg(argc, argv, "--adaptive-rate", adaptive_options.adaptation_rate);
    adaptive_options.f_cauchy_scale =
        double_arg(argc, argv, "--adaptive-f-cauchy-scale", adaptive_options.f_cauchy_scale);
    adaptive_options.cr_normal_sigma =
        double_arg(argc, argv, "--adaptive-cr-sigma", adaptive_options.cr_normal_sigma);
    adaptive_options.use_lpsr = !has_arg(argc, argv, "--adaptive-no-lpsr");
    adaptive_options.lpsr_horizon_generations =
        int_arg(argc, argv, "--adaptive-lpsr-horizon-generations",
            adaptive_options.lpsr_horizon_generations);
    adaptive_options.minimum_popsize_multiplier =
        int_arg(argc, argv, "--adaptive-min-popsize-multiplier",
            adaptive_options.minimum_popsize_multiplier);
    adaptive_options.minimum_population_size =
        int_arg(argc, argv, "--adaptive-min-population",
            adaptive_options.minimum_population_size);
    adaptive_options.require_trace_feasible =
        !has_arg(argc, argv, "--adaptive-allow-trace-infeasible");
    adaptive_options.allow_near_trace_infeasible =
        has_arg(argc, argv, "--adaptive-allow-near-trace-infeasible");
    if (has_arg(argc, argv, "--adaptive-no-hard-reject-structural-failures")) {
        adaptive_options.hard_reject_structural_failures = false;
    } else if (has_arg(argc, argv, "--adaptive-hard-reject-structural-failures")) {
        adaptive_options.hard_reject_structural_failures = true;
    }
    return kContinueCommand;
}

int OptimizationCommand::adaptive_stop_options() {
    adaptive_search_purpose = normalized_option_token(
                    string_arg(argc, argv, "--adaptive-search-purpose", "optimize"));
    adaptive_options.treat_first_order_as_feasibility =
        adaptive_search_purpose == "initial_search";
    if (has_arg(argc, argv, "--adaptive-hard-first-order")) {
        adaptive_options.treat_first_order_as_feasibility = true;
    }
    if (has_arg(argc, argv, "--adaptive-soft-first-order")) {
        adaptive_options.treat_first_order_as_feasibility = false;
    }
    adaptive_options.feasibility_epsilon =
        double_arg(argc, argv, "--feasibility-epsilon",
            adaptive_options.feasibility_epsilon);
    adaptive_options.trace_coverage_epsilon =
        double_arg(argc, argv, "--trace-coverage-epsilon",
            adaptive_options.trace_coverage_epsilon);
    adaptive_options.constraint_epsilon =
        double_arg(argc, argv, "--constraint-epsilon",
            adaptive_options.constraint_epsilon);
    adaptive_options.first_order_epsilon =
        double_arg(argc, argv, "--first-order-epsilon",
            adaptive_options.first_order_epsilon);
    adaptive_options.early_stop_enabled = has_arg(argc, argv, "--adaptive-early-stop");
    adaptive_options.early_stop_metric =
        string_arg(argc, argv, "--adaptive-early-stop-metric",
            adaptive_options.early_stop_metric);
    adaptive_options.early_stop_min_epochs =
        int_arg(argc, argv, "--adaptive-early-stop-min-epochs",
            adaptive_options.early_stop_min_epochs);
    adaptive_options.early_stop_window =
        int_arg(argc, argv, "--adaptive-early-stop-window",
            adaptive_options.early_stop_window);
    adaptive_options.early_stop_min_topologies =
        int_arg(argc, argv, "--adaptive-early-stop-min-topologies",
            adaptive_options.early_stop_min_topologies);
    adaptive_options.early_stop_min_rel_improve =
        double_arg(argc, argv, "--adaptive-early-stop-min-rel-improve",
            adaptive_options.early_stop_min_rel_improve);
    adaptive_options.early_stop_min_abs_improve =
        double_arg(argc, argv, "--adaptive-early-stop-min-abs-improve",
            adaptive_options.early_stop_min_abs_improve);
    adaptive_options.early_stop_include_initial_point =
        has_arg(
            argc,
            argv,
            "--adaptive-early-stop-include-initial-point");
    adaptive_options.early_stop_use_min_improvement_threshold =
        has_arg(argc, argv,
            "--adaptive-early-stop-use-min-improvement-threshold");
    adaptive_options.early_stop_min_lpsr_progress =
        double_arg(argc, argv, "--adaptive-early-stop-min-lpsr-progress",
            adaptive_options.early_stop_min_lpsr_progress);
    adaptive_options.early_stop_tail_epochs =
        int_arg(argc, argv, "--adaptive-early-stop-tail-epochs",
            adaptive_options.early_stop_tail_epochs);
    adaptive_options.early_stop_require_topology_plateau =
        has_arg(argc, argv, "--adaptive-early-stop-require-topology-plateau");
    adaptive_options.early_stop_best_merit_max =
        double_arg(argc, argv, "--adaptive-early-stop-best-merit-max",
            adaptive_options.early_stop_best_merit_max);
    adaptive_options.early_stop_use_qualified_topologies =
        has_arg(argc, argv, "--adaptive-early-stop-qualified-topologies");
    adaptive_options.early_stop_qualified_topology_mode =
        string_arg(argc, argv,
            "--adaptive-early-stop-qualified-topology-mode",
            adaptive_options.early_stop_qualified_topology_mode);
    adaptive_options.early_stop_qualified_topology_first_order_epsilon =
        double_arg(argc, argv,
            "--adaptive-early-stop-qualified-topology-epsilon",
            adaptive_options.early_stop_qualified_topology_first_order_epsilon);
    adaptive_options.early_stop_qualified_topology_merit_ratio =
        double_arg(argc, argv,
            "--adaptive-early-stop-qualified-topology-merit-ratio",
            adaptive_options.early_stop_qualified_topology_merit_ratio);
    adaptive_options.early_stop_qualified_topology_merit_floor =
        double_arg(argc, argv,
            "--adaptive-early-stop-qualified-topology-merit-floor",
            adaptive_options.early_stop_qualified_topology_merit_floor);
    adaptive_options.early_stop_qualified_topology_merit_delta =
        double_arg(argc, argv,
            "--adaptive-early-stop-qualified-topology-merit-delta",
            adaptive_options.early_stop_qualified_topology_merit_delta);
    adaptive_options.early_stop_qualified_topology_merit_max =
        double_arg(argc, argv,
            "--adaptive-early-stop-qualified-topology-merit-max",
            adaptive_options.early_stop_qualified_topology_merit_max);
    return kContinueCommand;
}

} } }
