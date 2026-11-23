// Load saved merit settings and apply the native command-line evaluation overrides.
// Sampling, target, constraint, and weighting settings are fixed before initial evaluation.
// Preserve override precedence; changing these fields changes candidate fitness and acceptance.

#include "command_context.hpp"

namespace lensde { namespace cli { namespace detail {

int CommandContext::load_merit() {
    merit = make_demo_merit();
    if (!candidate_path.empty() || !system_path.empty()) {
        merit.target_efl = 0.0;
        merit.target_ttl = 0.0;
        merit.target_bfl = 0.0;
        merit.spot_target = 0.0;
        merit.weight_spot = 1.0;
    }
    merit.num_rays = int_arg(argc, argv, "--rays", merit.num_rays);
    merit.minimum_relative_illumination = double_arg(
        argc,
        argv,
        "--minimum-relative-illumination",
        merit.minimum_relative_illumination);
    if (std::isfinite(merit.minimum_relative_illumination) &&
        merit.minimum_relative_illumination <= 0.0) {
        merit.minimum_relative_illumination =
            std::numeric_limits<double>::quiet_NaN();
    } else if (std::isfinite(merit.minimum_relative_illumination) &&
               merit.minimum_relative_illumination > 1.0) {
        std::cerr << "--minimum-relative-illumination must not exceed 1\n";
        return 4;
    }
    merit.pupil_arms = int_arg(argc, argv, "--arms",
        int_arg(argc, argv, "--pupil-arms", merit.pupil_arms));
    merit.pupil_distribution = string_arg(argc, argv, "--distribution", merit.pupil_distribution);
    merit.codev_sampling_profile = string_arg(
        argc, argv, "--codev-sampling-profile", merit.codev_sampling_profile);
    merit.codev_ray_interval = std::clamp(
        double_arg(argc, argv, "--codev-ray-interval", merit.codev_ray_interval), 0.0, 1.0);
    merit.codev_obscuration = std::clamp(
        double_arg(argc, argv, "--codev-obscuration", merit.codev_obscuration), 0.0, 1.0);
    merit.codev_spot_aperture_weight = double_arg(
        argc, argv, "--codev-spot-aperture-weight", merit.codev_spot_aperture_weight);
    merit.codev_wavefront_aperture_weight = double_arg(
        argc, argv, "--codev-wavefront-aperture-weight", merit.codev_wavefront_aperture_weight);
    merit.image_field_weight_mode = string_arg(
        argc, argv, "--image-field-weight-mode", merit.image_field_weight_mode);
    merit.image_field_edge_weight = std::max(
        1.0,
        double_arg(
            argc, argv, "--image-field-edge-weight", merit.image_field_edge_weight));
    merit.image_field_weight_exponent = std::max(
        0.1,
        double_arg(
            argc, argv, "--image-field-weight-exponent", merit.image_field_weight_exponent));
    if (has_arg(argc, argv, "--codev-symmetry") ||
        has_arg(argc, argv, "--pupil-symmetry")) {
        merit.codev_use_symmetry = true;
    }
    if (has_arg(argc, argv, "--no-codev-symmetry") ||
        has_arg(argc, argv, "--no-pupil-symmetry")) {
        merit.codev_use_symmetry = false;
    }
    merit.wavefront_reference =
        string_arg(argc, argv, "--wavefront-reference", merit.wavefront_reference);
    merit.sampling_seed = static_cast<unsigned int>(
        std::max(0, int_arg(argc, argv, "--sampling-seed", static_cast<int>(merit.sampling_seed))));
    merit.dynamic_apertures = update_clear_apertures;
    if (has_arg(
            argc,
            argv,
            "--vignetting-merit-aperture-envelope-experimental")) {
        merit.vignetting_merit_aperture_envelope_experimental = true;
    }
    if (has_arg(
            argc,
            argv,
            "--no-vignetting-merit-aperture-envelope-experimental")) {
        merit.vignetting_merit_aperture_envelope_experimental = false;
    }
    if (clear_aperture_rings_explicit) {
        merit.clear_aperture_rings = clear_aperture_rings;
    }
    merit.trace_policy = trace_policy_arg(argc, argv, merit.trace_policy);
    merit.min_field_coverage = double_arg(argc, argv, "--min-field-coverage", merit.min_field_coverage);
    merit.min_system_coverage = double_arg(argc, argv, "--min-system-coverage", merit.min_system_coverage);
    merit.min_coverage_for_image_merit =
        double_arg(argc, argv, "--min-image-coverage", merit.min_coverage_for_image_merit);
    merit.trace_failure_weight =
        double_arg(argc, argv, "--trace-failure-weight", merit.trace_failure_weight);
    merit.coverage_failure_weight =
        double_arg(argc, argv, "--coverage-failure-weight", merit.coverage_failure_weight);
    merit.aim_failure_weight =
        double_arg(argc, argv, "--aim-failure-weight", merit.aim_failure_weight);
    merit.tir_failure_weight =
        double_arg(argc, argv, "--tir-failure-weight", merit.tir_failure_weight);
    merit.aperture_failure_weight =
        double_arg(argc, argv, "--aperture-failure-weight", merit.aperture_failure_weight);
    merit.surface_miss_weight =
        double_arg(argc, argv, "--surface-miss-weight", merit.surface_miss_weight);
    merit.backward_failure_weight =
        double_arg(argc, argv, "--backward-failure-weight", merit.backward_failure_weight);
    if (has_arg(argc, argv, "--require-chief-ray")) {
        merit.require_chief_ray = true;
    }
    if (has_arg(argc, argv, "--allow-chief-ray-failure")) {
        merit.require_chief_ray = false;
    }
    if (has_arg(argc, argv, "--require-off-axis-field-coverage")) {
        merit.require_off_axis_field_coverage = true;
    }
    if (has_arg(argc, argv, "--allow-zero-off-axis-field")) {
        merit.require_off_axis_field_coverage = false;
    }
    merit.hard_trace_gate_weight =
        double_arg(argc, argv, "--hard-trace-gate-weight", merit.hard_trace_gate_weight);
    merit.trace_progress_weight =
        double_arg(argc, argv, "--trace-progress-weight", merit.trace_progress_weight);
    merit.chief_progress_weight =
        double_arg(argc, argv, "--chief-progress-weight", merit.chief_progress_weight);
    merit.trace_priority_weight =
        double_arg(argc, argv, "--trace-priority-weight", merit.trace_priority_weight);
    if (has_arg(argc, argv, "--allow-soft-aim-rays-for-spot")) {
        merit.allow_soft_aim_rays_for_spot = true;
    }
    merit.soft_aim_residual_for_spot =
        double_arg(argc, argv, "--soft-aim-residual-for-spot", merit.soft_aim_residual_for_spot);
    const std::string merit_path = string_arg(argc, argv, "--merit");
    if (!merit_path.empty()) {
        std::string error;
        if (!load_merit_json(merit_path, &merit, &error)) {
            std::cerr << "Failed to load merit config: " << error << "\n";
            return 4;
        }
    }
    return kContinueCommand;
}

int CommandContext::override_merit() {
    merit.num_rays = int_arg(argc, argv, "--rays", merit.num_rays);
    merit.pupil_arms = int_arg(argc, argv, "--arms",
        int_arg(argc, argv, "--pupil-arms", merit.pupil_arms));
    merit.pupil_distribution = string_arg(argc, argv, "--distribution", merit.pupil_distribution);
    merit.codev_sampling_profile = string_arg(
        argc, argv, "--codev-sampling-profile", merit.codev_sampling_profile);
    merit.codev_ray_interval = std::clamp(
        double_arg(argc, argv, "--codev-ray-interval", merit.codev_ray_interval), 0.0, 1.0);
    merit.codev_obscuration = std::clamp(
        double_arg(argc, argv, "--codev-obscuration", merit.codev_obscuration), 0.0, 1.0);
    merit.codev_spot_aperture_weight = double_arg(
        argc, argv, "--codev-spot-aperture-weight", merit.codev_spot_aperture_weight);
    merit.codev_wavefront_aperture_weight = double_arg(
        argc, argv, "--codev-wavefront-aperture-weight", merit.codev_wavefront_aperture_weight);
    merit.image_field_weight_mode = string_arg(
        argc, argv, "--image-field-weight-mode", merit.image_field_weight_mode);
    merit.image_field_edge_weight = std::max(
        1.0,
        double_arg(
            argc, argv, "--image-field-edge-weight", merit.image_field_edge_weight));
    merit.image_field_weight_exponent = std::max(
        0.1,
        double_arg(
            argc, argv, "--image-field-weight-exponent", merit.image_field_weight_exponent));
    if (has_arg(argc, argv, "--codev-symmetry") ||
        has_arg(argc, argv, "--pupil-symmetry")) {
        merit.codev_use_symmetry = true;
    }
    if (has_arg(argc, argv, "--no-codev-symmetry") ||
        has_arg(argc, argv, "--no-pupil-symmetry")) {
        merit.codev_use_symmetry = false;
    }
    merit.wavefront_reference =
        string_arg(argc, argv, "--wavefront-reference", merit.wavefront_reference);
    merit.sampling_seed = static_cast<unsigned int>(
        std::max(0, int_arg(argc, argv, "--sampling-seed", static_cast<int>(merit.sampling_seed))));
    merit.dynamic_apertures = update_clear_apertures;
    if (has_arg(
            argc,
            argv,
            "--vignetting-merit-aperture-envelope-experimental")) {
        merit.vignetting_merit_aperture_envelope_experimental = true;
    }
    if (has_arg(
            argc,
            argv,
            "--no-vignetting-merit-aperture-envelope-experimental")) {
        merit.vignetting_merit_aperture_envelope_experimental = false;
    }
    if (clear_aperture_rings_explicit) {
        merit.clear_aperture_rings = clear_aperture_rings;
    }
    merit.trace_policy = trace_policy_arg(argc, argv, merit.trace_policy);
    merit.min_field_coverage = double_arg(argc, argv, "--min-field-coverage", merit.min_field_coverage);
    merit.min_system_coverage = double_arg(argc, argv, "--min-system-coverage", merit.min_system_coverage);
    merit.min_coverage_for_image_merit =
        double_arg(argc, argv, "--min-image-coverage", merit.min_coverage_for_image_merit);
    merit.trace_failure_weight =
        double_arg(argc, argv, "--trace-failure-weight", merit.trace_failure_weight);
    merit.coverage_failure_weight =
        double_arg(argc, argv, "--coverage-failure-weight", merit.coverage_failure_weight);
    merit.aim_failure_weight =
        double_arg(argc, argv, "--aim-failure-weight", merit.aim_failure_weight);
    merit.tir_failure_weight =
        double_arg(argc, argv, "--tir-failure-weight", merit.tir_failure_weight);
    merit.aperture_failure_weight =
        double_arg(argc, argv, "--aperture-failure-weight", merit.aperture_failure_weight);
    merit.surface_miss_weight =
        double_arg(argc, argv, "--surface-miss-weight", merit.surface_miss_weight);
    merit.backward_failure_weight =
        double_arg(argc, argv, "--backward-failure-weight", merit.backward_failure_weight);
    if (has_arg(argc, argv, "--require-chief-ray")) {
        merit.require_chief_ray = true;
    }
    if (has_arg(argc, argv, "--allow-chief-ray-failure")) {
        merit.require_chief_ray = false;
    }
    if (has_arg(argc, argv, "--require-off-axis-field-coverage")) {
        merit.require_off_axis_field_coverage = true;
    }
    if (has_arg(argc, argv, "--allow-zero-off-axis-field")) {
        merit.require_off_axis_field_coverage = false;
    }
    merit.hard_trace_gate_weight =
        double_arg(argc, argv, "--hard-trace-gate-weight", merit.hard_trace_gate_weight);
    merit.trace_progress_weight =
        double_arg(argc, argv, "--trace-progress-weight", merit.trace_progress_weight);
    merit.chief_progress_weight =
        double_arg(argc, argv, "--chief-progress-weight", merit.chief_progress_weight);
    merit.trace_priority_weight =
        double_arg(argc, argv, "--trace-priority-weight", merit.trace_priority_weight);
    if (has_arg(argc, argv, "--allow-soft-aim-rays-for-spot")) {
        merit.allow_soft_aim_rays_for_spot = true;
    }
    merit.soft_aim_residual_for_spot =
        double_arg(argc, argv, "--soft-aim-residual-for-spot", merit.soft_aim_residual_for_spot);
    merit.surface_tangent_angle_lower_deg = std::clamp(
        double_arg(
            argc,
            argv,
            "--surface-tangent-angle-lower-deg",
            merit.surface_tangent_angle_lower_deg),
        0.0,
        90.0);
    merit.surface_tangent_angle_samples = std::clamp(
        int_arg(
            argc,
            argv,
            "--surface-tangent-angle-samples",
            merit.surface_tangent_angle_samples),
        2,
        4096);
    merit.quality_surface_tangent_angle_tol_deg = std::max(
        1.0e-9,
        double_arg(
            argc,
            argv,
            "--surface-tangent-angle-tol-deg",
            merit.quality_surface_tangent_angle_tol_deg));
    merit.quality_conic_coefficient_tol = std::max(
        1.0e-9,
        double_arg(
            argc,
            argv,
            "--conic-coefficient-tol",
            merit.quality_conic_coefficient_tol));
    return kContinueCommand;
}

} } }
