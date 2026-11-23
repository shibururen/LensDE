#pragma once
#include "../prelude.cuh"
#include "runtime_options.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/optical_records.cuh"

namespace optiland {
namespace {
std::vector<DeviceField> make_device_fields(const LensSystem& base) {
    std::vector<DeviceField> out;
    const std::size_t count = base.fields.empty() ? 1 : base.fields.size();
    out.reserve(count);
    if (base.fields.empty()) {
        out.push_back(DeviceField{});
        return out;
    }
    for (const Field& field : base.fields) {
        DeviceField dst;
        dst.hx = field.hx;
        dst.hy = field.hy;
        dst.fvcx = field.fvcx;
        dst.fvcy = field.fvcy;
        dst.fvdx = field.fvdx;
        dst.fvdy = field.fvdy;
        out.push_back(dst);
    }
    return out;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
int host_center_field_index(const std::vector<DeviceField>& fields) {
    int best = 0;
    double best_field_score = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const double field_score = std::hypot(fields[i].hx, fields[i].hy);
        if (field_score < best_field_score) {
            best = static_cast<int>(i);
            best_field_score = field_score;
        }
    }
    return best;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
std::vector<DeviceWave> make_device_waves(const LensSystem& base) {
    std::vector<DeviceWave> out;
    const std::size_t count = base.wavelengths.empty() ? 1 : base.wavelengths.size();
    out.reserve(count);
    if (base.wavelengths.empty()) {
        DeviceWave wave;
        wave.primary = 1;
        out.push_back(wave);
        return out;
    }
    for (const Wavelength& wave : base.wavelengths) {
        DeviceWave dst;
        dst.value_um = wave.value_um;
        dst.weight = wave.weight;
        dst.primary = wave.primary ? 1 : 0;
        out.push_back(dst);
    }
    return out;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
void make_device_glass_proximity(
    const MeritConfig& merit,
    std::vector<DeviceGlassProximity>* constraints,
    std::vector<DeviceGlassProximityPoint>* points) {
    constraints->clear();
    points->clear();
    constraints->reserve(merit.glass_proximity.size());
    for (const GlassProximityConstraint& constraint : merit.glass_proximity) {
        if (constraints->size() >= kMaxDeviceGlassProximity ||
            points->size() >= kMaxDeviceGlassProximityPoints) {
            break;
        }
        DeviceGlassProximity dst;
        dst.index = static_cast<int>(constraint.index);
        dst.offset = static_cast<int>(points->size());
        dst.weight = constraint.weight;
        dst.tolerance = constraint.tolerance;
        dst.region_weight = constraint.region_weight;
        dst.region_tolerance = constraint.region_tolerance;
        dst.nd_scale = constraint.nd_scale;
        dst.vd_scale = constraint.vd_scale;
        dst.vd_relative_scale = constraint.vd_relative_scale;
        const std::string vd_metric = normalized_token(constraint.vd_metric);
        dst.vd_metric =
            vd_metric == "relative_log" || vd_metric == "relative" || vd_metric == "log"
                ? 1
                : 0;
        dst.smooth_temperature = constraint.smooth_temperature;
        for (const GlassProximityPoint& point : constraint.points) {
            if (points->size() >= kMaxDeviceGlassProximityPoints) {
                break;
            }
            points->push_back(DeviceGlassProximityPoint{point.nd, point.vd});
            ++dst.count;
        }
        dst.region_offset = static_cast<int>(points->size());
        for (const GlassProximityPoint& point : constraint.region_points) {
            if (points->size() >= kMaxDeviceGlassProximityPoints) {
                break;
            }
            points->push_back(DeviceGlassProximityPoint{point.nd, point.vd});
            ++dst.region_count;
        }
        if (dst.count > 0 || dst.region_count > 0) {
            constraints->push_back(dst);
        }
    }
}
} // namespace
} // namespace optiland
