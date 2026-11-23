// Configure adaptive material tolerance, Refresh generation, and local-polish scheduling.
// OptimizationCommand combines these settings with the common DE and basin policies.
// Retain target defaults, seed-pool loading, and polish-disable precedence before the native call.

#include "optimization_command.hpp"

namespace lensde { namespace cli { namespace detail {

int OptimizationCommand::adaptive_material_options() {
    adaptive_options.adaptive_glass_tolerance_enabled =
        has_arg(argc, argv, "--adaptive-glass-tolerance");
    if (has_arg(argc, argv, "--adaptive-no-glass-tolerance")) {
        adaptive_options.adaptive_glass_tolerance_enabled = false;
    }
    adaptive_options.adaptive_glass_tolerance_warmup_epochs = std::max(
        0,
        int_arg(argc, argv,
            "--adaptive-glass-tolerance-warmup-epochs",
            adaptive_options.adaptive_glass_tolerance_warmup_epochs));
    adaptive_options.adaptive_glass_tolerance_trigger_mode =
        string_arg(argc, argv,
            "--adaptive-glass-tolerance-trigger-mode",
            adaptive_options.adaptive_glass_tolerance_trigger_mode);
    adaptive_options.adaptive_glass_tolerance_focus_clear_target = std::max(
        0,
        int_arg(argc, argv,
            "--adaptive-glass-tolerance-focus-clear-target",
            adaptive_options.adaptive_glass_tolerance_focus_clear_target));
    if (has_arg(argc, argv, "--adaptive-glass-tolerance-stop-when-ready")) {
        adaptive_options.adaptive_glass_tolerance_stop_when_ready = true;
    }
    if (has_arg(argc, argv, "--adaptive-glass-tolerance-no-stop-when-ready")) {
        adaptive_options.adaptive_glass_tolerance_stop_when_ready = false;
    }
    if (has_arg(argc, argv, "--adaptive-glass-tolerance-bridge-freeze-focus")) {
        adaptive_options.adaptive_glass_tolerance_bridge_freeze_focus = true;
    }
    if (has_arg(argc, argv, "--adaptive-glass-tolerance-no-bridge-freeze-focus")) {
        adaptive_options.adaptive_glass_tolerance_bridge_freeze_focus = false;
    }
    adaptive_options.adaptive_glass_tolerance_elite_fraction = std::clamp(
        double_arg(argc, argv,
            "--adaptive-glass-tolerance-elite-fraction",
            adaptive_options.adaptive_glass_tolerance_elite_fraction),
        1.0e-6,
        1.0);
    adaptive_options.adaptive_glass_tolerance_quantile = std::clamp(
        double_arg(argc, argv,
            "--adaptive-glass-tolerance-quantile",
            adaptive_options.adaptive_glass_tolerance_quantile),
        0.0,
        1.0);
    adaptive_options.adaptive_glass_tolerance_margin = std::max(
        0.0,
        double_arg(argc, argv,
            "--adaptive-glass-tolerance-margin",
            adaptive_options.adaptive_glass_tolerance_margin));
    adaptive_options.adaptive_glass_tolerance_floor = std::max(
        0.0,
        double_arg(argc, argv,
            "--adaptive-glass-tolerance-floor",
            adaptive_options.adaptive_glass_tolerance_floor));
    adaptive_options.adaptive_glass_tolerance_smoothing = std::clamp(
        double_arg(argc, argv,
            "--adaptive-glass-tolerance-smoothing",
            adaptive_options.adaptive_glass_tolerance_smoothing),
        0.0,
        1.0);
    adaptive_options.adaptive_glass_tolerance_decay = std::clamp(
        double_arg(argc, argv,
            "--adaptive-glass-tolerance-decay",
            adaptive_options.adaptive_glass_tolerance_decay),
        0.0,
        1.0);
    adaptive_options.adaptive_glass_tolerance_early_stop_max = std::max(
        0.0,
        double_arg(argc, argv,
            "--adaptive-glass-tolerance-early-stop-max",
            adaptive_options.adaptive_glass_tolerance_early_stop_max));
    return kContinueCommand;
}

int OptimizationCommand::adaptive_refresh_options() {
    adaptive_options.rollback = !has_arg(argc, argv, "--hybrid-no-rollback");
    adaptive_options.polish_accept_merit_only =
        has_arg(argc, argv, "--adaptive-polish-accept-merit-only");
    adaptive_options.micro_island_enabled =
        has_arg(argc, argv, "--adaptive-micro-island");
    adaptive_options.micro_island_replace_polish =
        !has_arg(argc, argv, "--adaptive-micro-island-keep-polish");
    if (has_arg(argc, argv, "--adaptive-micro-island-replace-polish")) {
        adaptive_options.micro_island_replace_polish = true;
    }
    adaptive_options.micro_island_count = std::max(
        0,
        int_arg(argc, argv, "--adaptive-micro-island-count",
            adaptive_options.micro_island_count));
    adaptive_options.micro_island_popsize = std::max(
        0.0,
        double_arg(argc, argv, "--adaptive-micro-island-popsize",
            adaptive_options.micro_island_popsize));
    adaptive_options.micro_island_population = std::max(
        8,
        int_arg(argc, argv, "--adaptive-micro-island-population",
            adaptive_options.micro_island_population));
    adaptive_options.micro_island_outer_epochs = std::max(
        1,
        int_arg(argc, argv, "--adaptive-micro-island-outer",
            adaptive_options.micro_island_outer_epochs));
    adaptive_options.micro_island_de_generations = std::max(
        1,
        int_arg(argc, argv, "--adaptive-micro-island-de-gen",
            adaptive_options.micro_island_de_generations));
    adaptive_options.micro_island_seed_scale = std::max(
        0.0,
        double_arg(argc, argv, "--adaptive-micro-island-seed-scale",
            adaptive_options.micro_island_seed_scale));
    adaptive_options.micro_island_polish_candidate_count = std::max(
        -1,
        int_arg(argc, argv, "--adaptive-micro-island-polish-candidates",
            adaptive_options.micro_island_polish_candidate_count));
    adaptive_options.micro_island_polish_refill_max_extra = std::max(
        -1,
        int_arg(argc, argv, "--adaptive-micro-island-polish-refill-max-extra",
            adaptive_options.micro_island_polish_refill_max_extra));
    adaptive_options.micro_island_survivor_count = std::max(
        1,
        int_arg(argc, argv, "--adaptive-micro-island-survivors",
            adaptive_options.micro_island_survivor_count));
    adaptive_options.micro_island_family_seed_limit = std::max(
        0,
        int_arg(argc, argv, "--adaptive-micro-island-family-seed-limit",
            adaptive_options.micro_island_family_seed_limit));
    adaptive_options.micro_island_refresh_archive_seed_limit = std::max(
        0,
        int_arg(argc, argv, "--adaptive-micro-island-refresh-archive-seed-limit",
            adaptive_options.micro_island_refresh_archive_seed_limit));
    adaptive_options.micro_island_early_stop_enabled =
        has_arg(argc, argv, "--adaptive-micro-island-early-stop");
    if (has_arg(argc, argv, "--adaptive-micro-island-retire-on-early-stop")) {
        adaptive_options.micro_island_retire_on_early_stop = true;
    }
    if (has_arg(argc, argv, "--adaptive-micro-island-no-retire-on-early-stop")) {
        adaptive_options.micro_island_retire_on_early_stop = false;
    }
    adaptive_options.micro_island_early_stop_min_epochs = std::max(
        1,
        int_arg(argc, argv, "--adaptive-micro-island-early-stop-min-epochs",
            adaptive_options.micro_island_early_stop_min_epochs));
    adaptive_options.micro_island_early_stop_window = std::max(
        1,
        int_arg(argc, argv, "--adaptive-micro-island-early-stop-window",
            adaptive_options.micro_island_early_stop_window));
    adaptive_options.micro_island_early_stop_min_rel_improve = std::max(
        0.0,
        double_arg(argc, argv, "--adaptive-micro-island-early-stop-min-rel-improve",
            adaptive_options.micro_island_early_stop_min_rel_improve));
    adaptive_options.micro_island_early_stop_min_abs_improve = std::max(
        0.0,
        double_arg(argc, argv, "--adaptive-micro-island-early-stop-min-abs-improve",
            adaptive_options.micro_island_early_stop_min_abs_improve));
    adaptive_options.inject_best = !has_arg(argc, argv, "--hybrid-no-inject-best");
    adaptive_options.refresh_fraction =
        double_arg(argc, argv, "--hybrid-refresh", adaptive_options.refresh_fraction);
    adaptive_options.refresh_warmup_generations = std::max(
        0,
        int_arg(argc, argv, "--refresh-warmup-generations",
            adaptive_options.refresh_warmup_generations));
    apply_refresh_generation_args(argc, argv, adaptive_options.refresh_generation);
    adaptive_options.refresh_plateau_pulse_once =
        has_arg(argc, argv, "--refresh-plateau-pulse-once");
    apply_population_generation_target_defaults(
        adaptive_options.refresh_generation,
        merit,
        lens);
    if (!load_refresh_seed_pool(
            adaptive_options.refresh_generation,
            static_cast<int>(variables.size()),
            std::cerr)) {
        return 7;
    }
    return kContinueCommand;
}

int OptimizationCommand::adaptive_polish_options() {
    adaptive_options.parallel_polish = !has_arg(argc, argv, "--hybrid-serial-polish");
    adaptive_options.polish_enabled =
        !has_arg(argc, argv, "--no-polish") &&
        !has_arg(argc, argv, "--disable-polish") &&
        !has_arg(argc, argv, "--hybrid-no-polish");
    adaptive_options.batched_polish =
        has_arg(argc, argv, "--batched-polish") ||
        has_arg(argc, argv, "--batch-polish") ||
        has_arg(argc, argv, "--micro-dls-batch-polish");
    adaptive_options.polish_workers =
        std::max(0, int_arg(argc, argv, "--hybrid-polish-workers",
            adaptive_options.polish_workers));
    if (!adaptive_options.micro_island_enabled) {
        adaptive_options.micro_island_replace_polish = false;
    }
    adaptive_options.internal_scaling = internal_scaling;
    adaptive_options.polish.max_iterations =
        int_arg(argc, argv, "--hybrid-local-iter",
            int_arg(argc, argv, "--local-iter", 12));
    adaptive_options.polish.max_rejected_steps =
        int_arg(argc, argv, "--local-max-reject",
            adaptive_options.polish.max_rejected_steps);
    adaptive_options.polish.initial_lambda =
        double_arg(argc, argv, "--local-lambda", adaptive_options.polish.initial_lambda);
    adaptive_options.polish.finite_difference =
        double_arg(argc, argv, "--local-fd", adaptive_options.polish.finite_difference);
    adaptive_options.polish.min_step =
        double_arg(argc, argv, "--local-min-step", adaptive_options.polish.min_step);
    adaptive_options.polish.tolerance =
        double_arg(argc, argv, "--local-tol", adaptive_options.polish.tolerance);
    adaptive_options.polish.plateau_window = int_arg(
        argc, argv, "--local-plateau-window", adaptive_options.polish.plateau_window);
    adaptive_options.polish.plateau_min_iterations = int_arg(
        argc, argv, "--local-plateau-min-iter", adaptive_options.polish.plateau_min_iterations);
    adaptive_options.polish.plateau_min_abs_improvement = double_arg(
        argc, argv, "--local-plateau-min-abs-improve", adaptive_options.polish.plateau_min_abs_improvement);
    adaptive_options.polish.plateau_min_rel_improvement = double_arg(
        argc, argv, "--local-plateau-min-rel-improve", adaptive_options.polish.plateau_min_rel_improvement);
    adaptive_options.polish.damping_diagonal_floor =
        double_arg(argc, argv, "--local-damping-floor",
            adaptive_options.polish.damping_diagonal_floor);
    adaptive_options.polish.step_search_levels =
        int_arg(argc, argv, "--local-step-search",
            adaptive_options.polish.step_search_levels);
    adaptive_options.polish.step_search_shrink =
        double_arg(argc, argv, "--local-step-shrink",
            adaptive_options.polish.step_search_shrink);
    adaptive_options.polish.column_equilibration =
        has_arg(argc, argv, "--local-column-equilibration") ||
        has_arg(argc, argv, "--local-column-scale");
    adaptive_options.polish.active_set_bounds =
        has_arg(argc, argv, "--local-active-set") ||
        has_arg(argc, argv, "--local-active-bounds");
    adaptive_options.polish.projected_active_set_bounds =
        has_arg(argc, argv, "--local-projected-active-set") ||
        has_arg(argc, argv, "--local-project-active-set");
    adaptive_options.polish.active_set_bound_tolerance =
        double_arg(argc, argv, "--local-active-set-tol",
            adaptive_options.polish.active_set_bound_tolerance);
    adaptive_options.polish.qr_linear_solver =
        has_arg(argc, argv, "--local-qr-solver") ||
        normalized_option_token(
            string_arg(argc, argv, "--local-linear-solver", "")) == "qr";
    adaptive_options.polish.trf_initial_radius =
        double_arg(argc, argv, "--local-trust-radius",
            adaptive_options.polish.trf_initial_radius);
    adaptive_options.polish.trf_min_radius =
        double_arg(argc, argv, "--local-trust-min-radius",
            adaptive_options.polish.trf_min_radius);
    adaptive_options.polish.trf_max_radius =
        double_arg(argc, argv, "--local-trust-max-radius",
            adaptive_options.polish.trf_max_radius);
    adaptive_options.polish.trf_acceptance_ratio =
        double_arg(argc, argv, "--local-trust-eta",
            adaptive_options.polish.trf_acceptance_ratio);
    adaptive_options.polish.method =
        string_arg(argc, argv, "--local-method", adaptive_options.polish.method);
    adaptive_options.polish.residual_mode =
        string_arg(argc, argv, "--local-residual-mode",
            adaptive_options.polish.residual_mode);
    adaptive_options.polish.use_cuda = allow_cuda_for_merit;
    adaptive_options.polish.finite_difference_diagnostics =
        has_arg(argc, argv, "--local-fd-diagnostics") ||
        has_arg(argc, argv, "--dls-fd-diagnostics");
    adaptive_options.polish.freeze_vignetting_variables =
        has_arg(argc, argv, "--local-freeze-vignetting") ||
        (std::isfinite(merit.minimum_relative_illumination) &&
         merit.minimum_relative_illumination > 0.0);
    adaptive_options.polish.internal_scaling = internal_scaling;
    adaptive_options.polish.trace_acceptance_enabled =
        adaptive_options.require_trace_feasible ||
        adaptive_options.allow_near_trace_infeasible;
    adaptive_options.polish.require_trace_feasible =
        adaptive_options.require_trace_feasible;
    adaptive_options.polish.allow_near_trace_infeasible =
        adaptive_options.allow_near_trace_infeasible;
    adaptive_options.polish.trace_coverage_epsilon =
        adaptive_options.trace_coverage_epsilon;
    apply_local_auto_tune(adaptive_options.polish, argc, argv);
    return kContinueCommand;
}

} } }
