#pragma once
#include "../prelude.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/device_config.cuh"
#include "asphere_profile_records.cuh"
#include "../geometry/asphere_slope.cuh"

namespace optiland {
namespace {
__device__ __forceinline__ bool d_asphere_deep_close(
    double a,
    double b,
    double relative_tolerance = 1.0e-8) {
    if (!d_finite(a) || !d_finite(b)) {
        return !d_finite(a) && !d_finite(b);
    }
    const double scale = fmax(1.0, fmax(fabs(a), fabs(b)));
    return fabs(a - b) <= relative_tolerance * scale;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
template <bool Hybrid>
__device__ __forceinline__ void d_record_asphere_deep_model_comparison(
    const DeviceConfig& cfg,
    const DeviceCalibratedModel& p0_model,
    const DeviceCalibratedModel& candidate_model) {
    DeviceAsphereDeepProfile* profile = cfg.asphere_deep_profile;
    if (!profile) {
        return;
    }
    unsigned long long* comparisons = Hybrid
        ? &profile->hybrid_model_comparisons
        : &profile->model_comparisons;
    unsigned long long* both_valid = Hybrid
        ? &profile->hybrid_model_p0_valid_hybrid_valid
        : &profile->model_p0_valid_u1_valid;
    unsigned long long* p0_only = Hybrid
        ? &profile->hybrid_model_p0_valid_hybrid_invalid
        : &profile->model_p0_valid_u1_invalid;
    unsigned long long* candidate_only = Hybrid
        ? &profile->hybrid_model_p0_invalid_hybrid_valid
        : &profile->model_p0_invalid_u1_valid;
    unsigned long long* both_invalid = Hybrid
        ? &profile->hybrid_model_both_invalid
        : &profile->model_both_invalid;
    unsigned long long* equivalent_count = Hybrid
        ? &profile->hybrid_model_both_valid_equivalent
        : &profile->model_both_valid_equivalent;
    unsigned long long* different_count = Hybrid
        ? &profile->hybrid_model_both_valid_different
        : &profile->model_both_valid_different;
    unsigned long long* equal_edge_masks = Hybrid
        ? &profile->hybrid_model_equal_edge_valid_masks
        : &profile->model_equal_edge_valid_masks;
    atomicAdd(comparisons, 1ULL);
    if (p0_model.valid && candidate_model.valid) {
        atomicAdd(both_valid, 1ULL);
    } else if (p0_model.valid) {
        atomicAdd(p0_only, 1ULL);
        return;
    } else if (candidate_model.valid) {
        atomicAdd(candidate_only, 1ULL);
        return;
    } else {
        atomicAdd(both_invalid, 1ULL);
        return;
    }

    bool equivalent =
        p0_model.chief_seed.valid == candidate_model.chief_seed.valid &&
        d_asphere_deep_close(p0_model.chief_seed.x, candidate_model.chief_seed.x) &&
        d_asphere_deep_close(p0_model.chief_seed.y, candidate_model.chief_seed.y) &&
        d_asphere_deep_close(p0_model.chief_stop_x, candidate_model.chief_stop_x) &&
        d_asphere_deep_close(p0_model.chief_stop_y, candidate_model.chief_stop_y) &&
        d_asphere_deep_close(p0_model.fit_rms, candidate_model.fit_rms);
    unsigned int p0_edge_mask = 0;
    unsigned int candidate_edge_mask = 0;
    for (int edge = 0; edge < 4; ++edge) {
        const DeviceCalibratedEdgeAnchor& p0 = p0_model.edge_anchors[edge];
        const DeviceCalibratedEdgeAnchor& candidate =
            candidate_model.edge_anchors[edge];
        if (p0.valid && p0.seed.valid) {
            p0_edge_mask |= 1U << edge;
        }
        if (candidate.valid && candidate.seed.valid) {
            candidate_edge_mask |= 1U << edge;
        }
        equivalent = equivalent &&
            p0.valid == candidate.valid &&
            p0.seed.valid == candidate.seed.valid &&
            d_asphere_deep_close(p0.seed.x, candidate.seed.x) &&
            d_asphere_deep_close(p0.seed.y, candidate.seed.y);
    }
    if (p0_edge_mask == candidate_edge_mask) {
        atomicAdd(equal_edge_masks, 1ULL);
    }
    atomicAdd(
        equivalent
            ? equivalent_count
            : different_count,
        1ULL);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __forceinline__ DeviceConfig d_asphere_deep_u1_model_config(
    DeviceConfig cfg) {
    cfg.asphere_root_profile = nullptr;
    cfg.asphere_feasibility_profile = nullptr;
    cfg.asphere_deep_profile = nullptr;
    cfg.asphere_initial_root_mode = 4;
    cfg.asphere_upper_bound_mode = 2;
    cfg.asphere_slope_root_domain_enabled = 1;
    cfg.asphere_slope_root_domain_reuse = 0;
    cfg.asphere_aiming_local_branch = 0;
    cfg.asphere_auxiliary_local_branch = 0;
    return cfg;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __forceinline__ DeviceConfig d_asphere_deep_hybrid_model_config(
    DeviceConfig cfg) {
    cfg = d_asphere_deep_u1_model_config(cfg);
    cfg.asphere_exact_s1_seed_bypass = 1;
    return cfg;
}
} // namespace
} // namespace optiland
