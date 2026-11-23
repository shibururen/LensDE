#pragma once
#include "../prelude.cuh"
#include "../configuration/config_builder.cuh"
#include "../configuration/field_upload.cuh"
#include "../configuration/material_upload.cuh"
#include "../configuration/runtime_options.cuh"
#include "../contracts/constants.cuh"
#include "execution_context.cuh"

namespace optiland {
native_detail::PhaseOutcome PopulationScoringExecution::prepare_host_configuration() {
    local_timing.calls = 1;

    local_timing.candidates = population_size;

    memory_debug_enabled = env_flag_enabled("OPTILAND_DE_CUDA_MEMORY_DEBUG");

    memory_free_before = 0;

    memory_total = 0;

    memory_free_after_alloc = 0;

    memory_free_before_cleanup = 0;

    memory_free_after_cleanup = 0;

    if (memory_debug_enabled) {
        cudaMemGetInfo(&memory_free_before, &memory_total);
    }

    start = nullptr;

    after_setup = nullptr;

    after_h2d = nullptr;

    after_kernel = nullptr;

    after_d2h = nullptr;

    after_cleanup = nullptr;

    stage_timing_enabled = env_flag_enabled("OPTILAND_DE_CUDA_STAGE_TIMING");

    after_geometry = nullptr;

    after_initial_models = nullptr;

    after_aperture = nullptr;

    after_final_models = nullptr;

    after_spot_trace = nullptr;

    after_chief_trace = nullptr;

    after_spot_reduce = nullptr;

    after_wavefront_guard = nullptr;

    after_wavefront_stats = nullptr;

    after_spot = nullptr;

    after_merit = nullptr;

    cudaEventCreate(&start);

    cudaEventCreate(&after_setup);

    cudaEventCreate(&after_h2d);

    cudaEventCreate(&after_kernel);

    cudaEventCreate(&after_d2h);

    cudaEventCreate(&after_cleanup);

    if (stage_timing_enabled) {
        cudaEventCreate(&after_geometry);
        cudaEventCreate(&after_initial_models);
        cudaEventCreate(&after_aperture);
        cudaEventCreate(&after_final_models);
        cudaEventCreate(&after_spot_trace);
        cudaEventCreate(&after_chief_trace);
        cudaEventCreate(&after_spot_reduce);
        cudaEventCreate(&after_wavefront_guard);
        cudaEventCreate(&after_wavefront_stats);
        cudaEventCreate(&after_spot);
        cudaEventCreate(&after_merit);
    }

    cudaEventRecord(start);

    cfg = make_device_config(base, merit_config, variables);

    cfg.variable_count = static_cast<int>(variables.size());

    uc5_experiment_scope = asphere_root_policy() ==
            AsphereRootPolicy::UnifiedCertifiedExperimental &&
        cfg.asphere_initial_root_mode == 10;

    uc5_combined = uc5_experiment_scope && env_flag_enabled("OPTILAND_ASPHERE_UC5");

    uc5_symmetry_enabled = uc5_experiment_scope &&
        (uc5_combined ||
         env_flag_enabled("OPTILAND_ASPHERE_UC5_SYMMETRY"));

    uc5_primary_abort_enabled = uc5_experiment_scope &&
        (uc5_combined ||
         env_flag_enabled("OPTILAND_ASPHERE_UC5_PRIMARY_EARLY_ABORT"));

    uc5_candidate_batch_enabled = uc5_experiment_scope &&
        (uc5_combined ||
         env_flag_enabled("OPTILAND_ASPHERE_UC5_CANDIDATE_BATCH"));

    asphere_uc5_profile_enabled = uc5_experiment_scope &&
        env_flag_enabled("OPTILAND_ASPHERE_UC5_PROFILE");

    static thread_local unsigned long long uc5_evaluation_sequence = 0;

    uc5_evaluation_index = uc5_evaluation_sequence++;

    asphere_feasibility_profile_override = std::getenv("OPTILAND_DE_CUDA_ASPHERE_FEASIBILITY_PROFILE");

    asphere_feasibility_profile_enabled = asphere_feasibility_profile_override
            ? env_flag_enabled("OPTILAND_DE_CUDA_ASPHERE_FEASIBILITY_PROFILE")
            : asphere_root_policy() ==
                  AsphereRootPolicy::UnifiedCertifiedExperimental;

    asphere_deep_profile_enabled = env_flag_enabled("OPTILAND_DE_CUDA_ASPHERE_DEEP_PROFILE");

    asphere_root_profile_enabled = asphere_feasibility_profile_enabled ||
        asphere_deep_profile_enabled ||
        env_flag_enabled("OPTILAND_DE_CUDA_ASPHERE_ROOT_PROFILE");

    asphere_feasibility_timing = {};

    transpose_override = std::getenv("OPTILAND_DE_CUDA_TRANSPOSE_RAY_TASKS");

    transpose_ray_tasks = transpose_override
            ? env_flag_enabled("OPTILAND_DE_CUDA_TRANSPOSE_RAY_TASKS")
            : (asphere_root_policy() ==
                   AsphereRootPolicy::SlopeDomainTopologySeed ||
               asphere_root_policy() ==
                   AsphereRootPolicy::UnifiedCertifiedExperimental);

    constraint_debug_row = -1;

    if (const char* value = std::getenv("OPTILAND_DE_CUDA_CONSTRAINT_DEBUG_ROW")) {
        constraint_debug_row = std::atoi(value);
    }

    constraint_debug_enabled = constraint_debug_row >= 0 && constraint_debug_row < population_size;

    trace_debug_row = -1;

    if (const char* value = std::getenv("OPTILAND_DE_CUDA_TRACE_DEBUG_ROW")) {
        trace_debug_row = std::atoi(value);
    }

    trace_debug_enabled = trace_debug_row >= 0 && trace_debug_row < population_size;

    host_surfaces = make_device_surfaces(base, cfg.asphere_initial_root_mode);

    host_fields = make_device_fields(base);

    host_waves = make_device_waves(base);

    enable_calibrated_safeguarded_recovery = asphere_root_policy() ==
            AsphereRootPolicy::SlopeDomainTopologySeed &&
        std::any_of(
            host_surfaces.begin(),
            host_surfaces.end(),
            [](const DeviceSurface& surface) {
                return surface.kind == kKindEvenAsphere;
            }) &&
        !(asphere_feasibility_profile_enabled &&
          env_flag_enabled(
              "OPTILAND_DE_CUDA_ASPHERE_FEASIBILITY_DISABLE_RECOVERY"));

    host_material_indices = make_device_material_indices(base);

    cfg.field_count = static_cast<int>(host_fields.size());

    cfg.wavelength_count = static_cast<int>(host_waves.size());

    make_device_glass_proximity(
        merit_config,
        &host_glass_proximity,
        &host_glass_proximity_points);

    cfg.glass_proximity_count = static_cast<int>(host_glass_proximity.size());

    cfg.glass_proximity_point_count =
        static_cast<int>(host_glass_proximity_points.size());

    host_summaries = std::vector<CudaScoreSummary>(
        static_cast<std::size_t>(population_size));

    first_cuda_error = cudaSuccess;

    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace optiland
