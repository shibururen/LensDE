#pragma once
#include "../prelude.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/optical_records.cuh"
#include "../contracts/spot_wavefront_records.cuh"
#include "../diagnostics/asphere_profile_records.cuh"

namespace optiland {
struct UploadPopulationScoringInputsInputs {
    const std::vector<double>& flat_population;
    std::vector<DeviceVariable>& host_variables;
    std::vector<DeviceGlassCandidate>& host_glass_candidates;
    const bool& asphere_uc5_profile_enabled;
    const bool& asphere_feasibility_profile_enabled;
    const bool& asphere_deep_profile_enabled;
    const bool& asphere_root_profile_enabled;
    const std::vector<DeviceSurface>& host_surfaces;
    const std::vector<DeviceField>& host_fields;
    const std::vector<DeviceWave>& host_waves;
    const std::vector<double>& host_material_indices;
    std::vector<DeviceGlassProximity>& host_glass_proximity;
    std::vector<DeviceGlassProximityPoint>& host_glass_proximity_points;
    DeviceSurface*& d_surfaces;
    DeviceVariable*& d_variables;
    DeviceGlassCandidate*& d_glass_candidates;
    DeviceField*& d_fields;
    DeviceWave*& d_waves;
    double*& d_material_indices;
    DeviceGlassProximity*& d_glass_proximity;
    DeviceGlassProximityPoint*& d_glass_proximity_points;
    double*& d_population;
    double*& d_aperture_max_heights;
    double*& d_meridional_boundary_heights;
    DeviceSpotRaySample*& d_wavefront_guard_samples;
    DeviceAsphereRootProfile*& d_asphere_root_profile;
    DeviceAsphereFeasibilityProfile*& d_asphere_feasibility_profile;
    DeviceAsphereDeepProfile*& d_asphere_deep_profile;
    DeviceAsphereUC5Profile*& d_asphere_uc5_profile;
    const std::size_t& surface_bytes;
    const std::size_t& variable_bytes;
    const std::size_t& glass_candidate_bytes;
    const std::size_t& field_bytes;
    const std::size_t& wave_bytes;
    const std::size_t& material_index_bytes;
    const std::size_t& glass_proximity_bytes;
    const std::size_t& glass_proximity_point_bytes;
    const std::size_t& population_bytes;
    const std::size_t& aperture_max_heights_bytes;
    const std::size_t& meridional_boundary_heights_bytes;
    const std::size_t& wavefront_guard_samples_bytes;
    bool& ok;
    const bool& parallel_aperture_trace;
    const bool& reuse_aperture_y_guards;
};
} // namespace optiland

