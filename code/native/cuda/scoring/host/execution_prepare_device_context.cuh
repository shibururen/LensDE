#pragma once
#include "../prelude.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/calibration_tasks.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/optical_records.cuh"
#include "../contracts/spot_wavefront_records.cuh"
#include "execution_context.cuh"
#include "../merit/first_order_constraints.cuh"
#include "../merit/supported_ray_generation.cuh"
#include "../merit/unified_residuals.cuh"

namespace optiland {
native_detail::PhaseOutcome PopulationScoringExecution::prepare_device_context() {
    d_surfaces = nullptr;

    d_variables = nullptr;

    d_glass_candidates = nullptr;

    d_fields = nullptr;

    d_waves = nullptr;

    d_material_indices = nullptr;

    d_glass_proximity = nullptr;

    d_glass_proximity_points = nullptr;

    d_population = nullptr;

    d_candidate_surfaces = nullptr;

    d_candidate_fields = nullptr;

    d_aperture_max_heights = nullptr;

    d_meridional_boundary_heights = nullptr;

    d_calibrated_models = nullptr;

    d_evaluation_certificate = nullptr;

    d_builder_diagnostic = nullptr;

    builder_csv_path = std::getenv("OPTILAND_BUILDER_DIAGNOSTIC_CSV");

    edge_path_mode = std::getenv("OPTILAND_EDGE_PATH_MODE") ? std::atoi(std::getenv("OPTILAND_EDGE_PATH_MODE")) : 1961;

    edge_path_shadow = std::getenv("OPTILAND_EDGE_PATH_SHADOW") ? std::atoi(std::getenv("OPTILAND_EDGE_PATH_SHADOW")) : 0;

    cudaMemcpyToSymbol(g_edge_path_mode, &edge_path_mode, sizeof(edge_path_mode));

    cudaMemcpyToSymbol(g_edge_path_shadow, &edge_path_shadow, sizeof(edge_path_shadow));

    builder_diag_level = builder_csv_path ? (std::getenv("OPTILAND_BUILDER_DIAGNOSTIC_LEVEL") ? std::atoi(std::getenv("OPTILAND_BUILDER_DIAGNOSTIC_LEVEL")) : 2) : 0;

    d_spot_ray_samples = nullptr;

    d_pupil_order_heights = nullptr;

    d_wavefront_guard_samples = nullptr;

    d_spot_tail_extra_edge_samples = nullptr;

    d_chief_ray_samples = nullptr;

    d_field_spot_stats = nullptr;

    d_field_wavefront_stats = nullptr;

    d_merit_terms = nullptr;

    d_summaries = nullptr;

    d_constraint_debug = nullptr;

    d_asphere_root_profile = nullptr;

    host_asphere_root_profile = {};

    root_profile_after_geometry = {};

    root_profile_after_initial_models = {};

    root_profile_after_aperture = {};

    root_profile_after_final_models = {};

    d_asphere_feasibility_profile = nullptr;

    host_asphere_feasibility_profile = {};

    d_asphere_deep_profile = nullptr;

    host_asphere_deep_profile = {};

    d_asphere_uc5_profile = nullptr;

    host_asphere_uc5_profile = {};

    d_uc5_candidate_states = nullptr;

    surface_bytes = host_surfaces.size() * sizeof(DeviceSurface);

    variable_bytes = host_variables.size() * sizeof(DeviceVariable);

    glass_candidate_bytes = host_glass_candidates.size() * sizeof(DeviceGlassCandidate);

    field_bytes = host_fields.size() * sizeof(DeviceField);

    wave_bytes = host_waves.size() * sizeof(DeviceWave);

    material_index_bytes = host_material_indices.size() * sizeof(double);

    glass_proximity_bytes = host_glass_proximity.size() * sizeof(DeviceGlassProximity);

    glass_proximity_point_bytes = host_glass_proximity_points.size() * sizeof(DeviceGlassProximityPoint);

    population_bytes = static_cast<std::size_t>(population_size) * variables.size() * sizeof(double);

    candidate_surface_bytes = static_cast<std::size_t>(population_size) * host_surfaces.size() *
        sizeof(DeviceSurface);

    candidate_field_bytes = static_cast<std::size_t>(population_size) * host_fields.size() *
        sizeof(DeviceField);

    has_candidate_field_variables = std::any_of(
        variables.begin(),
        variables.end(),
        [](const VariableSpec& variable) {
            return variable.kind == VariableKind::VignetteX ||
                   variable.kind == VariableKind::VignetteY ||
                   variable.kind == VariableKind::VignetteCenterY ||
                   variable.kind == VariableKind::VignetteMagnitudeY ||
                   variable.kind == VariableKind::VignetteAsymmetryY ||
                   variable.kind == VariableKind::EvaluatorFieldHx ||
                   variable.kind == VariableKind::EvaluatorFieldHy ||
                   variable.kind == VariableKind::EvaluatorVignetteCenterX;
        });

    aperture_max_heights_bytes = static_cast<std::size_t>(population_size) * host_surfaces.size() *
        sizeof(double);

    meridional_boundary_heights_bytes = static_cast<std::size_t>(population_size) * host_fields.size() *
        host_waves.size() * kMeridionalSupportTraceCount * host_surfaces.size() *
        sizeof(double);

    calibrated_model_count = static_cast<std::size_t>(population_size) * host_fields.size() *
        host_waves.size();

    calibrated_model_bytes = calibrated_model_count * sizeof(DeviceCalibratedModel);

    uc5_candidate_state_bytes = static_cast<std::size_t>(population_size) *
        sizeof(DeviceUC5CandidateFamilyState);

    field_spot_stats_bytes = static_cast<std::size_t>(population_size) * host_fields.size() *
        sizeof(DeviceFieldSpotStats);

    codev_pupil_count = cfg.pupil_sampling_type == kPupilSamplingCodevRectangular
            ? static_cast<int>(make_codev_rectangular_distribution(
                  cfg.codev_ray_interval,
                  cfg.codev_obscuration,
                  cfg.codev_spot_aperture_weight).size())
            : 0;

    base_spot_pupil_count = cfg.pupil_sampling_type == kPupilSamplingCodevRectangular
            ? codev_pupil_count
            : std::max(cfg.num_rays, 1) * std::max(cfg.pupil_arms, 1);

    elevated_spot_pupil_count = cfg.pupil_sampling_type != kPupilSamplingCodevRectangular &&
                cfg.elevated_field_num_rays > 0 &&
                cfg.elevated_field_pupil_arms > 0
            ? cfg.elevated_field_num_rays * cfg.elevated_field_pupil_arms
            : 0;

    spot_pupil_count = std::max(base_spot_pupil_count, elevated_spot_pupil_count);

    spot_ray_sample_count = static_cast<std::size_t>(population_size) * host_fields.size() *
        host_waves.size() * static_cast<std::size_t>(spot_pupil_count);

    spot_ray_samples_bytes = spot_ray_sample_count * sizeof(DeviceSpotRaySample);

    pupil_order_heights_bytes = static_cast<std::size_t>(population_size) * host_fields.size() *
        static_cast<std::size_t>(spot_pupil_count) * host_surfaces.size() *
        sizeof(double);

    wavefront_guard_sample_count = static_cast<std::size_t>(population_size) * host_fields.size() *
        host_waves.size() * 4;

    wavefront_guard_samples_bytes = wavefront_guard_sample_count * sizeof(DeviceSpotRaySample);

    spot_tail_extra_edge_sample_count = static_cast<std::size_t>(population_size) * host_fields.size() *
        host_waves.size() * kSpotTailExtraEdgeCount;

    spot_tail_extra_edge_samples_bytes = spot_tail_extra_edge_sample_count * sizeof(DeviceSpotRaySample);

    chief_ray_sample_count = static_cast<std::size_t>(population_size) * host_fields.size() *
        host_waves.size();

    chief_ray_samples_bytes = chief_ray_sample_count * sizeof(DeviceChiefRaySample);

    field_wavefront_stats_bytes = static_cast<std::size_t>(population_size) * host_fields.size() *
        sizeof(DeviceFieldWavefrontStats);

    merit_terms_bytes = static_cast<std::size_t>(population_size) * sizeof(DeviceMeritTerms);

    summary_bytes = static_cast<std::size_t>(population_size) * sizeof(CudaScoreSummary);

    constraint_debug_bytes = host_surfaces.size() * kConstraintDebugStride * sizeof(double);

    host_constraint_debug = std::vector<double>(
        constraint_debug_enabled
            ? host_surfaces.size() * kConstraintDebugStride
            : 0,
        NAN);

    host_trace_debug_fields = std::vector<DeviceFieldSpotStats>(
        trace_debug_enabled ? host_fields.size() : 0);

    host_trace_debug_wavefront_fields = std::vector<DeviceFieldWavefrontStats>(
        trace_debug_enabled &&
                (cfg.operand_rms_wavefront ||
                 cfg.operand_single_ray_wavefront_guard)
            ? host_fields.size()
            : 0);

    host_trace_debug_samples = std::vector<DeviceSpotRaySample>(
        trace_debug_enabled
            ? host_fields.size() * host_waves.size() *
                  static_cast<std::size_t>(spot_pupil_count)
            : 0);

    host_trace_debug_models = std::vector<DeviceCalibratedModel>(
        trace_debug_enabled && cfg.aiming_mode == 5
            ? host_fields.size() * host_waves.size()
            : 0);

    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace optiland
