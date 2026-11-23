#pragma once
#include "../prelude.cuh"
#include "../configuration/runtime_options.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/calibration_tasks.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../contracts/spot_wavefront_records.cuh"
#include "../diagnostics/calibrated_probe_run.cuh"
#include "../diagnostics/chief_seed_probe.cuh"
#include "certificate_context.cuh"
#include "../kernels/aperture_preparation.cuh"
#include "../kernels/geometry_preparation.cuh"
#include "../kernels/merit_reduction.cuh"
#include "../kernels/spot_reduction.cuh"
#include "../kernels/spot_trace.cuh"
#include "../kernels/wavefront_trace.cuh"
#include "../launch/model_pipeline.cuh"
#include "../merit/first_order_constraints.cuh"
#include "../merit/supported_ray_generation.cuh"
#include "../merit/unified_residuals.cuh"
#include "../merit/unified_score.cuh"

namespace optiland {
template <class Capture_cuda_status_ok>
struct LaunchPopulationScoringPipelineInputs {
    const std::vector<VariableSpec>& variables;
    int& population_size;
    const bool& stage_timing_enabled;
    cudaEvent_t& after_geometry;
    cudaEvent_t& after_initial_models;
    cudaEvent_t& after_aperture;
    cudaEvent_t& after_final_models;
    cudaEvent_t& after_spot_trace;
    cudaEvent_t& after_chief_trace;
    cudaEvent_t& after_spot_reduce;
    cudaEvent_t& after_wavefront_guard;
    cudaEvent_t& after_wavefront_stats;
    cudaEvent_t& after_spot;
    cudaEvent_t& after_merit;
    DeviceConfig& cfg;
    const bool& uc5_candidate_batch_enabled;
    const bool& asphere_feasibility_profile_enabled;
    const bool& asphere_deep_profile_enabled;
    AsphereFeasibilityHostTiming& asphere_feasibility_timing;
    const bool& transpose_ray_tasks;
    int& constraint_debug_row;
    const bool& constraint_debug_enabled;
    const bool& enable_calibrated_safeguarded_recovery;
    Capture_cuda_status_ok& cuda_status_ok;
    DeviceSurface*& d_surfaces;
    DeviceVariable*& d_variables;
    DeviceGlassCandidate*& d_glass_candidates;
    DeviceField*& d_fields;
    DeviceWave*& d_waves;
    DeviceGlassProximity*& d_glass_proximity;
    DeviceGlassProximityPoint*& d_glass_proximity_points;
    double*& d_population;
    DeviceSurface*& d_candidate_surfaces;
    DeviceField*& d_candidate_fields;
    double*& d_aperture_max_heights;
    double*& d_meridional_boundary_heights;
    DeviceCalibratedModel*& d_calibrated_models;
    DeviceSpotRaySample*& d_spot_ray_samples;
    double*& d_pupil_order_heights;
    DeviceSpotRaySample*& d_wavefront_guard_samples;
    DeviceSpotRaySample*& d_spot_tail_extra_edge_samples;
    DeviceChiefRaySample*& d_chief_ray_samples;
    DeviceFieldSpotStats*& d_field_spot_stats;
    DeviceFieldWavefrontStats*& d_field_wavefront_stats;
    DeviceMeritTerms*& d_merit_terms;
    CudaScoreSummary*& d_summaries;
    double*& d_constraint_debug;
    DeviceAsphereRootProfile*& d_asphere_root_profile;
    DeviceAsphereRootProfile& root_profile_after_geometry;
    DeviceAsphereRootProfile& root_profile_after_initial_models;
    DeviceAsphereRootProfile& root_profile_after_aperture;
    DeviceAsphereRootProfile& root_profile_after_final_models;
    DeviceUC5CandidateFamilyState*& d_uc5_candidate_states;
    const int& spot_pupil_count;
    const std::size_t& spot_ray_sample_count;
    const std::size_t& wavefront_guard_sample_count;
    const std::size_t& spot_tail_extra_edge_sample_count;
    const std::size_t& chief_ray_sample_count;
    bool& ok;
    const bool& calibrated_model_cache;
    const bool& reuse_pre_aperture_models;
    const bool& parallel_aperture_trace;
    const bool& needs_vignetting_boundary_support;
    const bool& parallel_spot_trace;
    const bool& fuse_merit_aperture_envelope;
    const bool& needs_wavefront;
    const bool& needs_single_ray_spot_guard;
    const bool& needs_working_f_number;
    const bool& needs_selective_spot_tail_edges;
    const bool& track_unused_opd;
    const bool& needs_chief_samples;
    const bool& reuse_aperture_y_guards;
    const int& calibrated_center_field_index;
};
} // namespace optiland

