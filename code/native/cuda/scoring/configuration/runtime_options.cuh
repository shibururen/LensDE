#pragma once
#include "../prelude.cuh"

namespace optiland {
namespace {
bool env_flag_enabled(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return false;
    }
    std::string text(value);
    for (char& ch : text) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return text == "1" || text == "true" || text == "yes" || text == "on";
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
bool env_flag_enabled_default(const char* name, bool default_value) {
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return default_value;
    }
    std::string text(value);
    for (char& ch : text) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    if (text == "1" || text == "true" || text == "yes" || text == "on") {
        return true;
    }
    if (text == "0" || text == "false" || text == "no" || text == "off") {
        return false;
    }
    return default_value;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
struct AsphereFeasibilityHostTiming {
    double domain_seconds = 0.0;
    double calibrated_mark_seconds = 0.0;
    double calibrated_recovery_seconds = 0.0;
    int domain_kernel_launches = 0;
    int calibrated_mark_kernel_launches = 0;
    int calibrated_recovery_kernel_launches = 0;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
bool begin_diagnostic_cuda_timer(
    bool enabled,
    cudaEvent_t* start,
    cudaEvent_t* stop) {
    if (!enabled) {
        return true;
    }
    *start = nullptr;
    *stop = nullptr;
    if (cudaEventCreate(start) != cudaSuccess) {
        return false;
    }
    if (cudaEventCreate(stop) != cudaSuccess) {
        cudaEventDestroy(*start);
        *start = nullptr;
        return false;
    }
    if (cudaEventRecord(*start) != cudaSuccess) {
        cudaEventDestroy(*start);
        cudaEventDestroy(*stop);
        *start = nullptr;
        *stop = nullptr;
        return false;
    }
    return true;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
bool finish_diagnostic_cuda_timer(
    bool enabled,
    cudaEvent_t start,
    cudaEvent_t stop,
    double* elapsed_seconds) {
    if (!enabled) {
        return true;
    }
    bool ok = cudaEventRecord(stop) == cudaSuccess &&
              cudaEventSynchronize(stop) == cudaSuccess;
    float elapsed_ms = 0.0f;
    if (ok) {
        ok = cudaEventElapsedTime(&elapsed_ms, start, stop) == cudaSuccess;
    }
    cudaEventDestroy(start);
    cudaEventDestroy(stop);
    if (ok && elapsed_seconds) {
        *elapsed_seconds += static_cast<double>(elapsed_ms) / 1000.0;
    }
    return ok;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
bool de_cuda_v2_enabled() {
#ifdef OPTILAND_DE_CUDA_ONLY
    return !env_flag_enabled("OPTILAND_DE_CUDA_V2_DISABLE");
#else
    return env_flag_enabled("OPTILAND_DE_CUDA_V2_ENABLE");
#endif
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
bool de_cuda_v2_geometry_only_enabled() {
    return de_cuda_v2_enabled() &&
           env_flag_enabled("OPTILAND_DE_CUDA_V2_GEOMETRY_ONLY");
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
bool de_cuda_v2_full_enabled() {
#ifdef OPTILAND_DE_CUDA_ONLY
    return de_cuda_v2_enabled() &&
           !env_flag_enabled("OPTILAND_DE_CUDA_V2_GEOMETRY_ONLY");
#else
    return de_cuda_v2_enabled() &&
           env_flag_enabled("OPTILAND_DE_CUDA_V2_FULL");
#endif
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
std::string normalized_token(std::string text) {
    for (char& ch : text) {
        if (ch == '-' || ch == ' ') {
            ch = '_';
        } else if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return text;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
bool unified_trace_gate_enabled(const MeritConfig& config) {
    const std::string model = normalized_token(config.merit_model);
    return model == "unified_trace_gate" ||
           model == "trace_guarded_adaptive" ||
           model == "unified_adaptive" ||
           model == "trace_guarded";
}
} // namespace
} // namespace optiland
