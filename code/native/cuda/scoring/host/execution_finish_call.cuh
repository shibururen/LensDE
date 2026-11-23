#pragma once
#include "../prelude.cuh"
#include "execution_context.cuh"
#include "../kernels/merit_reduction.cuh"

namespace optiland {
native_detail::PhaseOutcome PopulationScoringExecution::finish_call() {
    cudaEventSynchronize(after_cleanup);

    auto elapsed = [](cudaEvent_t a, cudaEvent_t b) {
        float ms = 0.0f;
        cudaEventElapsedTime(&ms, a, b);
        return static_cast<double>(ms) / 1000.0;
    };

    local_timing.host_setup_seconds = elapsed(start, after_setup);

    local_timing.host_to_device_seconds = elapsed(after_setup, after_h2d);

    local_timing.kernel_seconds = elapsed(after_h2d, after_kernel);

    local_timing.device_to_host_seconds = elapsed(after_kernel, after_d2h);

    local_timing.cleanup_seconds = elapsed(after_d2h, after_cleanup);

    local_timing.total_seconds = elapsed(start, after_cleanup);

    if (ok && stage_timing_enabled) {
        std::fprintf(
            stderr,
            "DE-CUDA stage timing: geometry=%.6f s, initial_models=%.6f s, "
            "aperture=%.6f s, final_models=%.6f s, spot=%.6f s, "
            "merit=%.6f s\n"
            "DE-CUDA spot detail: trace=%.6f s, chief=%.6f s, "
            "reduce=%.6f s, wavefront_guard=%.6f s, "
            "wavefront_stats=%.6f s, finalize=%.6f s\n",
            elapsed(after_h2d, after_geometry),
            elapsed(after_geometry, after_initial_models),
            elapsed(after_initial_models, after_aperture),
            elapsed(after_aperture, after_final_models),
            elapsed(after_final_models, after_spot),
            elapsed(after_spot, after_merit),
            elapsed(after_final_models, after_spot_trace),
            elapsed(after_spot_trace, after_chief_trace),
            elapsed(after_chief_trace, after_spot_reduce),
            elapsed(after_spot_reduce, after_wavefront_guard),
            elapsed(after_wavefront_guard, after_wavefront_stats),
            elapsed(after_wavefront_stats, after_spot));
    }

    cudaEventDestroy(start);

    cudaEventDestroy(after_setup);

    cudaEventDestroy(after_h2d);

    cudaEventDestroy(after_kernel);

    cudaEventDestroy(after_d2h);

    cudaEventDestroy(after_cleanup);

    if (stage_timing_enabled) {
        cudaEventDestroy(after_geometry);
        cudaEventDestroy(after_initial_models);
        cudaEventDestroy(after_aperture);
        cudaEventDestroy(after_final_models);
        cudaEventDestroy(after_spot_trace);
        cudaEventDestroy(after_chief_trace);
        cudaEventDestroy(after_spot_reduce);
        cudaEventDestroy(after_wavefront_guard);
        cudaEventDestroy(after_wavefront_stats);
        cudaEventDestroy(after_spot);
        cudaEventDestroy(after_merit);
    }

    accumulate_timing(timing, local_timing);

    if (!ok) {
        if (debug_reject) {
            std::fprintf(
                stderr,
                "DE-CUDA scorer CUDA failure: %s (%s)\n",
                cudaGetErrorName(first_cuda_error),
                cudaGetErrorString(first_cuda_error));
        }
        return {native_detail::PhaseFlow::Return, false};
    }

    if (summaries) {
        *summaries = host_summaries;
    }

    if (merits) {
        merits->resize(static_cast<std::size_t>(population_size));
        for (int i = 0; i < population_size; ++i) {
            (*merits)[static_cast<std::size_t>(i)] =
                host_summaries[static_cast<std::size_t>(i)].merit;
        }
    }

    return {native_detail::PhaseFlow::Return, true};

    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace optiland