namespace optiland {
template <class Inputs>
inline native_detail::PhaseOutcome launch_population_scoring_pipeline(Inputs& inputs) {
    auto& variables = inputs.variables;
    auto& population_size = inputs.population_size;
    auto& stage_timing_enabled = inputs.stage_timing_enabled;
    auto& after_geometry = inputs.after_geometry;
    auto& after_initial_models = inputs.after_initial_models;
    auto& after_aperture = inputs.after_aperture;
    auto& after_final_models = inputs.after_final_models;
    auto& after_spot_trace = inputs.after_spot_trace;
    auto& after_chief_trace = inputs.after_chief_trace;
    auto& after_spot_reduce = inputs.after_spot_reduce;
    auto& after_wavefront_guard = inputs.after_wavefront_guard;
    auto& after_wavefront_stats = inputs.after_wavefront_stats;
    auto& after_spot = inputs.after_spot;
    auto& after_merit = inputs.after_merit;
    auto& cfg = inputs.cfg;
    auto& uc5_candidate_batch_enabled = inputs.uc5_candidate_batch_enabled;
    auto& asphere_feasibility_profile_enabled = inputs.asphere_feasibility_profile_enabled;
    auto& asphere_deep_profile_enabled = inputs.asphere_deep_profile_enabled;
    auto& asphere_feasibility_timing = inputs.asphere_feasibility_timing;
    auto& transpose_ray_tasks = inputs.transpose_ray_tasks;
    auto& constraint_debug_row = inputs.constraint_debug_row;
    auto& constraint_debug_enabled = inputs.constraint_debug_enabled;
    auto& enable_calibrated_safeguarded_recovery = inputs.enable_calibrated_safeguarded_recovery;
    auto& cuda_status_ok = inputs.cuda_status_ok;
    auto& d_surfaces = inputs.d_surfaces;
    auto& d_variables = inputs.d_variables;
    auto& d_glass_candidates = inputs.d_glass_candidates;
    auto& d_fields = inputs.d_fields;
    auto& d_waves = inputs.d_waves;
    auto& d_glass_proximity = inputs.d_glass_proximity;
    auto& d_glass_proximity_points = inputs.d_glass_proximity_points;
    auto& d_population = inputs.d_population;
    auto& d_candidate_surfaces = inputs.d_candidate_surfaces;
    auto& d_candidate_fields = inputs.d_candidate_fields;
    auto& d_aperture_max_heights = inputs.d_aperture_max_heights;
    auto& d_meridional_boundary_heights = inputs.d_meridional_boundary_heights;
    auto& d_calibrated_models = inputs.d_calibrated_models;
    auto& d_spot_ray_samples = inputs.d_spot_ray_samples;
    auto& d_pupil_order_heights = inputs.d_pupil_order_heights;
    auto& d_wavefront_guard_samples = inputs.d_wavefront_guard_samples;
    auto& d_spot_tail_extra_edge_samples = inputs.d_spot_tail_extra_edge_samples;
    auto& d_chief_ray_samples = inputs.d_chief_ray_samples;
    auto& d_field_spot_stats = inputs.d_field_spot_stats;
    auto& d_field_wavefront_stats = inputs.d_field_wavefront_stats;
    auto& d_merit_terms = inputs.d_merit_terms;
    auto& d_summaries = inputs.d_summaries;
    auto& d_constraint_debug = inputs.d_constraint_debug;
    auto& d_asphere_root_profile = inputs.d_asphere_root_profile;
    auto& root_profile_after_geometry = inputs.root_profile_after_geometry;
    auto& root_profile_after_initial_models = inputs.root_profile_after_initial_models;
    auto& root_profile_after_aperture = inputs.root_profile_after_aperture;
    auto& root_profile_after_final_models = inputs.root_profile_after_final_models;
    auto& d_uc5_candidate_states = inputs.d_uc5_candidate_states;
    auto& spot_pupil_count = inputs.spot_pupil_count;
    auto& spot_ray_sample_count = inputs.spot_ray_sample_count;
    auto& wavefront_guard_sample_count = inputs.wavefront_guard_sample_count;
    auto& spot_tail_extra_edge_sample_count = inputs.spot_tail_extra_edge_sample_count;
    auto& chief_ray_sample_count = inputs.chief_ray_sample_count;
    auto& ok = inputs.ok;
    auto& calibrated_model_cache = inputs.calibrated_model_cache;
    auto& reuse_pre_aperture_models = inputs.reuse_pre_aperture_models;
    auto& parallel_aperture_trace = inputs.parallel_aperture_trace;
    auto& needs_vignetting_boundary_support = inputs.needs_vignetting_boundary_support;
    auto& parallel_spot_trace = inputs.parallel_spot_trace;
    auto& fuse_merit_aperture_envelope = inputs.fuse_merit_aperture_envelope;
    auto& needs_wavefront = inputs.needs_wavefront;
    auto& needs_single_ray_spot_guard = inputs.needs_single_ray_spot_guard;
    auto& needs_working_f_number = inputs.needs_working_f_number;
    auto& needs_selective_spot_tail_edges = inputs.needs_selective_spot_tail_edges;
    auto& track_unused_opd = inputs.track_unused_opd;
    auto& needs_chief_samples = inputs.needs_chief_samples;
    auto& reuse_aperture_y_guards = inputs.reuse_aperture_y_guards;
    auto& calibrated_center_field_index = inputs.calibrated_center_field_index;
    if (ok) {
        const int block = de_cuda_general_block_size();
        const int model_block = de_cuda_calibrated_model_block_size();
        const int grid = (population_size + block - 1) / block;
        DeviceConfig slope_domain_cfg = cfg;
        if (asphere_deep_profile_enabled &&
            !slope_domain_cfg.asphere_slope_root_domain_enabled) {
            slope_domain_cfg.asphere_slope_root_domain_enabled = 1;
            slope_domain_cfg.asphere_slope_root_domain_min_tangent_deg =
                kAsphereRootDomainMinimumTangentAxisAngleDeg;
            slope_domain_cfg.asphere_slope_root_domain_reuse = 0;
        }
        const bool parallel_slope_domains =
            cfg.asphere_slope_root_domain_enabled != 0;
        const bool materialize_slope_domains =
            parallel_slope_domains || asphere_deep_profile_enabled;
        prepare_geometry_kernel<<<grid, block>>>(
            d_surfaces,
            d_variables,
            d_glass_candidates,
            d_fields,
            d_waves,
            cfg,
            d_population,
            population_size,
            static_cast<int>(variables.size()),
            d_candidate_surfaces,
            d_candidate_fields,
            parallel_slope_domains ? 1 : 0);
        ok = cuda_status_ok(cudaGetLastError());
        const int domain_item_count = population_size * cfg.surface_count;
        const int domain_grid = (domain_item_count + block - 1) / block;
        if (ok && materialize_slope_domains) {
            cudaEvent_t domain_start = nullptr;
            cudaEvent_t domain_stop = nullptr;
            const bool timer_started = begin_diagnostic_cuda_timer(
                asphere_feasibility_profile_enabled,
                &domain_start,
                &domain_stop);
            bool launch_ok = false;
            bool timer_ok = false;
            if (timer_started) {
                materialize_asphere_slope_domains_kernel<<<domain_grid, block>>>(
                    d_candidate_surfaces, slope_domain_cfg, population_size);
                launch_ok = cuda_status_ok(cudaGetLastError());
                timer_ok = finish_diagnostic_cuda_timer(
                    asphere_feasibility_profile_enabled,
                    domain_start,
                    domain_stop,
                    &asphere_feasibility_timing.domain_seconds);
                if (asphere_feasibility_profile_enabled) {
                    ++asphere_feasibility_timing.domain_kernel_launches;
                }
            }
            ok = timer_started && launch_ok && timer_ok;
        }
        if (ok && parallel_slope_domains) {
            prepare_geometry_kernel<<<grid, block>>>(
                d_surfaces,
                d_variables,
                d_glass_candidates,
                d_fields,
                d_waves,
                cfg,
                d_population,
                population_size,
                static_cast<int>(variables.size()),
                d_candidate_surfaces,
                d_candidate_fields,
                2);
            ok = cuda_status_ok(cudaGetLastError());
        }
        if (ok && parallel_slope_domains) {
            cudaEvent_t domain_start = nullptr;
            cudaEvent_t domain_stop = nullptr;
            const bool timer_started = begin_diagnostic_cuda_timer(
                asphere_feasibility_profile_enabled,
                &domain_start,
                &domain_stop);
            bool launch_ok = false;
            bool timer_ok = false;
            if (timer_started) {
                materialize_asphere_slope_domains_kernel<<<domain_grid, block>>>(
                    d_candidate_surfaces, slope_domain_cfg, population_size);
                launch_ok = cuda_status_ok(cudaGetLastError());
                timer_ok = finish_diagnostic_cuda_timer(
                    asphere_feasibility_profile_enabled,
                    domain_start,
                    domain_stop,
                    &asphere_feasibility_timing.domain_seconds);
                if (asphere_feasibility_profile_enabled) {
                    ++asphere_feasibility_timing.domain_kernel_launches;
                }
            }
            ok = timer_started && launch_ok && timer_ok;
        }
        if (ok && stage_timing_enabled) {
            cudaEventRecord(after_geometry);
        }
        if (ok && asphere_feasibility_profile_enabled) {
            ok = cudaMemcpy(
                     &root_profile_after_geometry,
                     d_asphere_root_profile,
                     sizeof(DeviceAsphereRootProfile),
                     cudaMemcpyDeviceToHost) == cudaSuccess;
        }
        if (ok && calibrated_model_cache) {
            ok = launch_calibrated_model_build(
                d_candidate_surfaces,
                d_fields,
                d_waves,
                cfg,
                population_size,
                calibrated_center_field_index,
                d_calibrated_models,
                d_uc5_candidate_states,
                model_block,
                enable_calibrated_safeguarded_recovery,
                uc5_candidate_batch_enabled,
                asphere_feasibility_profile_enabled
                    ? &asphere_feasibility_timing
                    : nullptr);
        }
        if (ok && calibrated_model_cache &&
            env_flag_enabled("OPTILAND_DE_CUDA_COOPERATIVE_PROBE_DIAGNOSTIC")) {
            ok = run_calibrated_probe_diagnostic(
                d_candidate_surfaces,
                d_fields,
                d_waves,
                d_calibrated_models,
                cfg,
                population_size);
        }
        if (ok && calibrated_model_cache &&
            env_flag_enabled("OPTILAND_DE_CUDA_MIXED_PRECISION_PROBE_DIAGNOSTIC")) {
            ok = run_calibrated_precision_probe_diagnostic(
                d_candidate_surfaces,
                d_fields,
                d_waves,
                d_calibrated_models,
                cfg,
                population_size);
        }
        if (ok && calibrated_model_cache &&
            env_flag_enabled("OPTILAND_DE_CUDA_EDGE_REPLAY_DIAGNOSTIC")) {
            ok = run_calibrated_edge_replay_diagnostic(
                d_candidate_surfaces,
                d_fields,
                d_waves,
                d_calibrated_models,
                cfg,
                population_size,
                calibrated_center_field_index);
        }
        if (ok && calibrated_model_cache &&
            env_flag_enabled("OPTILAND_DE_CUDA_CHIEF_SEED0_DIAGNOSTIC")) {
            ok = run_calibrated_chief_seed0_diagnostic(
                d_candidate_surfaces,
                d_fields,
                d_waves,
                d_calibrated_models,
                cfg,
                population_size,
                calibrated_center_field_index);
        }
        if (ok && stage_timing_enabled) {
            cudaEventRecord(after_initial_models);
        }
        if (ok && asphere_feasibility_profile_enabled) {
            ok = cudaMemcpy(
                     &root_profile_after_initial_models,
                     d_asphere_root_profile,
                     sizeof(DeviceAsphereRootProfile),
                     cudaMemcpyDeviceToHost) == cudaSuccess;
        }
        if (ok && asphere_deep_profile_enabled &&
            !parallel_slope_domains) {
            // The finite domain exists only so the initial-model shadow can
            // replay U1. Restore the exact P0 surface state before aperture
            // and merit tracing, where first-root paths would otherwise see
            // the diagnostic-only bound.
            materialize_asphere_slope_domains_kernel<<<domain_grid, block>>>(
                d_candidate_surfaces, cfg, population_size);
            ok = cuda_status_ok(cudaGetLastError());
        }
        if (ok && parallel_aperture_trace) {
            if (fuse_merit_aperture_envelope) {
                const int ray_grid =
                    (static_cast<int>(spot_ray_sample_count) + block - 1) /
                    block;
                trace_paraxial_spot_rays_kernel<<<ray_grid, block>>>(
                    d_candidate_surfaces,
                    d_fields,
                    d_waves,
                    d_calibrated_models,
                    cfg,
                    population_size,
                    spot_pupil_count,
                    d_spot_ray_samples,
                    d_pupil_order_heights,
                    d_aperture_max_heights,
                    transpose_ray_tasks ? 1 : 0,
                    (needs_wavefront || track_unused_opd) ? 1 : 0);
                ok = cuda_status_ok(cudaGetLastError());
            } else {
                const int aperture_pupil_count =
                    std::max(cfg.clear_aperture_rings, 1) *
                    std::max(cfg.clear_aperture_arms, 1);
                const int aperture_item_count =
                    population_size * cfg.field_count * cfg.wavelength_count *
                    aperture_pupil_count;
                const int aperture_grid =
                    (aperture_item_count + block - 1) / block;
                aperture_trace_kernel<<<aperture_grid, block>>>(
                    d_candidate_surfaces,
                    d_fields,
                    d_waves,
                    d_calibrated_models,
                    cfg,
                    population_size,
                    aperture_pupil_count,
                    d_aperture_max_heights,
                    transpose_ray_tasks ? 1 : 0,
                    track_unused_opd ? 1 : 0);
                ok = cuda_status_ok(cudaGetLastError());
            }
            if (ok) {
                const int guard_item_count =
                    population_size * cfg.field_count *
                    cfg.wavelength_count * kApertureTraceProbeCount;
                const int guard_grid =
                    (guard_item_count + block - 1) / block;
                aperture_guard_trace_kernel<<<guard_grid, block>>>(
                    d_candidate_surfaces,
                    d_fields,
                    d_waves,
                    d_calibrated_models,
                    cfg,
                    population_size,
                    d_aperture_max_heights,
                    d_meridional_boundary_heights,
                    d_wavefront_guard_samples,
                    d_chief_ray_samples,
                    reuse_aperture_y_guards ? 1 : 0,
                    transpose_ray_tasks ? 1 : 0,
                    track_unused_opd ? 1 : 0);
                ok = cuda_status_ok(cudaGetLastError());
            }
            if (ok) {
                finalize_apertures_kernel<<<grid, block>>>(
                    d_surfaces,
                    d_candidate_surfaces,
                    cfg,
                    population_size,
                    d_aperture_max_heights,
                    parallel_slope_domains ? 1 : 0);
                ok = cuda_status_ok(cudaGetLastError());
            }
            if (ok && parallel_slope_domains) {
                cudaEvent_t domain_start = nullptr;
                cudaEvent_t domain_stop = nullptr;
                const bool timer_started = begin_diagnostic_cuda_timer(
                    asphere_feasibility_profile_enabled,
                    &domain_start,
                    &domain_stop);
                bool launch_ok = false;
                bool timer_ok = false;
                if (timer_started) {
                    materialize_asphere_slope_domains_kernel
                        <<<domain_grid, block>>>(
                            d_candidate_surfaces,
                            slope_domain_cfg,
                            population_size);
                    launch_ok = cuda_status_ok(cudaGetLastError());
                    timer_ok = finish_diagnostic_cuda_timer(
                        asphere_feasibility_profile_enabled,
                        domain_start,
                        domain_stop,
                        &asphere_feasibility_timing.domain_seconds);
                    if (asphere_feasibility_profile_enabled) {
                        ++asphere_feasibility_timing.domain_kernel_launches;
                    }
                }
                ok = timer_started && launch_ok && timer_ok;
            }
            if (ok && stage_timing_enabled) {
                cudaEventRecord(after_aperture);
            }
            if (ok && asphere_feasibility_profile_enabled) {
                ok = cudaMemcpy(
                         &root_profile_after_aperture,
                         d_asphere_root_profile,
                         sizeof(DeviceAsphereRootProfile),
                         cudaMemcpyDeviceToHost) == cudaSuccess;
            }
            if (ok && calibrated_model_cache && !reuse_pre_aperture_models) {
                ok = launch_calibrated_model_build(
                    d_candidate_surfaces,
                    d_fields,
                    d_waves,
                    cfg,
                    population_size,
                    calibrated_center_field_index,
                    d_calibrated_models,
                    d_uc5_candidate_states,
                    model_block,
                    enable_calibrated_safeguarded_recovery,
                    uc5_candidate_batch_enabled,
                    asphere_feasibility_profile_enabled
                        ? &asphere_feasibility_timing
                        : nullptr);
            }
        }
        if (ok && stage_timing_enabled && !parallel_aperture_trace) {
            cudaEventRecord(after_aperture);
        }
        if (ok && asphere_feasibility_profile_enabled &&
            !parallel_aperture_trace) {
            root_profile_after_aperture = root_profile_after_initial_models;
        }
        if (ok && stage_timing_enabled) {
            cudaEventRecord(after_final_models);
        }
        if (ok && asphere_feasibility_profile_enabled) {
            ok = cudaMemcpy(
                     &root_profile_after_final_models,
                     d_asphere_root_profile,
                     sizeof(DeviceAsphereRootProfile),
                     cudaMemcpyDeviceToHost) == cudaSuccess;
        }
        // This prototype covers the specified bounded Gaussian wide-angle
        // profile only, after all models and final apertures are fixed.
        const bool evaluation_outer_prefix = evaluation_device_mode>0 &&
            env_flag_enabled("OPTILAND_DE_CUDA_OUTER_RMS_PRUNING") &&
            cfg.unified_trace_gate && cfg.operand_rms_spot && cfg.field_count==10 && needs_chief_samples &&
            cfg.pupil_sampling_type==kPupilSamplingGaussianRingArm &&
            !fuse_merit_aperture_envelope && !needs_vignetting_boundary_support &&
            !(cfg.minimum_relative_illumination>0.0 && cfg.minimum_relative_illumination<=1.0) &&
            cfg.min_field_coverage-evaluation_context_epsilon>cfg.min_coverage_for_image_merit+1.0e-9 &&
            std::any_of(evaluation_context_records.begin(),evaluation_context_records.end(),
                [](const EvaluationCertificate& p){return p.merit_eligible!=0;});
        const int field_item_count = population_size * cfg.field_count;
        const int field_grid = (field_item_count + block - 1) / block;
        if (ok && parallel_spot_trace) {
            if (ok && evaluation_outer_prefix) {
                const int chief_grid=(static_cast<int>(chief_ray_sample_count)+block-1)/block;
                trace_paraxial_chief_rays_kernel<<<chief_grid,block>>>(
                    d_candidate_surfaces,d_fields,d_waves,d_calibrated_models,cfg,
                    population_size,d_chief_ray_samples,(needs_wavefront||track_unused_opd)?1:0);
                ok=cuda_status_ok(cudaGetLastError());
                const int ray_grid=(static_cast<int>(spot_ray_sample_count)+block-1)/block;
                if (ok) trace_paraxial_spot_rays_kernel<<<ray_grid,block>>>(
                    d_candidate_surfaces,d_fields,d_waves,d_calibrated_models,cfg,
                    population_size,spot_pupil_count,d_spot_ray_samples,
                    d_pupil_order_heights,nullptr,transpose_ray_tasks?1:0,
                    (needs_wavefront||track_unused_opd)?1:0,1);
                if (ok) ok=cuda_status_ok(cudaGetLastError());
                if (ok && cfg.codev_use_symmetry) {
                    const int count=population_size*cfg.field_count*cfg.wavelength_count;
                    reconstruct_symmetric_spot_samples_kernel<<<(count+block-1)/block,block>>>(
                        d_fields,cfg,population_size,spot_pupil_count,d_spot_ray_samples,1);
                    ok=cuda_status_ok(cudaGetLastError());
                }
                if (ok) reduce_paraxial_spot_fields_kernel<<<field_grid,block>>>(
                    d_candidate_surfaces,d_fields,d_waves,cfg,population_size,
                    spot_pupil_count,d_spot_ray_samples,d_chief_ray_samples,
                    d_pupil_order_heights,d_meridional_boundary_heights,d_field_spot_stats,1);
                if (ok) ok=cuda_status_ok(cudaGetLastError());
                if (ok) evaluation_certify_outer_rms_kernel<<<grid,block>>>(
                    d_fields,d_waves,d_chief_ray_samples,d_field_spot_stats,cfg,
                    population_size,evaluation_context_epsilon);
                if (ok) ok=cuda_status_ok(cudaGetLastError());
            }

            if (!fuse_merit_aperture_envelope) {
                const int ray_grid =
                    (static_cast<int>(spot_ray_sample_count) + block - 1) /
                    block;
                trace_paraxial_spot_rays_kernel<<<ray_grid, block>>>(
                    d_candidate_surfaces,
                    d_fields,
                    d_waves,
                    d_calibrated_models,
                    cfg,
                    population_size,
                    spot_pupil_count,
                    d_spot_ray_samples,
                    d_pupil_order_heights,
                    nullptr,
                    transpose_ray_tasks ? 1 : 0,
                    (needs_wavefront || track_unused_opd) ? 1 : 0, evaluation_outer_prefix?2:0);
                ok = cuda_status_ok(cudaGetLastError());
            }
            if (ok && cfg.codev_use_symmetry) {
                const int reconstruction_item_count =
                    population_size * cfg.field_count * cfg.wavelength_count;
                const int reconstruction_grid =
                    (reconstruction_item_count + block - 1) / block;
                reconstruct_symmetric_spot_samples_kernel
                    <<<reconstruction_grid, block>>>(
                        d_fields,
                        cfg,
                        population_size,
                        spot_pupil_count,
                        d_spot_ray_samples, evaluation_outer_prefix?2:0);
                ok = cuda_status_ok(cudaGetLastError());
            }
            if (ok && stage_timing_enabled) {
                cudaEventRecord(after_spot_trace);
            }
            if (ok && needs_chief_samples && !evaluation_outer_prefix) {
                const int chief_grid =
                    (static_cast<int>(chief_ray_sample_count) + block - 1) / block;
                trace_paraxial_chief_rays_kernel<<<chief_grid, block>>>(
                    d_candidate_surfaces,
                    d_fields,
                    d_waves,
                    d_calibrated_models,
                    cfg,
                    population_size,
                    d_chief_ray_samples,
                    (needs_wavefront || track_unused_opd) ? 1 : 0);
                ok = cuda_status_ok(cudaGetLastError());
            }
            if (ok && stage_timing_enabled) {
                cudaEventRecord(after_chief_trace);
            }
            if (ok) {
                reduce_paraxial_spot_fields_kernel<<<field_grid, block>>>(
                    d_candidate_surfaces,
                    d_fields,
                    d_waves,
                    cfg,
                    population_size,
                    spot_pupil_count,
                    d_spot_ray_samples,
                    d_chief_ray_samples,
                    d_pupil_order_heights,
                    d_meridional_boundary_heights,
                    d_field_spot_stats, evaluation_outer_prefix?2:0);
                ok = cuda_status_ok(cudaGetLastError());
            }
            if (ok && stage_timing_enabled) {
                cudaEventRecord(after_spot_reduce);
            }
            if (ok && (needs_wavefront || needs_single_ray_spot_guard ||
                       needs_working_f_number)) {
                const int guard_grid =
                    (static_cast<int>(wavefront_guard_sample_count) + block - 1) /
                    block;
                trace_wavefront_guard_rays_kernel<<<guard_grid, block>>>(
                    d_candidate_surfaces,
                    d_fields,
                    d_waves,
                    d_calibrated_models,
                    cfg,
                    population_size,
                    d_wavefront_guard_samples,
                    reuse_aperture_y_guards ? 1 : 0,
                    needs_wavefront ? 1 : 0);
                ok = cuda_status_ok(cudaGetLastError());
            }
            if (ok && needs_selective_spot_tail_edges) {
                const int extra_guard_grid =
                    (static_cast<int>(spot_tail_extra_edge_sample_count) +
                     block - 1) /
                    block;
                trace_spot_tail_extra_edge_rays_kernel
                    <<<extra_guard_grid, block>>>(
                        d_candidate_surfaces,
                        d_fields,
                        d_waves,
                        d_calibrated_models,
                        cfg,
                        population_size,
                        d_spot_tail_extra_edge_samples);
                ok = cuda_status_ok(cudaGetLastError());
            }
            if (ok && stage_timing_enabled) {
                cudaEventRecord(after_wavefront_guard);
            }
            if (ok && (needs_single_ray_spot_guard ||
                       needs_working_f_number)) {
                apply_single_ray_spot_guard_fields_kernel<<<field_grid, block>>>(
                    d_candidate_surfaces,
                    d_fields,
                    d_waves,
                    cfg,
                    population_size,
                    d_wavefront_guard_samples,
                    d_chief_ray_samples,
                    d_spot_tail_extra_edge_samples,
                    d_field_spot_stats);
                ok = cuda_status_ok(cudaGetLastError());
            }
            if (ok && needs_wavefront) {
                wavefront_stats_kernel<<<field_grid, block>>>(
                    d_candidate_surfaces,
                    d_fields,
                    d_waves,
                    cfg,
                    population_size,
                    spot_pupil_count,
                    d_spot_ray_samples,
                    d_wavefront_guard_samples,
                    d_chief_ray_samples,
                    d_field_wavefront_stats);
                ok = cuda_status_ok(cudaGetLastError());
            }
            if (ok && stage_timing_enabled) {
                cudaEventRecord(after_wavefront_stats);
            }
        } else if (ok) {
            trace_spot_fields_kernel<<<field_grid, block>>>(
                d_candidate_surfaces,
                d_fields,
                d_waves,
                cfg,
                population_size,
                d_field_spot_stats);
            ok = cuda_status_ok(cudaGetLastError());
            if (ok && stage_timing_enabled) {
                cudaEventRecord(after_spot_trace);
                cudaEventRecord(after_chief_trace);
                cudaEventRecord(after_spot_reduce);
                cudaEventRecord(after_wavefront_guard);
                cudaEventRecord(after_wavefront_stats);
            }
        }
        if (ok && stage_timing_enabled) {
            cudaEventRecord(after_spot);
        }
        if (ok) {
            trace_spot_kernel<<<grid, block>>>(
                d_candidate_surfaces,
                d_field_spot_stats,
                d_fields,
                d_waves,
                d_meridional_boundary_heights,
                cfg,
                population_size,
                d_summaries,
                d_merit_terms);
            ok = cuda_status_ok(cudaGetLastError());
        }
        if (ok && needs_wavefront) {
            reduce_wavefront_fields_kernel<<<grid, block>>>(
                d_field_wavefront_stats,
                d_fields,
                cfg,
                population_size,
                d_summaries,
                d_merit_terms);
            ok = cuda_status_ok(cudaGetLastError());
        }
        if (ok) {
            chief_metrics_kernel<<<grid, block>>>(
                d_candidate_surfaces,
                d_fields,
                d_waves,
                d_calibrated_models,
                d_chief_ray_samples,
                cfg,
                population_size,
                d_summaries);
            ok = cuda_status_ok(cudaGetLastError());
        }
        if (ok) {
            finalize_merit_kernel<<<grid, block>>>(
                d_candidate_surfaces,
                d_glass_proximity,
                d_glass_proximity_points,
                cfg,
                population_size,
                d_merit_terms,
                d_summaries);
            ok = cuda_status_ok(cudaGetLastError());
        }
        if (ok && constraint_debug_enabled) {
            constraint_debug_kernel<<<1, 1>>>(
                d_candidate_surfaces,
                cfg,
                constraint_debug_row,
                d_constraint_debug);
            ok = cuda_status_ok(cudaGetLastError());
        }
        if (ok && constraint_debug_enabled) {
            aperture_trace_debug_kernel<<<1, 1>>>(
                d_candidate_surfaces,
                d_fields,
                d_waves,
                cfg,
                constraint_debug_row,
                d_constraint_debug);
            ok = cuda_status_ok(cudaGetLastError());
        }
        if (ok && stage_timing_enabled) {
            cudaEventRecord(after_merit);
        }
        if (ok) {
            ok = cuda_status_ok(cudaDeviceSynchronize());
        }
    }
    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace optiland
