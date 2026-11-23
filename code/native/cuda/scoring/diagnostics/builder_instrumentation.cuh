#pragma once
#include "../prelude.cuh"

namespace optiland {
namespace {
struct DeviceBuilderWork {
    unsigned long long calls = 0, iterations = 0, probes = 0, backtracks = 0;
    unsigned long long failed_iterations = 0, duplicate_final = 0;
    double first_step_x = 0, first_step_y = 0, first_ex = 0, first_ey = 0;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
struct DeviceBuilderDiagnostic {
    unsigned long long calls = 0, total_cycles = 0, chief_cycles = 0;
    unsigned long long paraxial_ok = 0, budget_calls = 0, chief_trials = 0;
    unsigned long long chief_ok = 0, model_ok = 0;
    unsigned long long edge_cycles[4] = {}, e3_cycles[4] = {};
    unsigned long long e3_calls[4] = {}, e3_ok[4] = {}, edge_ok[4] = {};
    DeviceBuilderWork chief_work, edge_work[4], e3_work[4];
    double center_data[12] = {}, initial_data[4][6] = {}, anchor_data[4][4] = {};
    unsigned long long symmetry_reused[4] = {}, symmetry_probe_failed[4] = {};
    unsigned long long warm_attempt[4] = {}, warm_traceable[4] = {}, warm_solved[4] = {};
    double warm_initial[4][4] = {};
    unsigned long long repeated_prediction[4] = {}, residual_ok_window_rejected[4] = {};
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
struct DeviceCalibratedModel;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ DeviceBuilderDiagnostic* g_builder_diagnostic = nullptr;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ DeviceCalibratedModel* g_builder_model_base = nullptr;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ unsigned long long g_builder_model_count = 0;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ int g_builder_diagnostic_level = 0;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
// CUDA 11.2 retains constructor initializers in its host stub. Give that
// host-only initializer a valid clock expression without changing GPU timing.
__device__ inline unsigned long long d_builder_clock() {
#ifdef __CUDA_ARCH__
    return clock64();
#else
    return 0;
#endif
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
struct DeviceBuilderCycleScope {
    unsigned long long* counter;
    unsigned long long start;
    __device__ DeviceBuilderCycleScope(unsigned long long* c)
        : counter(c), start(c ? d_builder_clock() : 0) {}
    __device__ void finish() {
        if (counter) { *counter += clock64() - start; counter = nullptr; }
    }
    __device__ ~DeviceBuilderCycleScope() { finish(); }
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
struct DeviceConfig;
} // namespace
} // namespace optiland
