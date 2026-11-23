#pragma once
#include "../prelude.cuh"
#include "../diagnostics/asphere_profile_records.cuh"
#include "certificate_context.cuh"
#include "execution_context.cuh"

namespace optiland {
native_detail::PhaseOutcome PopulationScoringExecution::release_device_resources() {
    if (d_surfaces) {
        cudaFree(d_surfaces);
    }

    if (d_variables) {
        cudaFree(d_variables);
    }

    if (d_glass_candidates) {
        cudaFree(d_glass_candidates);
    }

    if (d_population) {
        cudaFree(d_population);
    }

    if (d_candidate_surfaces) {
        cudaFree(d_candidate_surfaces);
    }

    if (d_candidate_fields) {
        cudaFree(d_candidate_fields);
    }

    if (d_aperture_max_heights) {
        cudaFree(d_aperture_max_heights);
    }

    if (d_meridional_boundary_heights) {
        cudaFree(d_meridional_boundary_heights);
    }

    if (d_evaluation_certificate) {
        if (ok) ok = cudaMemcpy(evaluation_context_records.data(), d_evaluation_certificate,
            evaluation_context_records.size() * sizeof(EvaluationCertificate), cudaMemcpyDeviceToHost) == cudaSuccess;
        EvaluationCertificate* null_certificate = nullptr;
        cudaMemcpyToSymbol(g_evaluation_certificate, &null_certificate, sizeof(null_certificate));
        cudaFree(d_evaluation_certificate);
    }

    if (d_calibrated_models) {
        cudaFree(d_calibrated_models);
    }

    if (d_uc5_candidate_states) {
        cudaFree(d_uc5_candidate_states);
    }

    if (d_spot_ray_samples) {
        cudaFree(d_spot_ray_samples);
    }

    if (d_pupil_order_heights) {
        cudaFree(d_pupil_order_heights);
    }

    if (d_wavefront_guard_samples) {
        cudaFree(d_wavefront_guard_samples);
    }

    if (d_spot_tail_extra_edge_samples) {
        cudaFree(d_spot_tail_extra_edge_samples);
    }

    if (d_chief_ray_samples) {
        cudaFree(d_chief_ray_samples);
    }

    if (d_field_spot_stats) {
        cudaFree(d_field_spot_stats);
    }

    if (d_field_wavefront_stats) {
        cudaFree(d_field_wavefront_stats);
    }

    if (d_merit_terms) {
        cudaFree(d_merit_terms);
    }

    if (d_fields) {
        cudaFree(d_fields);
    }

    if (d_waves) {
        cudaFree(d_waves);
    }

    if (d_material_indices) {
        cudaFree(d_material_indices);
    }

    if (d_glass_proximity) {
        cudaFree(d_glass_proximity);
    }

    if (d_glass_proximity_points) {
        cudaFree(d_glass_proximity_points);
    }

    if (d_summaries) {
        cudaFree(d_summaries);
    }

    if (d_constraint_debug) {
        cudaFree(d_constraint_debug);
    }

    if (d_asphere_root_profile) {
        cudaFree(d_asphere_root_profile);
    }

    if (d_asphere_feasibility_profile) {
        cudaFree(d_asphere_feasibility_profile);
    }

    if (d_asphere_deep_profile) {
        cudaFree(d_asphere_deep_profile);
    }

    if (d_asphere_uc5_profile) {
        cudaMemcpyToSymbol(
            g_asphere_uc5_profile,
            &null_uc5_profile,
            sizeof(null_uc5_profile));
        cudaFree(d_asphere_uc5_profile);
    }

    if (memory_debug_enabled) {
        cudaMemGetInfo(&memory_free_after_cleanup, &memory_total);
        const std::size_t peak_free =
            std::min(memory_free_after_alloc, memory_free_before_cleanup);
        const std::size_t peak_delta =
            memory_free_before > peak_free
                ? memory_free_before - peak_free
                : 0;
        std::fprintf(
            stderr,
            "DE-CUDA memory: total_bytes=%zu, free_before_bytes=%zu, "
            "free_after_alloc_bytes=%zu, free_before_cleanup_bytes=%zu, "
            "free_after_cleanup_bytes=%zu, peak_delta_bytes=%zu\n",
            memory_total,
            memory_free_before,
            memory_free_after_alloc,
            memory_free_before_cleanup,
            memory_free_after_cleanup,
            peak_delta);
    }

    cudaEventRecord(after_cleanup);

    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace optiland
