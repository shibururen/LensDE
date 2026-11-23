#pragma once
#include "../prelude.cuh"
#include "execution_context.cuh"
#include "kernel_pipeline.cuh"

namespace optiland {
native_detail::PhaseOutcome PopulationScoringExecution::launch_kernels() {
    LaunchPopulationScoringPipelineInputs<decltype(cuda_status_ok)> launch_population_scoring_pipeline_inputs{variables, population_size, stage_timing_enabled, after_geometry, after_initial_models, after_aperture, after_final_models, after_spot_trace, after_chief_trace, after_spot_reduce, after_wavefront_guard, after_wavefront_stats, after_spot, after_merit, cfg, uc5_candidate_batch_enabled, asphere_feasibility_profile_enabled, asphere_deep_profile_enabled, asphere_feasibility_timing, transpose_ray_tasks, constraint_debug_row, constraint_debug_enabled, enable_calibrated_safeguarded_recovery, cuda_status_ok, d_surfaces, d_variables, d_glass_candidates, d_fields, d_waves, d_glass_proximity, d_glass_proximity_points, d_population, d_candidate_surfaces, d_candidate_fields, d_aperture_max_heights, d_meridional_boundary_heights, d_calibrated_models, d_spot_ray_samples, d_pupil_order_heights, d_wavefront_guard_samples, d_spot_tail_extra_edge_samples, d_chief_ray_samples, d_field_spot_stats, d_field_wavefront_stats, d_merit_terms, d_summaries, d_constraint_debug, d_asphere_root_profile, root_profile_after_geometry, root_profile_after_initial_models, root_profile_after_aperture, root_profile_after_final_models, d_uc5_candidate_states, spot_pupil_count, spot_ray_sample_count, wavefront_guard_sample_count, spot_tail_extra_edge_sample_count, chief_ray_sample_count, ok, calibrated_model_cache, reuse_pre_aperture_models, parallel_aperture_trace, needs_vignetting_boundary_support, parallel_spot_trace, fuse_merit_aperture_envelope, needs_wavefront, needs_single_ray_spot_guard, needs_working_f_number, needs_selective_spot_tail_edges, track_unused_opd, needs_chief_samples, reuse_aperture_y_guards, calibrated_center_field_index};

    launch_population_scoring_pipeline(launch_population_scoring_pipeline_inputs);

    cudaEventRecord(after_kernel);

    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace optiland
