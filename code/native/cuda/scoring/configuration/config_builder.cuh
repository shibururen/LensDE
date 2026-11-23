#pragma once
#include "../prelude.cuh"
#include "asphere_policy.cuh"
#include "merit_operands.cuh"
#include "runtime_options.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"

namespace optiland {
namespace {
DeviceConfig make_device_config(
    const LensSystem& base,
    const MeritConfig& merit,
    const std::vector<VariableSpec>& variables) {
    DeviceConfig cfg;
    cfg.surface_count = static_cast<int>(base.surfaces.size());
    cfg.field_count = static_cast<int>(base.fields.empty() ? 1 : base.fields.size());
    cfg.wavelength_count =
        static_cast<int>(base.wavelengths.empty() ? 1 : base.wavelengths.size());
    cfg.glass_proximity_count = static_cast<int>(merit.glass_proximity.size());
    for (const VariableSpec& variable : variables) {
        if (variable.kind == VariableKind::Thickness && variable.index < 64) {
            cfg.thickness_variable_mask |= 1ULL << variable.index;
        }
    }
    for (const BoundConstraint& bound : merit.bounds) {
        if (normalized_token(bound.kind) != "conic" || bound.index >= 64 ||
            bound.term != 0 || !(bound.weight > 0.0)) {
            continue;
        }
        cfg.conic_soft_bound_mask |= 1ULL << bound.index;
        cfg.conic_soft_bound_lower = bound.lower;
        cfg.conic_soft_bound_upper = bound.upper;
    }
    cfg.first_optical_surface = static_cast<int>(base.first_optical_surface());
    cfg.bfl_reference_surface = base.bfl_reference_surface;
    // The public policy is shared with the CPU tracer so an explicit scoring
    // contract has the same meaning on both evaluators. Environment switches
    // remain higher-priority audit/ablation controls.
    const bool production_asphere_policy =
        asphere_root_policy() == AsphereRootPolicy::SlopeDomainTopologySeed;
    const bool unified_certified_policy =
        asphere_root_policy() ==
        AsphereRootPolicy::UnifiedCertifiedExperimental;
    ConfigureAspherePolicyInputs configure_asphere_policy_inputs{cfg, production_asphere_policy, unified_certified_policy};
    configure_asphere_policy(configure_asphere_policy_inputs);

    if (const char* value =
            std::getenv("OPTILAND_DE_CUDA_ASPHERE_SLOPE_ROOT_DOMAIN_DEG")) {
        const double parsed = std::atof(value);
        if (std::isfinite(parsed) && parsed > 0.0) {
            cfg.asphere_slope_root_domain_min_tangent_deg =
                std::clamp(parsed, 0.0, 89.999999);
            cfg.asphere_slope_root_domain_enabled = 1;
        }
    }
    if (const char* value =
            std::getenv("OPTILAND_DE_CUDA_ASPHERE_UPPER_BOUND_MODE")) {
        const std::string mode = normalized_token(value);
        if (mode == "u0" || mode == "newton_unbounded" || mode == "1") {
            cfg.asphere_upper_bound_mode = 1;
            cfg.asphere_slope_root_domain_enabled = 0;
        } else if (mode == "u1" || mode == "newton_domain" || mode == "2") {
            cfg.asphere_upper_bound_mode = 2;
            cfg.asphere_initial_root_mode = 4;
            if (!cfg.asphere_slope_root_domain_enabled) {
                cfg.asphere_slope_root_domain_min_tangent_deg =
                    kAsphereRootDomainMinimumTangentAxisAngleDeg;
                cfg.asphere_slope_root_domain_enabled = 1;
            }
        }
    }
    if (const char* value = std::getenv(
            "OPTILAND_DE_CUDA_ASPHERE_PIECEWISE_PREFIX_SEGMENTS")) {
        const int parsed = std::atoi(value);
        if (parsed == 4 || parsed == 6 || parsed == 8) {
            cfg.asphere_initial_root_mode = 5;
            cfg.asphere_confirmation_segments = parsed;
            if (!cfg.asphere_slope_root_domain_enabled) {
                cfg.asphere_slope_root_domain_min_tangent_deg =
                    kAsphereRootDomainMinimumTangentAxisAngleDeg;
                cfg.asphere_slope_root_domain_enabled = 1;
            }
        }
    }
    if ((!production_asphere_policy || cfg.asphere_initial_root_mode != 5) &&
        !unified_certified_policy) {
        cfg.asphere_aiming_local_branch =
            cfg.asphere_slope_root_domain_enabled &&
                    env_flag_enabled(
                        "OPTILAND_DE_CUDA_ASPHERE_AIMING_LOCAL_BRANCH")
                ? 1
                : 0;
        cfg.asphere_auxiliary_local_branch =
            cfg.asphere_slope_root_domain_enabled &&
                    env_flag_enabled(
                        "OPTILAND_DE_CUDA_ASPHERE_AUXILIARY_LOCAL_BRANCH")
                ? 1
                : 0;
        cfg.asphere_slope_root_domain_reuse =
            cfg.asphere_slope_root_domain_enabled &&
                    env_flag_enabled(
                        "OPTILAND_DE_CUDA_ASPHERE_SLOPE_ROOT_DOMAIN_REUSE")
                ? 1
                : 0;
    } else {
        // Production defaults stay locked to the validated P6 route, but an
        // explicitly present variable remains available for same-binary
        // diagnosis of the aiming/local and auxiliary branches.
        if (std::getenv("OPTILAND_DE_CUDA_ASPHERE_AIMING_LOCAL_BRANCH")) {
            cfg.asphere_aiming_local_branch = env_flag_enabled(
                "OPTILAND_DE_CUDA_ASPHERE_AIMING_LOCAL_BRANCH") ? 1 : 0;
        }
        if (std::getenv("OPTILAND_DE_CUDA_ASPHERE_AUXILIARY_LOCAL_BRANCH")) {
            cfg.asphere_auxiliary_local_branch = env_flag_enabled(
                "OPTILAND_DE_CUDA_ASPHERE_AUXILIARY_LOCAL_BRANCH") ? 1 : 0;
        }
        if (std::getenv("OPTILAND_DE_CUDA_ASPHERE_SLOPE_ROOT_DOMAIN_REUSE")) {
            cfg.asphere_slope_root_domain_reuse = env_flag_enabled(
                "OPTILAND_DE_CUDA_ASPHERE_SLOPE_ROOT_DOMAIN_REUSE") ? 1 : 0;
        }
    }
    if (const char* value =
            std::getenv("OPTILAND_DE_CUDA_ASPHERE_CONFIRMATION_SEGMENTS")) {
        const int parsed = std::atoi(value);
        if (parsed == 4 || parsed == 6 || parsed == 8) {
            cfg.asphere_confirmation_segments = parsed;
        }
    }
    cfg.primary_wavelength = base.primary_wavelength();
    cfg.primary_wavelength_apertures = merit.vignetting_optimization_enabled() ? 1 : 0;
    cfg.entrance_pupil_radius = base.entrance_pupil_radius();
    cfg.aperture_type = static_cast<int>(base.aperture.type);
    cfg.aperture_value = base.aperture.value;
    cfg.aiming_mode = static_cast<int>(base.ray_aiming.mode);
    cfg.aiming_stop_radius_source =
        static_cast<int>(base.ray_aiming.stop_radius_source);
    cfg.ray_aiming_tolerance = base.ray_aiming.tolerance;
    if (base.ray_aiming.mode == RayAimingMode::CalibratedParaxial) {
        const auto& cp = base.ray_aiming.calibrated_paraxial;
        cfg.calibrated_enabled = cp.enabled ? 1 : 0;
        const std::string chief_mode = normalized_token(cp.chief_anchor_mode);
        cfg.calibrated_angle_bridge_fast =
            (chief_mode == "abcd_budget_angle_bridge_fast" ||
             chief_mode == "budget_abcd_angle_bridge_fast" ||
             chief_mode == "abcd_budget_angle_bridge_fast_then_full" ||
             chief_mode == "budget_abcd_angle_bridge_fast_then_full")
                ? 1
                : 0;
        cfg.calibrated_budget_ignore_aperture_clip =
            cp.budget_chief_ignore_aperture_clip ? 1 : 0;
        cfg.calibrated_budget_allow_inside_stop =
            cp.budget_chief_acceptance == "inside_stop" ||
                    cp.budget_chief_acceptance == "stop_inside" ||
                    cp.budget_chief_acceptance == "physical_stop"
                ? 1
                : 0;
        cfg.calibrated_budget_reuse_solve_context = env_flag_enabled_default(
            "OPTILAND_DE_CUDA_CALIBRATED_REUSE_BUDGET_CONTEXT",
            true)
                                                        ? 1
                                                        : 0;
        cfg.calibrated_ordered_reuse_solve_context = env_flag_enabled(
            "OPTILAND_DE_CUDA_CALIBRATED_ORDERED_REUSE_BUDGET_CONTEXT")
                                                        ? 1
                                                        : 0;
        cfg.calibrated_edge_iterations = std::max(0, cp.edge_calibration_iterations);
        cfg.calibrated_budget_iterations = std::max(0, cp.budget_chief_iterations);
        cfg.calibrated_validation_sample_count =
            std::max(0, cp.validation_sample_count);
        cfg.calibrated_validation_residual_stop_fraction =
            cp.validation_residual_stop_fraction;
        cfg.calibrated_edge_residual_stop_fraction =
            cp.edge_calibration_residual_stop_fraction;
        cfg.calibrated_budget_residual_stop_fraction =
            cp.budget_chief_residual_stop_fraction;
        cfg.calibrated_budget_accept_stop_fraction =
            cp.budget_chief_accept_stop_fraction;
        cfg.calibrated_budget_min_a = cp.budget_chief_min_a;
        cfg.calibrated_budget_field_continuation_angle_deg =
            cp.budget_chief_field_continuation_angle_deg;
        cfg.calibrated_budget_skip_abcd_angle_deg =
            cp.budget_chief_skip_abcd_angle_deg;
        cfg.calibrated_max_field_extrapolation_factor =
            base.ray_aiming.adaptive_pilot_grid.max_field_extrapolation_factor;
    }
    cfg.dynamic_apertures = merit.dynamic_apertures ? 1 : 0;
    cfg.clear_aperture_arms = std::max(1, merit.clear_aperture_arms);
    cfg.clear_aperture_rings =
        merit.vignetting_merit_aperture_envelope_experimental
            ? std::max(1, merit.num_rays)
            : (merit.clear_aperture_rings > 0
                   ? merit.clear_aperture_rings
                   : merit.num_rays);
    cfg.clear_aperture_margin = merit.effective_clear_aperture_margin();
    cfg.pupil_arms = merit.pupil_arms;
    cfg.num_rays = merit.num_rays;
    cfg.fixed_budget_sampling = normalized_token(merit.pupil_distribution) == "gaussian_ring_arm_fixed_budget" ? 1 : 0;
    cfg.pupil_sampling_type =
        is_codev_rectangular_distribution(merit.pupil_distribution)
            ? kPupilSamplingCodevRectangular
            : kPupilSamplingGaussianRingArm;
    cfg.codev_use_symmetry = merit.codev_use_symmetry ? 1 : 0;
    cfg.codev_ray_interval = effective_codev_ray_interval(base, merit);
    cfg.codev_obscuration = std::clamp(merit.codev_obscuration, 0.0, 1.0);
    cfg.codev_spot_aperture_weight = merit.codev_spot_aperture_weight;
    cfg.codev_wavefront_aperture_weight = merit.codev_wavefront_aperture_weight;
    const std::string image_field_weight_mode =
        normalized_token(merit.image_field_weight_mode);
    cfg.image_field_edge_power =
        image_field_weight_mode == "center_double" ? 2 :
        image_field_weight_mode == "uniform" || image_field_weight_mode == "none" ||
                image_field_weight_mode == "off" || image_field_weight_mode == "disabled"
            ? 0 : 1;
    cfg.image_field_edge_weight = std::max(1.0, merit.image_field_edge_weight);
    cfg.image_field_weight_exponent =
        std::max(0.1, merit.image_field_weight_exponent);
    cfg.elevated_field_sampling_min_radius =
        merit.elevated_field_sampling_min_radius;
    cfg.elevated_field_num_rays = merit.elevated_field_num_rays;
    cfg.elevated_field_pupil_arms = merit.elevated_field_pupil_arms;
    cfg.single_ray_spot_tail_extra_edge_min_field_radius = std::clamp(
        merit.single_ray_spot_tail_extra_edge_min_field_radius,
        0.0,
        2.0);
    cfg.max_field_deg = base.max_field_deg;
    cfg.unified_trace_gate = unified_trace_gate_enabled(merit) ? 1 : 0;
    cfg.soft_trace = merit.trace_policy != TraceFailurePolicy::Strict ? 1 : 0;
    cfg.require_chief_ray = merit.require_chief_ray ? 1 : 0;
    cfg.require_off_axis_field_coverage =
        merit.require_off_axis_field_coverage ? 1 : 0;
    cfg.reject_ray_path_structural_failures =
        merit.reject_ray_path_structural_failures ? 1 : 0;
    cfg.reject_chief_entry_side = merit.vignetting_optimization_enabled() ? 0 : 1;
    cfg.axis_cross_allow_count = std::max(0, merit.axis_cross_allow_count);
    cfg.target_efl = merit.target_efl;
    cfg.ttl_upper = merit.target_ttl > 0.0 ? merit.target_ttl : std::numeric_limits<double>::quiet_NaN();
    cfg.bfl_lower = merit.target_bfl > 0.0 ? merit.target_bfl : std::numeric_limits<double>::quiet_NaN();
    cfg.working_f_number_upper = std::numeric_limits<double>::quiet_NaN();
    cfg.spot_target = merit.spot_target;
    cfg.quality_wavefront_scale = merit.quality_wavefront_scale;
    const std::string wavefront_reference = normalized_token(merit.wavefront_reference);
    cfg.wavefront_centroid_reference =
        wavefront_reference == "centroid" ||
                wavefront_reference == "centroid_sphere" ||
                wavefront_reference == "centroid_reference" ||
                wavefront_reference == "centroid_reference_sphere"
            ? 1
            : 0;
    const std::string distortion_model = normalized_token(merit.distortion_model);
    cfg.distortion_use_f_theta =
        distortion_model == "f_theta" || distortion_model == "ftheta" ||
        distortion_model == "theta" ||
        (distortion_model == "auto" && std::abs(base.max_field_deg) >= 80.0)
            ? 1
            : 0;
    ConfigureMeritOperandsInputs configure_merit_operands_inputs{merit, cfg};
    configure_merit_operands(configure_merit_operands_inputs);

    cfg.fail_merit = merit.fail_merit;
    cfg.min_edge_thickness = merit.min_edge_thickness;
    cfg.weight_edge = merit.weight_edge;
    cfg.glass_center_lower = merit.glass_center_thickness_lower;
    cfg.glass_center_upper = merit.glass_center_thickness_upper;
    cfg.air_center_lower = merit.air_center_thickness_lower;
    cfg.air_center_upper = merit.air_center_thickness_upper;
    cfg.glass_edge_lower = merit.glass_edge_thickness_lower;
    cfg.glass_edge_upper = merit.glass_edge_thickness_upper;
    cfg.air_edge_lower = merit.air_edge_thickness_lower;
    cfg.air_edge_upper = merit.air_edge_thickness_upper;
    cfg.structural_prior_tight_triplet_weight =
        merit.structural_prior_tight_triplet_weight;
    cfg.structural_prior_gap_tolerance =
        merit.structural_prior_gap_tolerance;
    cfg.structural_prior_curvature_epsilon =
        merit.structural_prior_curvature_epsilon;
    cfg.pupil_order_prior_weight = merit.pupil_order_prior_weight;
    cfg.pupil_order_jacobian_floor = merit.pupil_order_jacobian_floor;
    cfg.pupil_order_soft_tolerance = merit.pupil_order_soft_tolerance;
    cfg.pupil_order_hard_jacobian = merit.pupil_order_hard_jacobian;
    cfg.pupil_order_hard_max_progress =
        merit.pupil_order_hard_max_progress;
    cfg.pupil_order_hard_consecutive_surfaces =
        merit.pupil_order_hard_consecutive_surfaces;
    cfg.pupil_order_min_field_radius = merit.pupil_order_min_field_radius;
    cfg.atc_lower = merit.atc_lower;
    cfg.atc_upper = merit.atc_upper;
    cfg.ate_lower = merit.ate_lower;
    cfg.ate_upper = merit.ate_upper;
    cfg.bl0_lower = merit.bl0_lower;
    cfg.bl0_upper = merit.bl0_upper;
    cfg.surface_tangent_angle_lower_deg = merit.surface_tangent_angle_lower_deg;
    cfg.surface_tangent_angle_samples = merit.surface_tangent_angle_samples;
    cfg.quality_spot_scale = merit.quality_spot_scale;
    cfg.quality_efl_rel_tol = merit.quality_efl_rel_tol;
    cfg.quality_ttl_rel_tol = merit.quality_ttl_rel_tol;
    cfg.quality_bfl_rel_tol = merit.quality_bfl_rel_tol;
    cfg.quality_working_f_number_tol = merit.quality_working_f_number_tol;
    cfg.quality_length_abs_tol = merit.quality_length_abs_tol;
    cfg.quality_edge_tol = merit.quality_edge_tol;
    cfg.quality_ratio_tol = merit.quality_ratio_tol;
    cfg.quality_bound_tol = merit.quality_bound_tol;
    cfg.minimum_relative_illumination =
        merit.minimum_relative_illumination;
    const auto illumination_fields = relative_illumination_supervised_fields(base, merit);
    for (int i = 0; i < kMaxDeviceFields; ++i) {
        cfg.relative_illumination_field_selected[i] = illumination_fields.empty() ||
            std::find(illumination_fields.begin(), illumination_fields.end(),
                      static_cast<std::size_t>(i)) != illumination_fields.end();
    }
    cfg.quality_relative_illumination_tol =
        merit.quality_relative_illumination_tol;
    cfg.vignetting_meridional_excess_weight =
        merit.vignetting_meridional_excess_weight;
    cfg.vignetting_soft_prior_weight = merit.vignetting_soft_prior_weight;
    cfg.vignetting_soft_field_power = merit.vignetting_soft_field_power;
    cfg.vignetting_loss_budget_fraction = merit.vignetting_loss_budget_fraction;
    cfg.vignetting_edge_fvdy_cap = merit.vignetting_edge_fvdy_cap;
    cfg.vignetting_boundary_support_weight =
        merit.vignetting_boundary_support_weight;
    cfg.vignetting_boundary_support_contract =
        merit.vignetting_boundary_support_contract ==
                VignettingBoundarySupportContract::GlobalTrimRealizability
            ? 1
            : 0;
    cfg.vignetting_boundary_support_tolerance =
        merit.vignetting_boundary_support_tolerance;
    cfg.vignetting_boundary_support_activation =
        merit.vignetting_boundary_support_activation;
    cfg.vignetting_boundary_support_transition =
        merit.vignetting_boundary_support_transition;
    cfg.vignetting_boundary_support_selectivity_slope =
        merit.vignetting_boundary_support_selectivity_slope;
    cfg.vignetting_physical_cut_match_enabled =
        merit.vignetting_physical_cut_match_enabled ? 1 : 0;
    cfg.vignetting_merit_aperture_envelope_experimental =
        merit.vignetting_merit_aperture_envelope_experimental ? 1 : 0;
    cfg.quality_surface_tangent_angle_tol_deg =
        merit.quality_surface_tangent_angle_tol_deg;
    cfg.quality_conic_coefficient_tol = merit.quality_conic_coefficient_tol;
    cfg.quality_distortion_tol = merit.quality_distortion_tol;
    cfg.quality_chief_ray_angle_tol = merit.quality_chief_ray_angle_tol;
    cfg.quality_lateral_color_tol = merit.quality_lateral_color_tol;
    cfg.quality_axial_color_tol = merit.quality_axial_color_tol;
    cfg.quality_spectral_spot_spread_tol =
        merit.quality_spectral_spot_spread_tol;
    cfg.min_field_coverage = merit.min_field_coverage;
    cfg.min_system_coverage = merit.min_system_coverage;
    cfg.min_coverage_for_image_merit = merit.min_coverage_for_image_merit;
    cfg.trace_failure_weight = merit.trace_failure_weight;
    cfg.coverage_failure_weight = merit.coverage_failure_weight;
    cfg.aim_failure_weight = merit.aim_failure_weight;
    cfg.tir_failure_weight = merit.tir_failure_weight;
    cfg.aperture_failure_weight = merit.aperture_failure_weight;
    cfg.surface_miss_weight = merit.surface_miss_weight;
    cfg.backward_failure_weight = merit.backward_failure_weight;
    cfg.hard_trace_gate_weight = merit.hard_trace_gate_weight;
    cfg.trace_progress_weight = merit.trace_progress_weight;
    cfg.chief_progress_weight = merit.chief_progress_weight;
    cfg.trace_priority_weight = merit.trace_priority_weight;
    cfg.axis_crossing_epsilon = merit.axis_crossing_epsilon;
    cfg.center_early_focus_guard_mm = merit.center_early_focus_guard_mm;
    cfg.center_early_focus_edge_ratio = merit.center_early_focus_edge_ratio;
    cfg.unified_loss_delta = merit.unified_loss_delta;
    cfg.group_weight_trace = merit.group_weight_trace;
    cfg.group_weight_constraint = merit.group_weight_constraint;
    cfg.group_weight_first_order = merit.group_weight_first_order;
    cfg.group_weight_image = merit.group_weight_image;
    cfg.unified_coverage_tol = merit.unified_coverage_tol;
    cfg.unified_progress_tol = merit.unified_progress_tol;
    cfg.unified_failed_weight_tol = merit.unified_failed_weight_tol;
    cfg.unified_trace_system_weight = merit.unified_trace_system_weight;
    cfg.unified_trace_min_field_weight = merit.unified_trace_min_field_weight;
    cfg.unified_trace_progress_weight = merit.unified_trace_progress_weight;
    cfg.unified_trace_chief_progress_weight = merit.unified_trace_chief_progress_weight;
    cfg.unified_trace_failed_weight = merit.unified_trace_failed_weight;
    cfg.unified_trace_failure_type_weight = merit.unified_trace_failure_type_weight;
    cfg.unified_edge_tol = merit.unified_edge_tol;
    cfg.unified_air_gap_tol = merit.unified_air_gap_tol;
    cfg.unified_center_thickness_tol = merit.unified_center_thickness_tol;
    cfg.unified_ttl_tol = merit.unified_ttl_tol;
    cfg.unified_bfl_tol = merit.unified_bfl_tol;
    cfg.unified_constraint_edge_weight = merit.unified_constraint_edge_weight;
    cfg.unified_constraint_air_gap_weight = merit.unified_constraint_air_gap_weight;
    cfg.unified_constraint_center_weight = merit.unified_constraint_center_weight;
    cfg.unified_constraint_bfl_weight = merit.unified_constraint_bfl_weight;
    cfg.unified_constraint_ttl_weight = merit.unified_constraint_ttl_weight;
    cfg.unified_constraint_curvature_weight = merit.unified_constraint_curvature_weight;
    cfg.unified_constraint_aperture_weight = merit.unified_constraint_aperture_weight;
    cfg.unified_efl_rel_tol = merit.unified_efl_rel_tol;
    cfg.unified_length_abs_tol = merit.unified_length_abs_tol;
    cfg.unified_first_order_efl_weight = merit.unified_first_order_efl_weight;
    cfg.unified_first_order_ttl_weight = merit.unified_first_order_ttl_weight;
    cfg.unified_first_order_bfl_weight = merit.unified_first_order_bfl_weight;
    cfg.unified_first_order_working_f_number_weight =
        merit.unified_first_order_working_f_number_weight;
    cfg.unified_first_order_image_height_weight = merit.unified_first_order_image_height_weight;
    cfg.unified_image_mean_spot_weight = merit.unified_image_mean_spot_weight;
    cfg.unified_image_single_ray_spot_tail_weight =
        merit.unified_image_single_ray_spot_tail_weight;
    cfg.unified_image_wavefront_weight = merit.unified_image_wavefront_weight;
    cfg.unified_image_max_spot_weight = merit.unified_image_max_spot_weight;
    cfg.unified_image_field_balance_weight = merit.unified_image_field_balance_weight;
    cfg.unified_image_distortion_weight = merit.unified_image_distortion_weight;
    cfg.unified_image_cra_weight = merit.unified_image_cra_weight;
    cfg.unified_image_chromatic_weight = merit.unified_image_chromatic_weight;
    cfg.unified_gate_system_coverage_lo = merit.unified_gate_system_coverage_lo;
    cfg.unified_gate_min_field_lo = merit.unified_gate_min_field_lo;
    cfg.unified_gate_chief_progress_lo = merit.unified_gate_chief_progress_lo;
    cfg.unified_min_first_order_gate = merit.unified_min_first_order_gate;
    for (std::size_t i = 0; i < base.wavelengths.size(); ++i) {
        if (base.wavelengths[i].primary) {
            cfg.primary_wave_index = static_cast<int>(i);
            cfg.primary_wavelength = base.wavelengths[i].value_um;
            break;
        }
    }
    if (base.wavelengths.empty()) {
        cfg.wavelength_values[0] = cfg.primary_wavelength;
    } else {
        const std::size_t count = std::min(base.wavelengths.size(),
                                           static_cast<std::size_t>(kMaxDeviceWaves));
        for (std::size_t i = 0; i < count; ++i) {
            cfg.wavelength_values[i] = base.wavelengths[i].value_um;
        }
    }
    return cfg;
}
} // namespace
} // namespace optiland
