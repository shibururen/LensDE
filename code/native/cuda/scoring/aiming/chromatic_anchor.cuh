#pragma once
#include "../prelude.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/calibration_tasks.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../geometry/asphere_slope.cuh"

namespace optiland {
namespace {
__device__ int d_center_field_index(const DeviceField* fields, const DeviceConfig& cfg) {
    if (!fields || cfg.field_count <= 0) {
        return 0;
    }
    int best = 0;
    double best_field_score = hypot(fields[0].hx, fields[0].hy);
    if (!d_finite(best_field_score)) {
        best_field_score = INFINITY;
    }
    for (int i = 1; i < cfg.field_count; ++i) {
        double field_score = hypot(fields[i].hx, fields[i].hy);
        if (!d_finite(field_score)) {
            field_score = INFINITY;
        }
        if (field_score < best_field_score) {
            best = i;
            best_field_score = field_score;
        }
    }
    return best;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ const DeviceField* d_candidate_row_fields(
    const DeviceField* fallback_fields,
    const DeviceConfig& cfg,
    int row) {
    return cfg.candidate_fields
               ? cfg.candidate_fields +
                     static_cast<long long>(row) * cfg.field_count
               : fallback_fields;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ DeviceCalibratedCenterFootprint d_make_center_footprint(
    const DeviceCalibratedModel& center_model) {
    DeviceCalibratedCenterFootprint footprint;
    if (!center_model.valid || !center_model.chief_seed.valid ||
        !center_model.calibrated_edges_all_valid) {
        return footprint;
    }
    footprint.valid = 1;
    footprint.chief_seed = center_model.chief_seed;
    for (int i = 0; i < 4; ++i) {
        const DeviceCalibratedEdgeAnchor& anchor = center_model.edge_anchors[i];
        if (!anchor.valid || !anchor.seed.valid) {
            footprint.valid = 0;
            return footprint;
        }
        footprint.edge_vectors[i].x = anchor.seed.x - center_model.chief_seed.x;
        footprint.edge_vectors[i].y = anchor.seed.y - center_model.chief_seed.y;
        footprint.edge_vectors[i].valid = 1;
    }
    return footprint;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_same_field_direction(
    const DeviceField& a,
    const DeviceField& b) {
    const double ra = hypot(a.hx, a.hy);
    const double rb = hypot(b.hx, b.hy);
    if (ra <= 1.0e-12 || rb <= 1.0e-12) {
        return true;
    }
    const double cross = fabs(a.hx * b.hy - a.hy * b.hx);
    const double dot = a.hx * b.hx + a.hy * b.hy;
    return dot >= 0.0 && cross <= 1.0e-6 * ra * rb;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ const DeviceCalibratedModel* d_primary_anchored_same_field_wave_model(
    const DeviceCalibratedModel* models,
    long long model_index,
    const DeviceConfig& cfg,
    int wave_index) {
    if (!models || cfg.wavelength_count <= 1) {
        return nullptr;
    }
    const int primary_wave_index =
        cfg.primary_wave_index >= 0 &&
                cfg.primary_wave_index < cfg.wavelength_count
            ? cfg.primary_wave_index
            : 0;
    // Prefer the primary wavelength for secondary models.  During the later
    // primary repair pass, an already-valid adjacent secondary model may also
    // be used as a deterministic *initial seed* source.  The repaired primary
    // anchors still have to pass the normal traced STOP residual/validity
    // checks. Keep the bounded primary recovery (E3) enabled with this reuse:
    // the outer_05 regression shows that seed reuse plus E4a alone can converge
    // to a different STOP-valid branch and miss the physical-support penalty.
    if (wave_index != primary_wave_index) {
        const long long field_model_base = model_index - wave_index;
        const DeviceCalibratedModel* primary =
            models + field_model_base + primary_wave_index;
        if (primary->valid) {
            return primary;
        }
    }
    if (wave_index > 0) {
        const DeviceCalibratedModel* previous = models + model_index - 1;
        if (previous->valid) {
            return previous;
        }
    }
    if (wave_index + 1 < cfg.wavelength_count) {
        const DeviceCalibratedModel* next = models + model_index + 1;
        if (next->valid) {
            return next;
        }
    }
    return nullptr;
}
} // namespace
} // namespace optiland
