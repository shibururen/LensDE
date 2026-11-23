#pragma once
#include "../prelude.cuh"
#include "../configuration/runtime_options.cuh"

namespace optiland {
// namespace
bool evaluate_population_cuda_available() {
    // The legacy cuda_evaluator.cu is intentionally not linked into the
    // DE-CUDA target. The dedicated DE build defaults to the CPU-parity scorer;
    // geometry-only remains as an explicit diagnostic mode.
    return de_cuda_v2_geometry_only_enabled() || de_cuda_v2_full_enabled();
}
} // namespace optiland
