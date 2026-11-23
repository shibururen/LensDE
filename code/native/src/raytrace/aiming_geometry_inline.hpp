#pragma once
#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

namespace raytrace_detail {
inline double condition_number_2x2(double a, double b, double c, double d) {
    const double det = a * d - b * c;
    if (!std::isfinite(det) || std::abs(det) <= 1.0e-14) {
        return std::numeric_limits<double>::infinity();
    }
    const double norm_j = std::sqrt(a * a + b * b + c * c + d * d);
    const double ia = d / det;
    const double ib = -b / det;
    const double ic = -c / det;
    const double id = a / det;
    const double norm_inv = std::sqrt(ia * ia + ib * ib + ic * ic + id * id);
    return norm_j * norm_inv;
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline void append_unique_positive(std::vector<double>& values, double value) {
    if (!std::isfinite(value) || value <= 0.0) {
        return;
    }
    for (double existing : values) {
        if (std::abs(existing - value) <=
            1.0e-9 * std::max({1.0, std::abs(existing), std::abs(value)})) {
            return;
        }
    }
    values.push_back(value);
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline double aperture_aware_halfwidth(double base_halfwidth, double min_halfwidth, double stop_scale) {
    const double safe_min = std::max(1.0e-9, min_halfwidth);
    const double safe_base = std::max(safe_min, base_halfwidth);
    if (!std::isfinite(stop_scale) || stop_scale <= 0.0) {
        return safe_base;
    }
    const double aperture_width = std::max(safe_min, 2.0 * stop_scale);
    return std::clamp(aperture_width, safe_min, safe_base);
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline double aperture_aware_probe_step(
    double base_step,
    double min_step,
    double max_step,
    double stop_scale) {
    const double safe_min = std::max(1.0e-9, min_step);
    const double safe_max = std::max(safe_min, max_step);
    double step = std::clamp(base_step, safe_min, safe_max);
    if (std::isfinite(stop_scale) && stop_scale > 0.0) {
        step = std::min(step, std::max(safe_min, 0.25 * stop_scale));
    }
    return std::clamp(step, safe_min, safe_max);
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline std::vector<double> aperture_aware_probe_steps(
    double first_surface_radius,
    double stop_scale,
    double probe_fraction,
    double min_probe_step,
    double max_probe_step) {
    const double safe_min = std::max(1.0e-9, min_probe_step);
    const double safe_max = std::max(safe_min, max_probe_step);
    const double base = std::clamp(
        probe_fraction * std::max(1.0e-9, first_surface_radius),
        safe_min,
        safe_max);
    std::vector<double> steps;
    append_unique_positive(steps, base);
    if (std::isfinite(stop_scale) && stop_scale > 0.0) {
        append_unique_positive(steps, std::clamp(0.25 * stop_scale, safe_min, safe_max));
        append_unique_positive(steps, std::clamp(0.125 * stop_scale, safe_min, safe_max));
    }
    for (double h = base * 0.5; h >= safe_min * (1.0 - 1.0e-12); h *= 0.5) {
        append_unique_positive(steps, std::clamp(h, safe_min, safe_max));
        if (h <= safe_min) {
            break;
        }
    }
    return steps;
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline Vec3 field_direction_cosines(const LensSystem& system, const Field& field) {
    const double field_radius = std::hypot(field.hx, field.hy);
    if (field_radius <= kEps || std::abs(system.max_field_deg) <= kEps) {
        return {0.0, 0.0, 1.0};
    }
    const double theta = deg_to_rad(system.max_field_deg) * field_radius;
    const double transverse = std::sin(theta);
    return normalize({
        transverse * field.hx / field_radius,
        transverse * field.hy / field_radius,
        std::cos(theta),
    });
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline double infinite_object_start_distance(
    const LensSystem& system,
    double aperture_radius,
    double target_z) {
    if (std::isfinite(system.ray_aiming.start_offset) && system.ray_aiming.start_offset > 0.0) {
        return system.ray_aiming.start_offset;
    }
    return std::max({
        10.0,
        std::abs(system.total_track()) + std::abs(target_z) + 4.0 * std::abs(aperture_radius) + 10.0,
    });
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline double infinite_object_aim_position_step_cap(const LensSystem& system, double stop_radius) {
    const double track = std::isfinite(system.total_track()) ? std::abs(system.total_track()) : 0.0;
    const double theta = std::isfinite(system.max_field_deg)
                             ? std::abs(deg_to_rad(system.max_field_deg))
                             : 0.0;
    const double field_slope = std::isfinite(theta)
                                   ? std::min(64.0, std::abs(std::tan(theta)))
                                   : 0.0;
    const double wide_field_scale = 0.10 * track * field_slope;
    return std::max({1.0, std::abs(stop_radius), wide_field_scale});
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline bool set_finite_object_ray(
    Ray* ray,
    const Vec3& target,
    const Vec3& direction,
    double object_distance) {
    if (!ray || std::abs(direction.z) <= kEps) {
        return false;
    }
    const double distance_to_target = (target.z + object_distance) / direction.z;
    if (!std::isfinite(distance_to_target) || distance_to_target <= kEps) {
        return false;
    }
    ray->p = target - direction * distance_to_target;
    ray->d = direction;
    return std::isfinite(ray->p.x) && std::isfinite(ray->p.y) && std::isfinite(ray->p.z);
}
} // namespace raytrace_detail

} // namespace optiland
