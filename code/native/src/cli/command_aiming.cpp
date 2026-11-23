// Apply calibrated and three-step aiming CLI overrides to the loaded optical system.
// Command preparation runs these settings before aperture preparation and initial evaluation.
// Preserve mode-specific defaults, aliases, and the relationship between solver budgets and caches.

#include "command_context.hpp"

namespace lensde { namespace cli { namespace detail {

int CommandContext::configure_calibrated_aiming() {
    lens.ray_aiming.mode = aiming_arg(argc, argv, lens.ray_aiming.mode);
    lens.ray_aiming.stop_radius_source =
        aiming_stop_radius_source_arg(argc, argv, lens.ray_aiming.stop_radius_source);
    lens.ray_aiming.max_iter = int_arg(argc, argv, "--aiming-iter", lens.ray_aiming.max_iter);
    lens.ray_aiming.tolerance = double_arg(argc, argv, "--aiming-tolerance", lens.ray_aiming.tolerance);
    lens.ray_aiming.continuation_steps = std::max(
        1,
        int_arg(argc, argv, "--aiming-continuation-steps", lens.ray_aiming.continuation_steps));
    lens.ray_aiming.calibrated_paraxial.correction_iterations = std::max(
        0,
        int_arg(argc, argv, "--aiming-calibrated-correction-iterations",
                lens.ray_aiming.calibrated_paraxial.correction_iterations));
    std::string calibrated_sampling_mode =
        string_arg(argc, argv, "--aiming-calibrated-sampling-mode");
    if (calibrated_sampling_mode.empty()) {
        calibrated_sampling_mode =
            string_arg(argc, argv, "--aiming-calibrated-mapping-mode");
    }
    if (calibrated_sampling_mode.empty()) {
        calibrated_sampling_mode =
            string_arg(argc, argv, "--aiming-calibrated-pupil-mapping");
    }
    if (!calibrated_sampling_mode.empty()) {
        std::transform(
            calibrated_sampling_mode.begin(),
            calibrated_sampling_mode.end(),
            calibrated_sampling_mode.begin(),
            [](unsigned char c) {
                return c == '-' || c == ' ' ? '_' : static_cast<char>(std::tolower(c));
            });
        lens.ray_aiming.calibrated_paraxial.sampling_mode = calibrated_sampling_mode;
    }
    lens.ray_aiming.calibrated_paraxial.pilot_grid_n = std::max(
        3,
        int_arg(argc, argv, "--aiming-calibrated-pilot-grid",
                lens.ray_aiming.calibrated_paraxial.pilot_grid_n));
    lens.ray_aiming.calibrated_paraxial.jacobian_probe_fraction =
        double_arg(argc, argv, "--aiming-calibrated-probe-fraction",
                   lens.ray_aiming.calibrated_paraxial.jacobian_probe_fraction);
    lens.ray_aiming.calibrated_paraxial.validation_residual_stop_fraction =
        double_arg(argc, argv, "--aiming-calibrated-validation-stop-fraction",
                   lens.ray_aiming.calibrated_paraxial.validation_residual_stop_fraction);
    lens.ray_aiming.calibrated_paraxial.fit_rms_tolerance_stop_fraction =
        double_arg(argc, argv, "--aiming-calibrated-fit-stop-fraction",
                   lens.ray_aiming.calibrated_paraxial.fit_rms_tolerance_stop_fraction);
    if (has_arg(argc, argv, "--aiming-calibrated-edge-calibration")) {
        lens.ray_aiming.calibrated_paraxial.edge_calibration_enabled = true;
    }
    if (has_arg(argc, argv, "--aiming-calibrated-no-edge-calibration") ||
        has_arg(argc, argv, "--aiming-calibrated-disable-edge-calibration")) {
        lens.ray_aiming.calibrated_paraxial.edge_calibration_enabled = false;
    }
    lens.ray_aiming.calibrated_paraxial.edge_calibration_iterations = std::max(
        0,
        int_arg(argc, argv, "--aiming-calibrated-edge-calibration-iterations",
                lens.ray_aiming.calibrated_paraxial.edge_calibration_iterations));
    std::string edge_solver =
        string_arg(argc, argv, "--aiming-calibrated-edge-calibration-solver");
    if (edge_solver.empty()) {
        edge_solver = string_arg(argc, argv, "--aiming-calibrated-edge-solver");
    }
    if (!edge_solver.empty()) {
        std::transform(edge_solver.begin(), edge_solver.end(), edge_solver.begin(), [](unsigned char c) {
            return c == '-' || c == ' ' ? '_' : static_cast<char>(std::tolower(c));
        });
        lens.ray_aiming.calibrated_paraxial.edge_calibration_solver = edge_solver;
    }
    lens.ray_aiming.calibrated_paraxial.edge_calibration_weight_power =
        double_arg(argc, argv, "--aiming-calibrated-edge-calibration-weight-power",
                   lens.ray_aiming.calibrated_paraxial.edge_calibration_weight_power);
    lens.ray_aiming.calibrated_paraxial.edge_calibration_residual_stop_fraction =
        double_arg(argc, argv, "--aiming-calibrated-edge-calibration-residual-stop-fraction",
                   lens.ray_aiming.calibrated_paraxial.edge_calibration_residual_stop_fraction);
    lens.ray_aiming.calibrated_paraxial.edge_calibration_residual_stop_fraction =
        double_arg(argc, argv, "--aiming-calibrated-edge-residual-stop-fraction",
                   lens.ray_aiming.calibrated_paraxial.edge_calibration_residual_stop_fraction);
    std::string chief_mode =
        string_arg(argc, argv, "--aiming-calibrated-chief-mode");
    if (!chief_mode.empty()) {
        std::transform(chief_mode.begin(), chief_mode.end(), chief_mode.begin(), [](unsigned char c) {
            return c == '-' || c == ' ' ? '_' : static_cast<char>(std::tolower(c));
        });
        lens.ray_aiming.calibrated_paraxial.chief_anchor_mode = chief_mode;
    }
    lens.ray_aiming.calibrated_paraxial.budget_chief_iterations = std::max(
        0,
        int_arg(argc, argv, "--aiming-calibrated-budget-chief-iter",
                lens.ray_aiming.calibrated_paraxial.budget_chief_iterations));
    std::string budget_solver =
        string_arg(argc, argv, "--aiming-calibrated-budget-chief-solver");
    if (budget_solver.empty()) {
        budget_solver = string_arg(argc, argv, "--aiming-calibrated-chief-solver");
    }
    if (!budget_solver.empty()) {
        std::transform(
            budget_solver.begin(),
            budget_solver.end(),
            budget_solver.begin(),
            [](unsigned char c) {
                return c == '-' || c == ' ' ? '_' : static_cast<char>(std::tolower(c));
            });
        lens.ray_aiming.calibrated_paraxial.budget_chief_solver = budget_solver;
    }
    std::string budget_acceptance =
        string_arg(argc, argv, "--aiming-calibrated-budget-chief-acceptance");
    if (!budget_acceptance.empty()) {
        std::transform(
            budget_acceptance.begin(),
            budget_acceptance.end(),
            budget_acceptance.begin(),
            [](unsigned char c) {
                return c == '-' || c == ' ' ? '_' : static_cast<char>(std::tolower(c));
            });
        lens.ray_aiming.calibrated_paraxial.budget_chief_acceptance = budget_acceptance;
    }
    lens.ray_aiming.calibrated_paraxial.budget_chief_accept_stop_fraction =
        double_arg(argc, argv, "--aiming-calibrated-budget-chief-stop-fraction",
                   lens.ray_aiming.calibrated_paraxial.budget_chief_accept_stop_fraction);
    lens.ray_aiming.calibrated_paraxial.budget_chief_residual_stop_fraction =
        double_arg(argc, argv, "--aiming-calibrated-budget-chief-residual-stop-fraction",
                   lens.ray_aiming.calibrated_paraxial.budget_chief_residual_stop_fraction);
    lens.ray_aiming.calibrated_paraxial.budget_chief_residual_stop_fraction =
        double_arg(argc, argv, "--aiming-calibrated-chief-residual-stop-fraction",
                   lens.ray_aiming.calibrated_paraxial.budget_chief_residual_stop_fraction);
    lens.ray_aiming.calibrated_paraxial.budget_chief_min_a =
        double_arg(argc, argv, "--aiming-calibrated-budget-chief-min-a",
                   lens.ray_aiming.calibrated_paraxial.budget_chief_min_a);
    lens.ray_aiming.calibrated_paraxial.budget_chief_field_continuation_angle_deg =
        double_arg(argc, argv, "--aiming-calibrated-budget-chief-field-angle",
                   lens.ray_aiming.calibrated_paraxial.budget_chief_field_continuation_angle_deg);
    lens.ray_aiming.calibrated_paraxial.budget_chief_skip_abcd_angle_deg =
        double_arg(argc, argv, "--aiming-calibrated-budget-chief-skip-abcd-angle",
                   lens.ray_aiming.calibrated_paraxial.budget_chief_skip_abcd_angle_deg);
    if (has_arg(argc, argv, "--aiming-calibrated-budget-chief-ignore-aperture-clip")) {
        lens.ray_aiming.calibrated_paraxial.budget_chief_ignore_aperture_clip = true;
    }
    if (has_arg(argc, argv, "--aiming-calibrated-budget-chief-respect-aperture-clip") ||
        has_arg(argc, argv, "--aiming-calibrated-budget-chief-no-ignore-aperture-clip")) {
        lens.ray_aiming.calibrated_paraxial.budget_chief_ignore_aperture_clip = false;
    }
    std::string fallback_aiming =
        string_arg(argc, argv, "--aiming-calibrated-fallback");
    if (!fallback_aiming.empty()) {
        std::transform(fallback_aiming.begin(), fallback_aiming.end(), fallback_aiming.begin(), [](unsigned char c) {
            return c == '-' || c == ' ' ? '_' : static_cast<char>(std::tolower(c));
        });
        lens.ray_aiming.calibrated_paraxial.fallback = fallback_aiming;
    }
    std::string raw_aiming_mode = string_arg(argc, argv, "--aiming");
    std::transform(raw_aiming_mode.begin(), raw_aiming_mode.end(), raw_aiming_mode.begin(), [](unsigned char c) {
        return c == '-' || c == ' ' ? '_' : static_cast<char>(std::tolower(c));
    });
    if (raw_aiming_mode == "calibrated_paraxial_corrected" &&
        lens.ray_aiming.calibrated_paraxial.correction_iterations <= 0) {
        lens.ray_aiming.calibrated_paraxial.correction_iterations = 2;
    }
    return kContinueCommand;
}

int CommandContext::configure_three_step_aiming() {
    lens.ray_aiming.three_step.chief_reverse_trial_rays = std::max(
        2,
        int_arg(argc, argv, "--aiming-three-step-chief-rays",
                int_arg(argc, argv, "--aiming-three-step-reverse-trial-rays",
                        lens.ray_aiming.three_step.chief_reverse_trial_rays)));
    std::string three_step_sampling_mode =
        string_arg(argc, argv, "--aiming-three-step-sampling-mode");
    if (three_step_sampling_mode.empty()) {
        three_step_sampling_mode =
            string_arg(argc, argv, "--aiming-three-step-mapping-mode");
    }
    if (three_step_sampling_mode.empty()) {
        three_step_sampling_mode =
            string_arg(argc, argv, "--aiming-three-step-pupil-mapping");
    }
    if (!three_step_sampling_mode.empty()) {
        std::transform(
            three_step_sampling_mode.begin(),
            three_step_sampling_mode.end(),
            three_step_sampling_mode.begin(),
            [](unsigned char c) {
                return c == '-' || c == ' ' ? '_' : static_cast<char>(std::tolower(c));
            });
        lens.ray_aiming.three_step.sampling_mode = three_step_sampling_mode;
    }
    lens.ray_aiming.three_step.chief_reverse_max_angle_deg =
        double_arg(argc, argv, "--aiming-three-step-max-angle",
                   lens.ray_aiming.three_step.chief_reverse_max_angle_deg);
    lens.ray_aiming.three_step.edge_passes = std::max(
        1,
        int_arg(argc, argv, "--aiming-three-step-edge-passes",
                lens.ray_aiming.three_step.edge_passes));
    lens.ray_aiming.three_step.meridional_iterations = std::max(
        0,
        int_arg(argc, argv, "--aiming-three-step-meridional-iter",
                lens.ray_aiming.three_step.meridional_iterations));
    lens.ray_aiming.three_step.sagittal_iterations = std::max(
        0,
        int_arg(argc, argv, "--aiming-three-step-sagittal-iter",
                lens.ray_aiming.three_step.sagittal_iterations));
    std::string three_step_edge_solver =
        string_arg(argc, argv, "--aiming-three-step-edge-solver");
    if (!three_step_edge_solver.empty()) {
        std::transform(
            three_step_edge_solver.begin(),
            three_step_edge_solver.end(),
            three_step_edge_solver.begin(),
            [](unsigned char c) {
                return c == '-' || c == ' ' ? '_' : static_cast<char>(std::tolower(c));
            });
        lens.ray_aiming.three_step.edge_solver = three_step_edge_solver;
    }
    lens.ray_aiming.three_step.edge_residual_stop_fraction =
        double_arg(argc, argv, "--aiming-three-step-edge-residual-stop-fraction",
                   lens.ray_aiming.three_step.edge_residual_stop_fraction);
    lens.ray_aiming.three_step.edge_initial_radius_fraction =
        double_arg(argc, argv, "--aiming-three-step-edge-radius-fraction",
                   lens.ray_aiming.three_step.edge_initial_radius_fraction);
    lens.ray_aiming.three_step.accept_stop_fraction =
        double_arg(argc, argv, "--aiming-three-step-accept-stop-fraction",
                   lens.ray_aiming.three_step.accept_stop_fraction);
    lens.ray_aiming.three_step.correction_iterations = std::max(
        0,
        int_arg(argc, argv, "--aiming-three-step-correction-iterations",
                lens.ray_aiming.three_step.correction_iterations));
    if (has_arg(argc, argv, "--aiming-three-step-debug-stop-residual") ||
        has_arg(argc, argv, "--aiming-three-step-sample-stop-residual") ||
        has_arg(argc, argv, "--aiming-three-step-debug-residual")) {
        lens.ray_aiming.three_step.sample_stop_residual_debug = true;
    }
    if (has_arg(argc, argv, "--aiming-three-step-no-debug-stop-residual") ||
        has_arg(argc, argv, "--aiming-three-step-no-sample-stop-residual") ||
        has_arg(argc, argv, "--aiming-three-step-no-debug-residual")) {
        lens.ray_aiming.three_step.sample_stop_residual_debug = false;
    }
    std::string three_step_fallback =
        string_arg(argc, argv, "--aiming-three-step-fallback");
    if (!three_step_fallback.empty()) {
        std::transform(
            three_step_fallback.begin(),
            three_step_fallback.end(),
            three_step_fallback.begin(),
            [](unsigned char c) {
                return c == '-' || c == ' ' ? '_' : static_cast<char>(std::tolower(c));
            });
        lens.ray_aiming.three_step.fallback = three_step_fallback;
    }
    lens.ray_aiming.adaptive_pilot_grid.fine_grid_n = std::max(
        3,
        int_arg(argc, argv, "--aiming-pilot-fine-grid",
                lens.ray_aiming.adaptive_pilot_grid.fine_grid_n));
    lens.ray_aiming.adaptive_pilot_grid.coarse_grid_n = std::max(
        3,
        int_arg(argc, argv, "--aiming-pilot-coarse-grid",
                lens.ray_aiming.adaptive_pilot_grid.coarse_grid_n));
    lens.ray_aiming.adaptive_pilot_grid.nearest_k = std::max(
        1,
        int_arg(argc, argv, "--aiming-pilot-nearest-k",
                lens.ray_aiming.adaptive_pilot_grid.nearest_k));
    lens.ray_aiming.adaptive_pilot_grid.iterative_failure_sample_limit = std::max(
        0,
        int_arg(argc, argv, "--aiming-pilot-iterative-failure-limit",
                lens.ray_aiming.adaptive_pilot_grid.iterative_failure_sample_limit));
    if (has_arg(argc, argv, "--aiming-pilot-no-per-ray-iterative")) {
        lens.ray_aiming.adaptive_pilot_grid.try_iterative_for_each_ray = false;
    }
    if (has_arg(argc, argv, "--aiming-pilot-allow-interpolation")) {
        lens.ray_aiming.adaptive_pilot_grid.allow_barycentric_interpolation = true;
    }
    if (has_arg(argc, argv, "--aiming-require-convergence")) {
        lens.ray_aiming.require_convergence = true;
    }
    if (has_arg(argc, argv, "--aiming-best-effort")) {
        lens.ray_aiming.require_convergence = false;
    }
    if (lens.ray_aiming.mode == RayAimingMode::AdaptivePilotGrid &&
        !has_arg(argc, argv, "--aiming-best-effort")) {
        lens.ray_aiming.require_convergence = true;
        lens.ray_aiming.adaptive_pilot_grid.require_final_convergence = true;
    }
    return kContinueCommand;
}

} } }
