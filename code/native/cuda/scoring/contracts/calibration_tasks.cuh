#pragma once
#include "../prelude.cuh"
#include "calibrated_model.cuh"
#include "constants.cuh"
#include "device_config.cuh"
#include "optical_records.cuh"
#include "../diagnostics/asphere_profile_records.cuh"

namespace optiland {
namespace {
struct DeviceCalibratedCenterFootprint {
    int valid = 0;
    DeviceSeed chief_seed;
    DeviceSeed edge_vectors[4];
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
struct DeviceEdgeStopSample {
    int valid = 0;
    DeviceSeed seed;
    double sx = 0.0;
    double sy = 0.0;
    double residual = INFINITY;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __forceinline__ int d_uc5_counter_cell(
    const DeviceConfig& cfg,
    double wavelength_um,
    int anchor_index,
    bool repair_build) {
    const int wave_role =
        fabs(wavelength_um - cfg.primary_wavelength) <= 1.0e-10 ? 0 : 1;
    return ((repair_build ? 1 : 0) * kUC5CounterWaveRoles + wave_role) *
               kUC5CounterAnchors +
           max(0, min(kUC5CounterAnchors - 1, anchor_index));
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __forceinline__ int d_uc5_encode_edge_role(int cell) {
    // Low byte remains the semantic deep-profile role consumed by the tracer.
    // The upper bytes carry a UC5-only counter cell without widening the hot
    // secant ABI.
    return kAsphereDeepRoleEdge | ((cell + 1) << 8);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
struct DeviceUC5CandidateFamilyState {
    DeviceEdgeStopSample best_sample;
    DeviceEdgeStopSample previous_sample;
    DeviceSeed best_full_seed;
    DeviceCalibratedEdgeAnchor anchor;
    double max_residual = 0.0;
    int prepared = 0;
    int skipped_existing_valid = 0;
    int cached_traceable = 0;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
static_assert(
    sizeof(DeviceUC5CandidateFamilyState) <= 256,
    "UC5 continuation state must remain compact");
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kBudgetChiefSeedCapacity = 64;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
struct DeviceBudgetChiefSolveContext {
    double inverse_a = 0.0;
    double target_x = 0.0;
    double target_y = 0.0;
    double physical_stop_radius = 0.0;
    double convergence_limit = 0.0;
    double accept_limit = 0.0;
    int valid = 0;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
struct DeviceOrderedChiefTask {
    DeviceSeed trial_seeds[kBudgetChiefSeedCapacity];
    DeviceBudgetChiefSolveContext solve_context;
    DeviceSeed chief_seed;
    double stop_x = 0.0;
    double stop_y = 0.0;
    double residual = INFINITY;
    int trial_count = 0;
    int require_convergence = 0;
    int chief_valid = 0;
    int cooperative_exhausted = 0;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
struct DeviceOrderedChiefTrialResult {
    DeviceSeed chief_seed;
    double stop_x = 0.0;
    double stop_y = 0.0;
    double residual = INFINITY;
    int success = 0;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
enum DeviceCooperativeBridgeStatus : int {
    kCooperativeBridgeInactive = 0,
    kCooperativeBridgeReady = 1,
    kCooperativeBridgeWaiting = 2,
    kCooperativeTargetReady = 3,
    kCooperativeTargetWaiting = 4,
    kCooperativeBridgeSolved = 5,
    kCooperativeBridgeFailed = 6,
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
struct DeviceCooperativeBridgeTask {
    DeviceSeed trial_seeds[kBudgetChiefSeedCapacity];
    DeviceBudgetChiefSolveContext solve_context;
    DeviceField solve_field;
    DeviceSeed base_seed;
    DeviceSeed target_abcd_seed;
    double base_rho = 0.0;
    double target_rho = 0.0;
    double candidate_rho = 0.0;
    double center_radius = 0.0;
    double stop_radius = 0.0;
    int trial_begin = 0;
    int trial_count = 0;
    int next_bridge_index = 0;
    int require_convergence = 0;
    int built_bridge = 0;
    int has_inner = 0;
    int status = kCooperativeBridgeInactive;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
struct DeviceOrderedChiefStats {
    unsigned long long prepared = 0;
    unsigned long long paraxial_accepted = 0;
    unsigned long long trial_tasks = 0;
    unsigned long long trial_seed_sum = 0;
    unsigned long long solved_buckets[8] = {};
    unsigned long long serial_fallback = 0;
    unsigned long long serial_fallback_solved = 0;
    unsigned long long serial_fallback_failed = 0;
    unsigned long long cooperative_eligible = 0;
    unsigned long long cooperative_bridge_stages = 0;
    unsigned long long cooperative_bridge_seed0_solved = 0;
    unsigned long long cooperative_bridge_tile_solved = 0;
    unsigned long long cooperative_target_stages = 0;
    unsigned long long cooperative_target_seed0_solved = 0;
    unsigned long long cooperative_target_tile_solved = 0;
    unsigned long long cooperative_failed = 0;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __noinline__ bool f_sample_stop_from_seed_spherical(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceField& field,
    double wavelength_um,
    const DeviceSeed& seed,
    float* stop_x,
    float* stop_y);
} // namespace
} // namespace optiland