namespace optiland {
template <class Inputs>
inline native_detail::PhaseOutcome upload_population_scoring_inputs(Inputs& inputs) {
    auto& flat_population = inputs.flat_population;
    auto& host_variables = inputs.host_variables;
    auto& host_glass_candidates = inputs.host_glass_candidates;
    auto& asphere_uc5_profile_enabled = inputs.asphere_uc5_profile_enabled;
    auto& asphere_feasibility_profile_enabled = inputs.asphere_feasibility_profile_enabled;
    auto& asphere_deep_profile_enabled = inputs.asphere_deep_profile_enabled;
    auto& asphere_root_profile_enabled = inputs.asphere_root_profile_enabled;
    auto& host_surfaces = inputs.host_surfaces;
    auto& host_fields = inputs.host_fields;
    auto& host_waves = inputs.host_waves;
    auto& host_material_indices = inputs.host_material_indices;
    auto& host_glass_proximity = inputs.host_glass_proximity;
    auto& host_glass_proximity_points = inputs.host_glass_proximity_points;
    auto& d_surfaces = inputs.d_surfaces;
    auto& d_variables = inputs.d_variables;
    auto& d_glass_candidates = inputs.d_glass_candidates;
    auto& d_fields = inputs.d_fields;
    auto& d_waves = inputs.d_waves;
    auto& d_material_indices = inputs.d_material_indices;
    auto& d_glass_proximity = inputs.d_glass_proximity;
    auto& d_glass_proximity_points = inputs.d_glass_proximity_points;
    auto& d_population = inputs.d_population;
    auto& d_aperture_max_heights = inputs.d_aperture_max_heights;
    auto& d_meridional_boundary_heights = inputs.d_meridional_boundary_heights;
    auto& d_wavefront_guard_samples = inputs.d_wavefront_guard_samples;
    auto& d_asphere_root_profile = inputs.d_asphere_root_profile;
    auto& d_asphere_feasibility_profile = inputs.d_asphere_feasibility_profile;
    auto& d_asphere_deep_profile = inputs.d_asphere_deep_profile;
    auto& d_asphere_uc5_profile = inputs.d_asphere_uc5_profile;
    auto& surface_bytes = inputs.surface_bytes;
    auto& variable_bytes = inputs.variable_bytes;
    auto& glass_candidate_bytes = inputs.glass_candidate_bytes;
    auto& field_bytes = inputs.field_bytes;
    auto& wave_bytes = inputs.wave_bytes;
    auto& material_index_bytes = inputs.material_index_bytes;
    auto& glass_proximity_bytes = inputs.glass_proximity_bytes;
    auto& glass_proximity_point_bytes = inputs.glass_proximity_point_bytes;
    auto& population_bytes = inputs.population_bytes;
    auto& aperture_max_heights_bytes = inputs.aperture_max_heights_bytes;
    auto& meridional_boundary_heights_bytes = inputs.meridional_boundary_heights_bytes;
    auto& wavefront_guard_samples_bytes = inputs.wavefront_guard_samples_bytes;
    auto& ok = inputs.ok;
    auto& parallel_aperture_trace = inputs.parallel_aperture_trace;
    auto& reuse_aperture_y_guards = inputs.reuse_aperture_y_guards;
    if (ok) {
        ok = cudaMemcpy(d_surfaces, host_surfaces.data(), surface_bytes, cudaMemcpyHostToDevice) ==
                 cudaSuccess &&
             cudaMemcpy(d_variables, host_variables.data(), variable_bytes, cudaMemcpyHostToDevice) ==
                 cudaSuccess &&
             cudaMemcpy(d_fields, host_fields.data(), field_bytes, cudaMemcpyHostToDevice) ==
                 cudaSuccess &&
             cudaMemcpy(d_waves, host_waves.data(), wave_bytes, cudaMemcpyHostToDevice) ==
                 cudaSuccess &&
             cudaMemcpy(
                 d_material_indices,
                 host_material_indices.data(),
                 material_index_bytes,
                 cudaMemcpyHostToDevice) == cudaSuccess &&
             cudaMemcpy(d_population, flat_population.data(), population_bytes, cudaMemcpyHostToDevice) ==
                 cudaSuccess;
        if (ok && !host_glass_proximity.empty()) {
            ok = cudaMemcpy(
                     d_glass_proximity,
                     host_glass_proximity.data(),
                     glass_proximity_bytes,
                     cudaMemcpyHostToDevice) == cudaSuccess;
        }
        if (ok && !host_glass_candidates.empty()) {
            ok = cudaMemcpy(
                     d_glass_candidates,
                     host_glass_candidates.data(),
                     glass_candidate_bytes,
                     cudaMemcpyHostToDevice) == cudaSuccess;
        }
        if (ok && !host_glass_proximity_points.empty()) {
            ok = cudaMemcpy(
                     d_glass_proximity_points,
                     host_glass_proximity_points.data(),
                     glass_proximity_point_bytes,
                     cudaMemcpyHostToDevice) == cudaSuccess;
        }
        if (ok && parallel_aperture_trace) {
            ok = cudaMemset(
                     d_aperture_max_heights,
                     0,
                     aperture_max_heights_bytes) == cudaSuccess;
        }
        if (ok && d_meridional_boundary_heights) {
            ok = cudaMemset(
                     d_meridional_boundary_heights,
                     0,
                     meridional_boundary_heights_bytes) == cudaSuccess;
        }
        if (ok && reuse_aperture_y_guards) {
            ok = cudaMemset(
                     d_wavefront_guard_samples,
                     0,
                     wavefront_guard_samples_bytes) == cudaSuccess;
        }
        if (ok && asphere_root_profile_enabled) {
            ok = cudaMemset(
                     d_asphere_root_profile,
                     0,
                     sizeof(DeviceAsphereRootProfile)) == cudaSuccess;
        }
        if (ok && asphere_feasibility_profile_enabled) {
            ok = cudaMemset(
                     d_asphere_feasibility_profile,
                     0,
                     sizeof(DeviceAsphereFeasibilityProfile)) == cudaSuccess;
        }
        if (ok && asphere_deep_profile_enabled) {
            ok = cudaMemset(
                     d_asphere_deep_profile,
                     0,
                     sizeof(DeviceAsphereDeepProfile)) == cudaSuccess;
        }
        if (ok && asphere_uc5_profile_enabled) {
            ok = cudaMemset(
                     d_asphere_uc5_profile,
                     0,
                     sizeof(DeviceAsphereUC5Profile)) == cudaSuccess;
        }
    }
    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace optiland
