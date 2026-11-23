#pragma once
#include "../prelude.cuh"
#include "execution_context.cuh"
#include "upload.cuh"

namespace optiland {
native_detail::PhaseOutcome PopulationScoringExecution::upload_inputs() {
    UploadPopulationScoringInputsInputs upload_population_scoring_inputs_inputs{
        flat_population,
        host_variables,
        host_glass_candidates,
        asphere_uc5_profile_enabled,
        asphere_feasibility_profile_enabled,
        asphere_deep_profile_enabled,
        asphere_root_profile_enabled,
        host_surfaces,
        host_fields,
        host_waves,
        host_material_indices,
        host_glass_proximity,
        host_glass_proximity_points,
        d_surfaces,
        d_variables,
        d_glass_candidates,
        d_fields,
        d_waves,
        d_material_indices,
        d_glass_proximity,
        d_glass_proximity_points,
        d_population,
        d_aperture_max_heights,
        d_meridional_boundary_heights,
        d_wavefront_guard_samples,
        d_asphere_root_profile,
        d_asphere_feasibility_profile,
        d_asphere_deep_profile,
        d_asphere_uc5_profile,
        surface_bytes,
        variable_bytes,
        glass_candidate_bytes,
        field_bytes,
        wave_bytes,
        material_index_bytes,
        glass_proximity_bytes,
        glass_proximity_point_bytes,
        population_bytes,
        aperture_max_heights_bytes,
        meridional_boundary_heights_bytes,
        wavefront_guard_samples_bytes,
        ok,
        parallel_aperture_trace,
        reuse_aperture_y_guards
    };

    upload_population_scoring_inputs(upload_population_scoring_inputs_inputs);

    cudaEventRecord(after_h2d);

    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace optiland
