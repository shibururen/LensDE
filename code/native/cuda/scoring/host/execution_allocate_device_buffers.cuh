#pragma once
#include "../prelude.cuh"
#include "../configuration/field_upload.cuh"
#include "../configuration/runtime_options.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/optical_records.cuh"
#include "../diagnostics/asphere_profile_records.cuh"
#include "../diagnostics/builder_instrumentation.cuh"
#include "certificate_context.cuh"
#include "execution_context.cuh"

namespace optiland {
native_detail::PhaseOutcome PopulationScoringExecution::allocate_device_buffers() {
    ok = cudaMalloc(&d_surfaces, surface_bytes) == cudaSuccess &&
        cudaMalloc(&d_variables, variable_bytes) == cudaSuccess &&
        cudaMalloc(&d_fields, field_bytes) == cudaSuccess &&
        cudaMalloc(&d_waves, wave_bytes) == cudaSuccess &&
        cudaMalloc(&d_material_indices, material_index_bytes) == cudaSuccess &&
        cudaMalloc(&d_population, population_bytes) == cudaSuccess &&
        cudaMalloc(&d_candidate_surfaces, candidate_surface_bytes) == cudaSuccess &&
        cudaMalloc(&d_field_spot_stats, field_spot_stats_bytes) == cudaSuccess &&
        cudaMalloc(&d_merit_terms, merit_terms_bytes) == cudaSuccess &&
        cudaMalloc(&d_summaries, summary_bytes) == cudaSuccess;

    if (ok && has_candidate_field_variables) {
        ok = cudaMalloc(
                 &d_candidate_fields,
                 candidate_field_bytes) == cudaSuccess;
    }

    if (ok) {
        cfg.material_indices = d_material_indices;
        cfg.candidate_fields = d_candidate_fields;
    }

    uc5_symmetry_flag = uc5_symmetry_enabled ? 1 : 0;

    uc5_primary_abort_flag = (uc5_primary_abort_enabled || (edge_path_mode & 128)) ? 1 : 0;

    uc5_candidate_batch_flag = uc5_candidate_batch_enabled ? 1 : 0;

    null_uc5_profile = nullptr;

    if (ok) {
        ok = cudaMemcpyToSymbol(
                 g_asphere_uc5_symmetry_enabled,
                 &uc5_symmetry_flag,
                 sizeof(uc5_symmetry_flag)) == cudaSuccess &&
             cudaMemcpyToSymbol(
                 g_asphere_uc5_primary_abort_enabled,
                 &uc5_primary_abort_flag,
                 sizeof(uc5_primary_abort_flag)) == cudaSuccess &&
             cudaMemcpyToSymbol(
                 g_asphere_uc5_candidate_batch_enabled,
                 &uc5_candidate_batch_flag,
                 sizeof(uc5_candidate_batch_flag)) == cudaSuccess &&
             cudaMemcpyToSymbol(
                 g_asphere_uc5_profile,
                 &null_uc5_profile,
                 sizeof(null_uc5_profile)) == cudaSuccess;
    }

    if (ok && asphere_root_profile_enabled) {
        ok = cudaMalloc(
                 &d_asphere_root_profile,
                 sizeof(DeviceAsphereRootProfile)) == cudaSuccess;
        if (ok) {
            cfg.asphere_root_profile = d_asphere_root_profile;
        }
    }

    if (ok && asphere_feasibility_profile_enabled) {
        ok = cudaMalloc(
                 &d_asphere_feasibility_profile,
                 sizeof(DeviceAsphereFeasibilityProfile)) == cudaSuccess;
        if (ok) {
            cfg.asphere_feasibility_profile = d_asphere_feasibility_profile;
        }
    }

    if (ok && asphere_deep_profile_enabled) {
        ok = cudaMalloc(
                 &d_asphere_deep_profile,
                 sizeof(DeviceAsphereDeepProfile)) == cudaSuccess;
        if (ok) {
            cfg.asphere_deep_profile = d_asphere_deep_profile;
        }
    }

    if (ok && asphere_uc5_profile_enabled) {
        ok = cudaMalloc(
                 &d_asphere_uc5_profile,
                 sizeof(DeviceAsphereUC5Profile)) == cudaSuccess;
        if (ok) {
            ok = cudaMemcpyToSymbol(
                     g_asphere_uc5_profile,
                     &d_asphere_uc5_profile,
                     sizeof(d_asphere_uc5_profile)) == cudaSuccess;
        }
    }

    calibrated_model_cache = cfg.aiming_mode == 5;

    reuse_pre_aperture_models = calibrated_model_cache &&
        !env_flag_enabled("OPTILAND_DE_CUDA_REBUILD_POST_APERTURE_MODELS");

    evaluation_device_mode = 0;

    evaluation_ordered = std::getenv("OPTILAND_DE_CUDA_CALIBRATED_ORDERED_TILE8");

    evaluation_supported = evaluation_diagnostics_safe() && evaluation_context_mode > 0 &&
        evaluation_context_records.size() == static_cast<std::size_t>(population_size) &&
        reuse_pre_aperture_models && !enable_calibrated_safeguarded_recovery &&
        !uc5_combined && !uc5_symmetry_enabled && !uc5_primary_abort_enabled && !uc5_candidate_batch_enabled &&
        asphere_root_policy() == AsphereRootPolicy::UnifiedCertifiedExperimental &&
        (!evaluation_ordered || !env_flag_enabled("OPTILAND_DE_CUDA_CALIBRATED_ORDERED_TILE8")) &&
        cfg.pupil_sampling_type == kPupilSamplingGaussianRingArm &&
        std::isfinite(cfg.min_field_coverage) && std::isfinite(evaluation_context_epsilon) && evaluation_context_epsilon >= 0.0;

    if (ok && evaluation_supported) {
        const std::size_t bytes = evaluation_context_records.size() * sizeof(EvaluationCertificate);
        ok = cudaMalloc(&d_evaluation_certificate, bytes) == cudaSuccess;
        if (ok) ok = cudaMemcpy(d_evaluation_certificate, evaluation_context_records.data(), bytes, cudaMemcpyHostToDevice) == cudaSuccess;
        if (ok) evaluation_device_mode = evaluation_context_mode;
    }

    if (ok) ok = cudaMemcpyToSymbol(g_evaluation_certificate, &d_evaluation_certificate, sizeof(d_evaluation_certificate)) == cudaSuccess &&
    cudaMemcpyToSymbol(g_evaluation_certificate_mode, &evaluation_device_mode, sizeof(evaluation_device_mode)) == cudaSuccess;

    parallel_ray_trace = cfg.aiming_mode == 1 || calibrated_model_cache;

    parallel_aperture_trace = cfg.dynamic_apertures && parallel_ray_trace;

    needs_vignetting_boundary_support = parallel_aperture_trace &&
        std::isfinite(cfg.minimum_relative_illumination) &&
        cfg.minimum_relative_illumination > 0.0 &&
        cfg.minimum_relative_illumination <= 1.0 &&
        cfg.vignetting_boundary_support_weight > 0.0;

    parallel_spot_trace = parallel_ray_trace;

    fuse_merit_aperture_envelope = parallel_aperture_trace && parallel_spot_trace &&
        !cfg.primary_wavelength_apertures &&
        cfg.vignetting_merit_aperture_envelope_experimental != 0 &&
        reuse_pre_aperture_models &&
        !env_flag_enabled("OPTILAND_DE_CUDA_DISABLE_MERIT_APERTURE_FUSION");

    needs_pupil_order_history = parallel_spot_trace && cfg.pupil_order_prior_weight > 0.0;

    needs_wavefront = cfg.operand_rms_wavefront || cfg.operand_single_ray_wavefront_guard;

    needs_single_ray_spot_guard = cfg.operand_single_ray_spot_guard || cfg.operand_single_ray_spot_tail;

    needs_working_f_number = cfg.operand_working_f_number != 0;

    needs_selective_spot_tail_edges = cfg.operand_single_ray_spot_tail &&
        std::any_of(
            host_fields.begin(),
            host_fields.end(),
            [&](const DeviceField& field) {
                return std::hypot(field.hx, field.hy) + 1.0e-12 >=
                       cfg.single_ray_spot_tail_extra_edge_min_field_radius;
            });

    track_unused_opd = env_flag_enabled("OPTILAND_DE_CUDA_TRACK_UNUSED_OPD");

    needs_chief_samples = cfg.require_chief_ray || cfg.reject_ray_path_structural_failures ||
        needs_wavefront || needs_working_f_number ||
        cfg.operand_max_chief_ray_angle || cfg.operand_max_distortion ||
        cfg.operand_lateral_color;

    aperture_y_guards_unrestricted = true;

    for (const DeviceSurface& surface : host_surfaces) {
        if (!surface.is_stop &&
            (surface.clear_aperture_fixed || surface.aperture_radius > 0.0)) {
            aperture_y_guards_unrestricted = false;
            break;
        }
    }

    reuse_aperture_y_guards = parallel_aperture_trace && needs_wavefront &&
        aperture_y_guards_unrestricted &&
        !env_flag_enabled("OPTILAND_DE_CUDA_DISABLE_APERTURE_GUARD_REUSE");

    calibrated_center_field_index = host_center_field_index(host_fields);

    if (ok && parallel_aperture_trace) {
        ok = cudaMalloc(
                 &d_aperture_max_heights,
                 aperture_max_heights_bytes) == cudaSuccess;
    }

    if (ok &&
        (needs_vignetting_boundary_support ||
         (parallel_aperture_trace && needs_pupil_order_history))) {
        ok = cudaMalloc(
                 &d_meridional_boundary_heights,
                 meridional_boundary_heights_bytes) == cudaSuccess;
    }

    if (ok && calibrated_model_cache) {
        ok = cudaMalloc(
                 &d_calibrated_models,
                 calibrated_model_bytes) == cudaSuccess;
        if (ok) {
            ok = cudaMemset(
                     d_calibrated_models,
                     0,
                     calibrated_model_bytes) == cudaSuccess;
        }
    }

    if (ok) {
        const unsigned long long count = calibrated_model_count;
        if (builder_diag_level > 0 && calibrated_model_cache) {
            ok = cudaMalloc(&d_builder_diagnostic, 2 * count * sizeof(DeviceBuilderDiagnostic)) == cudaSuccess;
            if (ok) ok = cudaMemset(d_builder_diagnostic, 0, 2 * count * sizeof(DeviceBuilderDiagnostic)) == cudaSuccess;
        }
        if (ok) ok = cudaMemcpyToSymbol(g_builder_diagnostic, &d_builder_diagnostic, sizeof(d_builder_diagnostic)) == cudaSuccess &&
            cudaMemcpyToSymbol(g_builder_model_base, &d_calibrated_models, sizeof(d_calibrated_models)) == cudaSuccess &&
            cudaMemcpyToSymbol(g_builder_model_count, &count, sizeof(count)) == cudaSuccess &&
            cudaMemcpyToSymbol(g_builder_diagnostic_level, &builder_diag_level, sizeof(builder_diag_level)) == cudaSuccess;
    }

    if (ok && calibrated_model_cache && uc5_candidate_batch_enabled) {
        ok = cudaMalloc(
                 &d_uc5_candidate_states,
                 uc5_candidate_state_bytes) == cudaSuccess;
    }

    if (ok && parallel_spot_trace) {
        ok = cudaMalloc(
                 &d_spot_ray_samples,
                 spot_ray_samples_bytes) == cudaSuccess;
    }

    if (ok && needs_pupil_order_history) {
        ok = cudaMalloc(
                 &d_pupil_order_heights,
                 pupil_order_heights_bytes) == cudaSuccess;
    }

    if (ok && parallel_spot_trace && needs_chief_samples) {
        ok = cudaMalloc(
                 &d_chief_ray_samples,
                 chief_ray_samples_bytes) == cudaSuccess;
    }

    if (ok && parallel_spot_trace &&
        (needs_wavefront || needs_single_ray_spot_guard ||
         needs_working_f_number)) {
        ok = cudaMalloc(
                 &d_wavefront_guard_samples,
                 wavefront_guard_samples_bytes) == cudaSuccess;
    }

    if (ok && parallel_spot_trace && needs_selective_spot_tail_edges) {
        ok = cudaMalloc(
                 &d_spot_tail_extra_edge_samples,
                 spot_tail_extra_edge_samples_bytes) == cudaSuccess;
    }

    if (ok && parallel_spot_trace && needs_wavefront) {
        ok = cudaMalloc(
                 &d_field_wavefront_stats,
                 field_wavefront_stats_bytes) == cudaSuccess;
    }

    if (ok && !host_glass_proximity.empty()) {
        ok = cudaMalloc(&d_glass_proximity, glass_proximity_bytes) == cudaSuccess;
    }

    if (ok && !host_glass_candidates.empty()) {
        ok = cudaMalloc(&d_glass_candidates, glass_candidate_bytes) == cudaSuccess;
    }

    if (ok && !host_glass_proximity_points.empty()) {
        ok = cudaMalloc(&d_glass_proximity_points, glass_proximity_point_bytes) ==
             cudaSuccess;
    }

    if (ok && constraint_debug_enabled) {
        ok = cudaMalloc(&d_constraint_debug, constraint_debug_bytes) == cudaSuccess;
    }

    if (memory_debug_enabled) {
        cudaMemGetInfo(&memory_free_after_alloc, &memory_total);
    }

    cudaEventRecord(after_setup);

    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace optiland
