#pragma once
#include "../prelude.cuh"
#include "constants.cuh"
#include "optical_records.cuh"
#include "../diagnostics/asphere_profile_records.cuh"
#include "../diagnostics/builder_instrumentation.cuh"

namespace optiland {
namespace {
struct DeviceConfig {
    const DeviceField* candidate_fields = nullptr;
    int surface_count = 0;
    int variable_count = 0;
    int field_count = 0;
    int wavelength_count = 0;
    int glass_proximity_count = 0;
    int glass_proximity_point_count = 0;
    int first_optical_surface = 0;
    int bfl_reference_surface = -1;
    // 0: strict reference, 1: trust Newton, 2: confirmed initial root with
    // strict fallback, 3: confirmation without fallback, 4: slope-domain
    // Newton upper bound, 5: production topology-seed/no-B2 route,
    // 6: isolated unified certified experimental route, 7: UC1 variant with
    // a conservative candidate-prefix certificate and semantic S1 seed,
    // 8: UC2 exact-S1 constructed-root/no-root classifier over the same
    // locked slope-domain contract, 9: UC3 candidate-failure certificate,
    // 10: UC4 dead-construction elimination layered on UC3,
    // 11: isolated UC6 prefix-evaluation reuse layered on UC4,
    // 12/13/14: isolated UC7 residual/fused/combined Horner evaluation.
    int asphere_initial_root_mode = 2;
    // Four samples are the even-index subset of the validated eight-sample
    // grid.  The coarser derivative steps are deliberately conservative: they
    // send more rapidly varying profiles to the strict fallback rather than
    // accepting a new class of roots.
    int asphere_confirmation_segments = 4;
    int asphere_slope_root_domain_enabled = 0;
    int asphere_unified_contract_domain_enabled = 0;
    int asphere_aiming_local_branch = 0;
    int asphere_auxiliary_local_branch = 0;
    int asphere_slope_root_domain_reuse = 0;
    int asphere_upper_bound_mode = 0;
    int asphere_exact_s1_seed_bypass = 0;
    int asphere_deep_profile_row = -1;
    int asphere_deep_profile_field_index = -1;
    int asphere_deep_profile_wave_index = -1;
    int primary_wave_index = 0;
    int primary_wavelength_apertures = 0;
    int aperture_type = 0;
    int aiming_mode = 1;
    int aiming_stop_radius_source = 0;
    int calibrated_enabled = 0;
    int calibrated_angle_bridge_fast = 0;
    int calibrated_budget_ignore_aperture_clip = 0;
    int calibrated_budget_allow_inside_stop = 0;
    int calibrated_budget_reuse_solve_context = 0;
    int calibrated_ordered_reuse_solve_context = 0;
    int calibrated_edge_iterations = 0;
    int calibrated_budget_iterations = 0;
    int calibrated_validation_sample_count = 0;
    int dynamic_apertures = 0;
    int clear_aperture_rings = 4;
    int clear_aperture_arms = 8;
    int pupil_arms = 6;
    int num_rays = 3;
    int pupil_sampling_type = kPupilSamplingGaussianRingArm;
    int fixed_budget_sampling = 0;
    int codev_use_symmetry = 1;
    int image_field_edge_power = 1;
    int elevated_field_pupil_arms = 0;
    int elevated_field_num_rays = 0;
    int surface_tangent_angle_samples = 32;
    int unified_trace_gate = 0;
    int soft_trace = 1;
    int require_chief_ray = 0;
    int require_off_axis_field_coverage = 0;
    int reject_ray_path_structural_failures = 0;
    int reject_chief_entry_side = 1;
    int axis_cross_allow_count = 1;
    int pupil_order_hard_consecutive_surfaces = 1;
    int distortion_use_f_theta = 0;
    int operand_rms_spot = 1;
    int operand_max_spot = 0;
    int operand_worst_wavelength_spot = 0;
    int operand_spectral_spot_spread = 0;
    int operand_single_ray_spot_guard = 0;
    int operand_single_ray_spot_tail = 0;
    int operand_rms_wavefront = 0;
    int operand_single_ray_wavefront_guard = 0;
    int wavefront_centroid_reference = 0;
    int operand_efl = 1;
    int operand_ttl = 1;
    int operand_bfl = 1;
    int operand_working_f_number = 0;
    int working_f_number_target_mode = 0;
    int operand_max_distortion = 1;
    int operand_max_chief_ray_angle = 1;
    int operand_lateral_color = 1;
    int operand_axial_color = 0;
    int stop_air_edge_surface = -1;
    double stop_air_edge_lower = 0.0;
    double stop_air_edge_weight = 0.0;
    unsigned long long thickness_variable_mask = 0ULL;
    unsigned long long conic_soft_bound_mask = 0ULL;
    double primary_wavelength = 0.5875618;
    double asphere_slope_root_domain_min_tangent_deg = 0.0;
    double elevated_field_sampling_min_radius = 2.0;
    double codev_ray_interval = 0.385;
    double codev_obscuration = 0.0;
    double codev_spot_aperture_weight = 0.0;
    double codev_wavefront_aperture_weight = 0.0;
    double image_field_edge_weight = 2.0;
    double image_field_weight_exponent = 2.0;
    double entrance_pupil_radius = 1.0;
    double aperture_value = 10.0;
    double ray_aiming_tolerance = 1.0e-8;
    double calibrated_validation_residual_stop_fraction = 0.05;
    double calibrated_edge_residual_stop_fraction = 1.0e-6;
    double calibrated_budget_residual_stop_fraction = 1.0e-6;
    double calibrated_budget_accept_stop_fraction = 0.05;
    double calibrated_budget_min_a = 1.0e-6;
    double calibrated_budget_field_continuation_angle_deg = 75.0;
    double calibrated_budget_skip_abcd_angle_deg = 60.0;
    double calibrated_max_field_extrapolation_factor = 2.0;
    double max_field_deg = 0.0;
    double target_efl = 0.0;
    double ttl_upper = NAN;
    double bfl_lower = NAN;
    double working_f_number_upper = NAN;
    double working_f_number_target = NAN;
    double spot_target = 0.0;
    double worst_wavelength_spot_upper = NAN;
    double spectral_spot_spread_upper = NAN;
    double single_ray_spot_guard_upper = NAN;
    double single_ray_spot_tail_weight = 0.0;
    double single_ray_spot_tail_extra_edge_min_field_radius = 0.85;
    double single_ray_wavefront_guard_upper = NAN;
    double max_distortion_upper = NAN;
    double max_chief_ray_angle_upper = NAN;
    double lateral_color_upper = NAN;
    double axial_color_upper = NAN;
    double conic_soft_bound_lower = NAN;
    double conic_soft_bound_upper = NAN;
    double clear_aperture_margin = 1.02;
    double fail_merit = 1.0e12;
    double min_edge_thickness = 0.05;
    double weight_edge = 0.5;
    double glass_center_lower = 0.5;
    double glass_center_upper = 10.0;
    double air_center_lower = 0.1;
    double air_center_upper = 500.0;
    double glass_edge_lower = 0.5;
    double glass_edge_upper = 10.0;
    double air_edge_lower = 0.1;
    double air_edge_upper = 500.0;
    double structural_prior_tight_triplet_weight = 5.0;
    double structural_prior_gap_tolerance = 0.05;
    double structural_prior_curvature_epsilon = 1.0e-9;
    double pupil_order_prior_weight = 20.0;
    double pupil_order_jacobian_floor = 0.05;
    double pupil_order_soft_tolerance = 0.25;
    double pupil_order_hard_jacobian = -1.0e-4;
    double pupil_order_hard_max_progress = 1.0;
    double pupil_order_min_field_radius = 0.75;
    double atc_lower = 0.1;
    double atc_upper = 0.0;
    double ate_lower = 0.1;
    double ate_upper = 0.0;
    double bl0_lower = 0.0;
    double bl0_upper = 0.9;
    double surface_tangent_angle_lower_deg = 30.0;
    double quality_spot_scale = 0.01;
    double quality_wavefront_scale = 0.50;
    double quality_efl_rel_tol = 0.01;
    double quality_ttl_rel_tol = 0.02;
    double quality_bfl_rel_tol = 0.02;
    double quality_working_f_number_tol = 0.01;
    double quality_length_abs_tol = 0.1;
    double quality_edge_tol = 0.05;
    double quality_ratio_tol = 0.01;
    double quality_bound_tol = 0.05;
    double minimum_relative_illumination = NAN;
    int relative_illumination_field_selected[kMaxDeviceFields] = {};
    double relative_illumination_operand_lower[kMaxDeviceFields] = {};
    double relative_illumination_operand_weight[kMaxDeviceFields] = {};
    double quality_relative_illumination_tol = 0.01;
    double vignetting_meridional_excess_weight = 25.0;
    double vignetting_soft_prior_weight = 0.25;
    double vignetting_soft_field_power = 4.0;
    double vignetting_loss_budget_fraction = 2.0 / 3.0;
    double vignetting_edge_fvdy_cap = 0.5;
    double vignetting_boundary_support_weight = 25.0;
    int vignetting_boundary_support_contract = 0;
    double vignetting_boundary_support_tolerance = 0.02;
    double vignetting_boundary_support_activation = 0.02;
    double vignetting_boundary_support_transition = 0.05;
    double vignetting_boundary_support_selectivity_slope = 0.05;
    int vignetting_physical_cut_match_enabled = 0;
    int vignetting_merit_aperture_envelope_experimental = 0;
    double quality_surface_tangent_angle_tol_deg = 5.0;
    double quality_conic_coefficient_tol = 1.0;
    double quality_distortion_tol = 0.01;
    double quality_chief_ray_angle_tol = 1.0;
    double quality_lateral_color_tol = 0.01;
    double quality_axial_color_tol = 0.025;
    double quality_spectral_spot_spread_tol = 0.0015;
    double min_field_coverage = 1.0;
    double min_system_coverage = 1.0;
    double min_coverage_for_image_merit = 0.5;
    double trace_failure_weight = 1.0;
    double coverage_failure_weight = 10.0;
    double aim_failure_weight = 1.0;
    double tir_failure_weight = 10.0;
    double aperture_failure_weight = 2.0;
    double surface_miss_weight = 5.0;
    double backward_failure_weight = 10.0;
    double hard_trace_gate_weight = 1000.0;
    double trace_progress_weight = 0.0;
    double chief_progress_weight = 0.0;
    double trace_priority_weight = 0.0;
    double axis_crossing_epsilon = 1.0e-3;
    double center_early_focus_guard_mm = 0.5;
    double center_early_focus_edge_ratio = 0.75;
    double unified_loss_delta = 2.0;
    double group_weight_trace = 50.0;
    double group_weight_constraint = 1.0;
    double group_weight_first_order = 20.0;
    double group_weight_image = 30.0;
    double unified_coverage_tol = 0.05;
    double unified_progress_tol = 0.10;
    double unified_failed_weight_tol = 1.0;
    double unified_trace_system_weight = 20.0;
    double unified_trace_min_field_weight = 40.0;
    double unified_trace_progress_weight = 8.0;
    double unified_trace_chief_progress_weight = 25.0;
    double unified_trace_failed_weight = 12.0;
    double unified_trace_failure_type_weight = 8.0;
    double unified_edge_tol = 0.10;
    double unified_air_gap_tol = 0.10;
    double unified_center_thickness_tol = 0.20;
    double unified_ttl_tol = 1.0;
    double unified_bfl_tol = 1.0;
    double unified_constraint_edge_weight = 25.0;
    double unified_constraint_air_gap_weight = 12.0;
    double unified_constraint_center_weight = 12.0;
    double unified_constraint_bfl_weight = 18.0;
    double unified_constraint_ttl_weight = 12.0;
    double unified_constraint_curvature_weight = 4.0;
    double unified_constraint_aperture_weight = 15.0;
    double unified_efl_rel_tol = 0.01;
    double unified_length_abs_tol = 0.10;
    double unified_first_order_efl_weight = 1.0;
    double unified_first_order_ttl_weight = 0.4;
    double unified_first_order_bfl_weight = 0.5;
    double unified_first_order_working_f_number_weight = 1.0;
    double unified_first_order_image_height_weight = 6.0;
    double unified_image_mean_spot_weight = 3.0;
    double unified_image_single_ray_spot_tail_weight = 0.012;
    double unified_image_wavefront_weight = 1.0;
    double unified_image_max_spot_weight = 10.0;
    double unified_image_field_balance_weight = 4.0;
    double unified_image_distortion_weight = 6.0;
    double unified_image_cra_weight = 5.0;
    double unified_image_chromatic_weight = 5.0;
    double unified_gate_system_coverage_lo = 0.85;
    double unified_gate_min_field_lo = 0.50;
    double unified_gate_chief_progress_lo = 0.70;
    double unified_min_first_order_gate = 0.25;
    const double* material_indices = nullptr;
    DeviceAsphereRootProfile* asphere_root_profile = nullptr;
    DeviceAsphereFeasibilityProfile* asphere_feasibility_profile = nullptr;
    DeviceAsphereDeepProfile* asphere_deep_profile = nullptr;
    double wavelength_values[kMaxDeviceWaves] = {};
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ void d_field_radius_extrema(
    const DeviceField* fields,
    const DeviceConfig& cfg,
    double* minimum,
    double* maximum) {
    double minimum_radius = INFINITY;
    double maximum_radius = 0.0;
    for (int fi = 0; fi < cfg.field_count; ++fi) {
        const double radius = hypot(fields[fi].hx, fields[fi].hy);
        if (!isfinite(radius)) {
            continue;
        }
        minimum_radius = fmin(minimum_radius, radius);
        maximum_radius = fmax(maximum_radius, radius);
    }
    if (!isfinite(minimum_radius)) {
        minimum_radius = 0.0;
    }
    if (minimum) {
        *minimum = minimum_radius;
    }
    if (maximum) {
        *maximum = maximum_radius;
    }
}
} // namespace
} // namespace optiland
