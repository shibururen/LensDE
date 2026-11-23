#pragma once
#include "../prelude.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../contracts/ray_record.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/first_order_metrics.cuh"

namespace optiland {
namespace {
__device__ double d_ray_nominal_weight(const DeviceRay& ray) {
    if (d_finite(ray.nominal_weight) && ray.nominal_weight > 0.0) {
        return ray.nominal_weight;
    }
    if (d_finite(ray.intensity) && ray.intensity > 0.0) {
        return ray.intensity;
    }
    return 0.0;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_ray_progress(const DeviceRay& ray, int surface_count) {
    if (ray.valid && d_finite(ray.x) && d_finite(ray.y) && d_finite(ray.z)) {
        return 1.0;
    }
    double progress = 0.0;
    if (ray.failure_surface >= 0 && surface_count > 1) {
        progress = d_clamp(
            static_cast<double>(ray.failure_surface + 1) /
                static_cast<double>(surface_count),
            0.0,
            1.0);
    }
    if (ray.failure_code == 1 && d_finite(ray.aim_residual)) {
        const double scale = fmax(1.0e-9, fabs(ray.aim_target_radius));
        const double residual = fmax(0.0, ray.aim_residual / scale);
        progress = fmax(progress, 0.5 / (1.0 + residual));
    }
    return d_clamp(progress, 0.0, 1.0);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_ray_failure_penalty(
    const DeviceRay& ray,
    const DeviceConfig& cfg) {
    const double weight = d_ray_nominal_weight(ray);
    const double severity =
        d_finite(ray.failure_severity) ? fmax(0.0, ray.failure_severity) : 1.0;
    const double curved = log1p(severity * severity);
    switch (ray.failure_code) {
        case 1:
            return weight * fabs(cfg.aim_failure_weight) * curved;
        case 3:
            return weight * fabs(cfg.aperture_failure_weight) * curved;
        case 4:
            return weight * fabs(cfg.tir_failure_weight) * log1p(severity + 1.0);
        case 2:
        case 7:
            return weight * fabs(cfg.surface_miss_weight) * log1p(severity + 1.0);
        case 6:
            return weight * fabs(cfg.backward_failure_weight) * curved;
        case 5:
        case 8:
            return weight * fabs(cfg.trace_failure_weight) * log1p(severity + 1.0);
        case 0:
        default:
            return weight * fabs(cfg.trace_failure_weight);
    }
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_ray_aim_usable_for_spot(const DeviceRay& ray) {
    return ray.aim_converged != 0;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_ray_usable_for_spot(const DeviceRay& ray) {
    return ray.valid && !ray.aperture_blocked &&
           ray.intensity > 0.0 &&
           d_finite(ray.intensity) &&
           d_finite(ray.x) &&
           d_finite(ray.y) &&
           d_ray_aim_usable_for_spot(ray);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_calibrated_paraxial_stop_sampling_rejection(
    const DeviceRay& ray,
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg) {
    if (d_normal_secondary_blockage(ray)) return true;
    const int stop = d_stop_index(surfaces, cfg);
    return cfg.aiming_mode == 5 &&
           stop >= 0 &&
           ray.aim_converged != 0 &&
           ray.backward_propagation == 0 &&
           ray.failure_code == 3 &&
           ray.failure_surface == stop &&
           ray.stop_reached != 0;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_field_is_off_axis(const DeviceField& field) {
    return hypot(field.hx, field.hy) > 1.0e-9;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_clamp01(double value) {
    return d_clamp(value, 0.0, 1.0);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_image_field_weight(
    const DeviceConfig& cfg,
    const DeviceField& field) {
    if (cfg.image_field_edge_power == 2) {
        return hypot(field.hx, field.hy) <= 1.0e-9 ? 2.0 : 1.0;
    }
    if (!cfg.image_field_edge_power) {
        return 1.0;
    }
    const double radius = d_clamp01(hypot(field.hx, field.hy));
    return 1.0 +
           (fmax(1.0, cfg.image_field_edge_weight) - 1.0) *
               pow(radius, fmax(0.1, cfg.image_field_weight_exponent));
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ int d_chromatic_family_count(const DeviceConfig& cfg) {
    return (cfg.operand_lateral_color ? 1 : 0) +
           (cfg.operand_axial_color ? 1 : 0) +
           (cfg.operand_worst_wavelength_spot ? 1 : 0) +
           (cfg.operand_spectral_spot_spread ? 1 : 0);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_chromatic_family_budget(const DeviceConfig& cfg) {
    const int count = d_chromatic_family_count(cfg);
    return count > 0
               ? fabs(cfg.unified_image_chromatic_weight) /
                     static_cast<double>(count)
               : 0.0;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_total_image_field_weight(
    const DeviceField* fields,
    const DeviceConfig& cfg) {
    double total = 0.0;
    for (int fi = 0; fi < cfg.field_count; ++fi) {
        total += d_image_field_weight(cfg, fields[fi]);
    }
    return total;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_smoothstep(double value, double lo, double hi) {
    if (!(hi > lo)) {
        return value >= hi ? 1.0 : 0.0;
    }
    const double t = d_clamp01((value - lo) / (hi - lo));
    return t * t * (3.0 - 2.0 * t);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_pseudo_huber(double residual, double delta) {
    if (!d_finite(residual)) {
        return 1.0e12;
    }
    const double d = fmax(1.0e-9, fabs(delta));
    const double scaled = residual / d;
    return d * d * (sqrt(1.0 + scaled * scaled) - 1.0);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_quality_reference(double value, double target, double lower, double upper, double fallback) {
    if (d_finite(target) && fabs(target) > kDeviceEps) {
        return fabs(target);
    }
    if (d_finite(lower) && fabs(lower) > kDeviceEps) {
        return fabs(lower);
    }
    if (d_finite(upper) && fabs(upper) > kDeviceEps) {
        return fabs(upper);
    }
    if (d_finite(value) && fabs(value) > kDeviceEps) {
        return fabs(value);
    }
    return fallback;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_first_order_tolerance(
    const DeviceConfig& cfg,
    int name,
    double value,
    double target,
    double lower,
    double upper) {
    const double abs_floor = fmax(1.0e-9, cfg.unified_length_abs_tol);
    const double ref = d_quality_reference(value, target, lower, upper, 1.0);
    if (name == 0) {
        return fmax(abs_floor, fabs(cfg.unified_efl_rel_tol) * ref);
    }
    if (name == 1) {
        return fmax(abs_floor, fabs(cfg.quality_ttl_rel_tol) * ref);
    }
    if (name == 2) {
        return fmax(abs_floor, fabs(cfg.quality_bfl_rel_tol) * ref);
    }
    if (name == 3) {
        return fmax(1.0e-9, fabs(cfg.quality_working_f_number_tol));
    }
    return abs_floor;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_image_tolerance(const DeviceConfig& cfg, int name) {
    if (name == 0 || name == 1) {
        return fmax(1.0e-9, fmax(cfg.spot_target, cfg.quality_spot_scale));
    }
    if (name == 2) {
        return fmax(1.0e-9, cfg.quality_distortion_tol);
    }
    if (name == 3) {
        return fmax(1.0e-9, cfg.quality_chief_ray_angle_tol);
    }
    if (name == 4) {
        return fmax(1.0e-9, cfg.quality_lateral_color_tol);
    }
    if (name == 5) {
        return fmax(1.0e-9, cfg.quality_spectral_spot_spread_tol);
    }
    if (name == 6) {
        return fmax(1.0e-9, cfg.quality_axial_color_tol);
    }
    return fmax(1.0e-9, cfg.quality_bound_tol);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_constraint_tolerance(const DeviceConfig& cfg, int kind) {
    if (kind == 0) {
        return fmax(1.0e-9, cfg.quality_edge_tol);
    }
    if (kind == 1) {
        return fmax(1.0e-9, cfg.quality_ratio_tol);
    }
    if (kind == 2) {
        return fmax(1.0e-9, cfg.quality_surface_tangent_angle_tol_deg);
    }
    return fmax(1.0e-9, cfg.quality_bound_tol);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_equivalent_spot_residual(
    const DeviceConfig& cfg,
    double delta,
    double tolerance) {
    const double equivalent_scale = fmax(1.0e-9, cfg.quality_spot_scale);
    const double spot_normalizer =
        fmax(1.0e-9, fmax(cfg.spot_target, cfg.quality_spot_scale));
    const double equivalent_spot =
        equivalent_scale * delta / fmax(1.0e-9, tolerance);
    return equivalent_spot / spot_normalizer;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_upper_delta(double value, double upper) {
    if (!d_finite(value)) {
        return 1.0e6;
    }
    return d_finite(upper) ? fmax(0.0, value - upper) : 0.0;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_lower_delta(double value, double lower) {
    if (!d_finite(value)) {
        return 1.0e6;
    }
    return d_finite(lower) ? fmax(0.0, lower - value) : 0.0;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_range_delta(double value, double lower, double upper) {
    if (!d_finite(value)) {
        return 1.0e6;
    }
    if (d_finite(lower) && value < lower) {
        return lower - value;
    }
    if (d_finite(upper) && value > upper) {
        return value - upper;
    }
    return 0.0;
}
} // namespace
} // namespace optiland
