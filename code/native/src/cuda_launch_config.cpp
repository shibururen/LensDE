// Resolve the retained CUDA block-size environment controls for general and calibrated-model kernels.
// Host launch pipelines consume these validated sizes rather than interpreting environment text themselves.
// Keep fallback sizes, upper bounds, and general-kernel warp alignment consistent with the launch contract.

#include "optiland/cuda_launch_config.hpp"

#include <algorithm>
#include <cstdlib>

namespace optiland {
namespace {

int block_size_from_env(
    const char* name,
    int fallback,
    int minimum,
    bool warp_aligned) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || *end != '\0') {
        return fallback;
    }
    const int clamped = std::clamp(static_cast<int>(parsed), minimum, 256);
    if (!warp_aligned) {
        return clamped;
    }
    return std::max(minimum, (clamped / 32) * 32);
}

}  // namespace

int de_cuda_general_block_size() {
    return block_size_from_env("OPTILAND_DE_CUDA_BLOCK_SIZE", 32, 32, true);
}

int de_cuda_calibrated_model_block_size() {
    return block_size_from_env(
        "OPTILAND_DE_CUDA_MODEL_BLOCK_SIZE", 8, 8, false);
}

}  // namespace optiland
