#pragma once
#include "../prelude.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "asphere_slope.cuh"
#include "material_and_sag.cuh"

namespace optiland {
namespace {
__device__ double d_minimum_surface_tangent_axis_angle_deg(
    const DeviceSurface& surface,
    int sample_count) {
    if (surface.kind == kKindObject || surface.kind == kKindImage) {
        return NAN;
    }
    if (d_is_plane(surface)) {
        return 90.0;
    }
    const double aperture_radius = d_clear_aperture_radius(surface);
    if (!(aperture_radius > 0.0) || !d_finite(aperture_radius)) {
        return 90.0;
    }
    const int samples = max(2, min(4096, sample_count));
    double min_angle_deg = 90.0;
    for (int sample = 0; sample < samples; ++sample) {
        const double r = aperture_radius * static_cast<double>(sample) /
                         static_cast<double>(samples - 1);
        double radial_slope = 0.0;
        if (d_finite(surface.radius) && fabs(surface.radius) >= kDeviceEps &&
            fabs(surface.radius) <= 0.5 * kDeviceInf) {
            const double root_arg =
                1.0 - (1.0 + surface.conic) * r * r /
                          (surface.radius * surface.radius);
            if (!(root_arg > 0.0)) {
                return 0.0;
            }
            radial_slope += r / (surface.radius * sqrt(root_arg));
        }
        if (surface.kind == kKindEvenAsphere) {
            double radial_power = r;
            for (int i = 0;
                 i < surface.coeff_count && i < kMaxDeviceAsphereCoefficients;
                 ++i) {
                const double order = 2.0 * static_cast<double>(i + 1);
                radial_slope += order * surface.coeffs[i] * radial_power;
                radial_power *= r * r;
            }
        }
        if (!d_finite(radial_slope)) {
            return 0.0;
        }
        const double angle_deg = atan2(1.0, fabs(radial_slope)) *
                                 (180.0 / 3.14159265358979323846);
        min_angle_deg = fmin(min_angle_deg, angle_deg);
    }
    return min_angle_deg;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_mechanical_aperture_radius(const DeviceSurface& s) {
    if (s.mechanical_aperture > 0.0) {
        return s.mechanical_aperture;
    }
    return d_clear_aperture_radius(s);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_edge_sample_radius(const DeviceSurface& s) {
    return fmax(d_mechanical_aperture_radius(s), d_clear_aperture_radius(s));
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_surface_profile_radius_limit(const DeviceSurface& s) {
    if (d_is_plane(s)) {
        return kDeviceInf;
    }
    if (!d_finite(s.radius) || fabs(s.radius) < kDeviceEps ||
        fabs(s.radius) > 0.5 * kDeviceInf) {
        return s.kind == kKindEvenAsphere ? kDeviceInf : 0.0;
    }
    const double conic_term = 1.0 + s.conic;
    if (!(conic_term > 0.0)) {
        return kDeviceInf;
    }
    return fabs(s.radius) / sqrt(conic_term);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_edge_sag_radius(const DeviceSurface& s, double fallback_radius) {
    double radius = d_clear_aperture_radius(s);
    if (radius <= 0.0) {
        radius = d_mechanical_aperture_radius(s);
    }
    if (radius <= 0.0) {
        radius = fallback_radius;
    }
    return fmax(0.0, radius);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_profile_surface_sag(
    const DeviceSurface& s,
    double y,
    double profile_radius) {
    const double clear_radius = fmax(0.0, profile_radius);
    double clamped_y = y;
    if (clamped_y > clear_radius) {
        clamped_y = clear_radius;
    } else if (clamped_y < -clear_radius) {
        clamped_y = -clear_radius;
    }
    const double physical_limit = d_surface_profile_radius_limit(s);
    const double abs_y = fabs(clamped_y);
    const double effective_radius = fmin(abs_y, physical_limit);
    if (effective_radius <= 0.0) {
        return 0.0;
    }
    return d_sag(s, effective_radius);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_aperture_limited_edge_sag(
    const DeviceSurface& s,
    double sample_radius,
    double fallback_radius) {
    const double profile_radius = d_edge_sag_radius(s, fallback_radius);
    const double effective_radius =
        sample_radius > 0.0 ? sample_radius : profile_radius;
    return d_profile_surface_sag(s, effective_radius, profile_radius);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_edge_thickness_sample_radius(
    const DeviceSurface& a,
    const DeviceSurface& b,
    double fallback_radius) {
    const double radius_a = d_edge_sample_radius(a);
    const double radius_b = d_edge_sample_radius(b);
    double radius = 0.0;
    if (d_interval_is_glass(a)) {
        radius = fmax(radius_a, radius_b);
    } else if (radius_a > 0.0 && radius_b > 0.0) {
        radius = fmin(radius_a, radius_b);
    } else {
        radius = fmax(radius_a, radius_b);
    }
    if (radius <= 0.0) {
        radius = fallback_radius;
    }
    return fmax(0.0, radius);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_edge_thickness_at(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    int i) {
    if (i + 1 >= cfg.surface_count) {
        return NAN;
    }
    const DeviceSurface& a = surfaces[i];
    const DeviceSurface& b = surfaces[i + 1];
    if (!d_finite(a.thickness) || fabs(a.thickness) > 0.5 * kDeviceInf ||
        a.kind == kKindObject || b.kind == kKindObject) {
        return NAN;
    }
    const double r_max =
        d_edge_thickness_sample_radius(a, b, cfg.entrance_pupil_radius);
    if (!(r_max > 0.0)) {
        const double sag_a =
            d_aperture_limited_edge_sag(a, 0.0, cfg.entrance_pupil_radius);
        const double sag_b =
            d_aperture_limited_edge_sag(b, 0.0, cfg.entrance_pupil_radius);
        if (!d_finite(sag_a) || !d_finite(sag_b)) {
            return -INFINITY;
        }
        return a.thickness + sag_b - sag_a;
    }
    double min_edge = INFINITY;
    for (int sample = 0; sample <= 32; ++sample) {
        const double fraction = 0.5 + 0.5 * static_cast<double>(sample) / 32.0;
        const double radius = r_max * fraction;
        const double sag_a =
            d_aperture_limited_edge_sag(a, radius, cfg.entrance_pupil_radius);
        const double sag_b =
            d_aperture_limited_edge_sag(b, radius, cfg.entrance_pupil_radius);
        if (!d_finite(sag_a) || !d_finite(sag_b)) {
            return -INFINITY;
        }
        min_edge = fmin(min_edge, a.thickness + sag_b - sag_a);
    }
    return min_edge;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_stop_air_edge_thickness_at(
    const DeviceSurface* surfaces, const DeviceConfig& cfg, int stop) {
    if (stop <= 0 || stop + 1 >= cfg.surface_count) return NAN;
    const DeviceSurface& a = surfaces[stop - 1];
    const DeviceSurface& aperture = surfaces[stop];
    const DeviceSurface& b = surfaces[stop + 1];
    if (!aperture.is_stop || !d_is_plane(aperture) ||
        d_interval_is_glass(a) || d_interval_is_glass(aperture) ||
        a.kind == kKindObject || b.kind == kKindImage ||
        !d_finite(a.thickness) || !d_finite(aperture.thickness)) return NAN;
    const double fallback = cfg.entrance_pupil_radius;
    const double radius = d_edge_thickness_sample_radius(a, b, fallback);
    const double center = a.thickness + aperture.thickness;
    double minimum = center;
    // minimum already contains the axis; zero is the edge helper's rim sentinel.
    for (int sample = 1; sample <= 66; ++sample) {
        const double r = sample <= 64 ? radius * sample / 64.0
            : fmin(radius, d_edge_sag_radius(sample == 65 ? a : b, fallback));
        const double za = d_aperture_limited_edge_sag(a, r, fallback);
        const double zb = d_aperture_limited_edge_sag(b, r, fallback);
        if (!d_finite(za) || !d_finite(zb)) return -INFINITY;
        minimum = fmin(minimum, center + zb - za);
    }
    return minimum;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_min_edge_thickness(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg) {
    if (cfg.surface_count < 2) {
        return NAN;
    }
    double min_edge = INFINITY;
    for (int i = cfg.first_optical_surface; i + 1 < cfg.surface_count; ++i) {
        const DeviceSurface& a = surfaces[i];
        const DeviceSurface& b = surfaces[i + 1];
        if (a.kind == kKindObject || b.kind == kKindObject ||
            d_image_space_interval(surfaces, cfg.surface_count, i) ||
            !d_finite(a.thickness) || fabs(a.thickness) > 0.5 * kDeviceInf) {
            continue;
        }
        const double edge = d_edge_thickness_at(surfaces, cfg, i);
        if (!d_finite(edge)) {
            min_edge = -INFINITY;
            continue;
        }
        min_edge = fmin(min_edge, edge);
    }
    return min_edge;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_total_track(const DeviceSurface* surfaces, const DeviceConfig& cfg) {
    double sum = 0.0;
    for (int i = cfg.first_optical_surface; i < cfg.surface_count; ++i) {
        if (d_finite(surfaces[i].thickness) &&
            fabs(surfaces[i].thickness) < 0.5 * kDeviceInf) {
            sum += surfaces[i].thickness;
        }
    }
    return sum;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_bfl(const DeviceSurface* surfaces, const DeviceConfig& cfg) {
    if (cfg.surface_count < 2) {
        return NAN;
    }
    const int last_interval = cfg.surface_count - 2;
    const int reference = cfg.bfl_reference_surface >= 0
        ? max(
              min(cfg.bfl_reference_surface, last_interval),
              min(cfg.first_optical_surface, last_interval))
        : last_interval;
    double center_path = 0.0;
    double edge_path = 0.0;
    bool edge_path_finite = true;
    for (int index = reference; index <= last_interval; ++index) {
        const double center = surfaces[index].thickness;
        if (!d_finite(center)) {
            return center;
        }
        center_path += center;
        const double edge = d_edge_thickness_at(surfaces, cfg, index);
        if (d_finite(edge)) {
            edge_path += edge;
        } else {
            edge_path_finite = false;
        }
    }
    return edge_path_finite ? fmin(center_path, edge_path) : center_path;
}
} // namespace
} // namespace optiland
