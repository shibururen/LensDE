#pragma once
#include "../prelude.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/spot_wavefront_records.cuh"
#include "../diagnostics/asphere_profile_records.cuh"
#include "execution_context.cuh"

namespace optiland {
native_detail::PhaseOutcome PopulationScoringExecution::readback_results() {
    if (ok) {
        ok = cudaMemcpy(host_summaries.data(), d_summaries, summary_bytes, cudaMemcpyDeviceToHost) ==
             cudaSuccess;
    }

    if (ok && trace_debug_enabled) {
        const std::size_t field_offset =
            static_cast<std::size_t>(trace_debug_row) * host_fields.size();
        ok = cudaMemcpy(
                 host_trace_debug_fields.data(),
                 d_field_spot_stats + field_offset,
                 host_trace_debug_fields.size() * sizeof(DeviceFieldSpotStats),
                 cudaMemcpyDeviceToHost) == cudaSuccess;
    }

    if (ok && trace_debug_enabled && parallel_spot_trace) {
        const std::size_t sample_offset =
            static_cast<std::size_t>(trace_debug_row) *
            host_trace_debug_samples.size();
        ok = cudaMemcpy(
                 host_trace_debug_samples.data(),
                 d_spot_ray_samples + sample_offset,
                 host_trace_debug_samples.size() * sizeof(DeviceSpotRaySample),
                 cudaMemcpyDeviceToHost) == cudaSuccess;
    }

    if (ok && trace_debug_enabled && calibrated_model_cache) {
        const std::size_t model_offset =
            static_cast<std::size_t>(trace_debug_row) *
            host_trace_debug_models.size();
        ok = cudaMemcpy(
                 host_trace_debug_models.data(),
                 d_calibrated_models + model_offset,
                 host_trace_debug_models.size() * sizeof(DeviceCalibratedModel),
                 cudaMemcpyDeviceToHost) == cudaSuccess;
    }

    if (ok && trace_debug_enabled && parallel_spot_trace && needs_wavefront) {
        const std::size_t field_offset =
            static_cast<std::size_t>(trace_debug_row) * host_fields.size();
        ok = cudaMemcpy(
                 host_trace_debug_wavefront_fields.data(),
                 d_field_wavefront_stats + field_offset,
                 host_trace_debug_wavefront_fields.size() *
                     sizeof(DeviceFieldWavefrontStats),
                 cudaMemcpyDeviceToHost) == cudaSuccess;
    }

    if (ok && constraint_debug_enabled) {
        ok = cudaMemcpy(
                 host_constraint_debug.data(),
                 d_constraint_debug,
                 constraint_debug_bytes,
                 cudaMemcpyDeviceToHost) == cudaSuccess;
    }

    if (ok && asphere_root_profile_enabled) {
        ok = cudaMemcpy(
                 &host_asphere_root_profile,
                 d_asphere_root_profile,
                 sizeof(DeviceAsphereRootProfile),
                 cudaMemcpyDeviceToHost) == cudaSuccess;
    }

    if (ok && asphere_feasibility_profile_enabled) {
        ok = cudaMemcpy(
                 &host_asphere_feasibility_profile,
                 d_asphere_feasibility_profile,
                 sizeof(DeviceAsphereFeasibilityProfile),
                 cudaMemcpyDeviceToHost) == cudaSuccess;
    }

    if (ok && asphere_deep_profile_enabled) {
        ok = cudaMemcpy(
                 &host_asphere_deep_profile,
                 d_asphere_deep_profile,
                 sizeof(DeviceAsphereDeepProfile),
                 cudaMemcpyDeviceToHost) == cudaSuccess;
    }

    if (ok && asphere_uc5_profile_enabled) {
        ok = cudaMemcpy(
                 &host_asphere_uc5_profile,
                 d_asphere_uc5_profile,
                 sizeof(DeviceAsphereUC5Profile),
                 cudaMemcpyDeviceToHost) == cudaSuccess;
    }

    cudaEventRecord(after_d2h);

    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace optiland
