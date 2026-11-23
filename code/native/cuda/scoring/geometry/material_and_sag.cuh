#pragma once
#include "../prelude.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "asphere_slope.cuh"

namespace optiland {
namespace {
__device__ bool d_effectively_finite_distance(double value) {
    return d_finite(value) && fabs(value) < 0.5 * kDeviceInf;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_active_lower_bound(double value) {
    return d_finite(value) && value > 0.0 ? value : -kDeviceInf;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_active_upper_bound(double value) {
    return d_finite(value) && value > 0.0 ? value : kDeviceInf;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_is_plane(const DeviceSurface& s) {
    return s.kind == kKindPlane || s.kind == kKindImage || s.kind == kKindObject ||
           (s.kind != kKindEvenAsphere &&
            (!d_finite(s.radius) || fabs(s.radius) < kDeviceEps ||
             fabs(s.radius) > 0.5 * kDeviceInf));
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_interval_is_glass(const DeviceSurface& s) {
    return d_finite(s.nd) && s.nd > 1.0005;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_pupil_order_air_interval(const DeviceSurface& surface) {
    return d_finite(surface.nd) && fabs(surface.nd - 1.0) <= 1.0e-12 &&
           d_finite(surface.vd) && fabs(surface.vd) <= 1.0e-12;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ int d_pupil_order_last_optical_surface(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg) {
    for (int si = cfg.surface_count - 1; si >= cfg.first_optical_surface; --si) {
        const DeviceSurface& surface = surfaces[si];
        if (surface.kind == kKindImage || surface.kind == kKindObject) {
            continue;
        }
        // Match CPU: exclude image-space air auxiliaries, but retain flat
        // glass exit faces and mirrors. Do not use the BFL measurement marker.
        const bool air_before = si == 0 || d_pupil_order_air_interval(surfaces[si - 1]);
        if (surface.reflective || !air_before || !d_pupil_order_air_interval(surface)) {
            return si;
        }
    }
    return cfg.first_optical_surface;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_image_space_interval(
    const DeviceSurface* surfaces,
    int surface_count,
    int i) {
    return i + 1 < surface_count && surfaces[i + 1].kind == kKindImage;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_refractive_index(double nd, double vd, double wavelength_um) {
    if (!d_finite(nd)) {
        return 1.0;
    }
    if (vd <= 0.0 || fabs(nd - 1.0) < 1.0e-10) {
        return nd;
    }
    const double inv_v = 1.0 / vd;
    const double inv_v2 = inv_v * inv_v;
    const double nd_sq = nd * nd;
    const double nd_div_v = nd / vd;
    const double v1 =
        0.004160 + 4.462559 * inv_v + 2.326660 * inv_v2 + 0.002330 * nd -
        0.003697 * nd_sq - 4.697604 * nd_div_v;
    const double v2 =
        0.066434 - 7.636396 * inv_v + 12.597434 * inv_v2 -
        0.037014 * nd_sq + 5.551013 * nd_div_v;
    const double v3 =
        -0.032218 + 2.230357 * inv_v - 103.318994 * inv_v2 -
        0.009654 * nd_sq + 1.934983 * nd_div_v;
    constexpr double wave_ref = 0.5875618;
    constexpr double alpha = 2.5;
    const double dlambda = wavelength_um - wave_ref;
    const double omega = dlambda / (1.0 + alpha * dlambda);
    return nd + v1 * omega + v2 * omega * omega + v3 * omega * omega * omega;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ int d_configured_wave_index(
    const DeviceConfig& cfg,
    double wavelength_um) {
    int best = -1;
    double best_delta = INFINITY;
    for (int i = 0; i < cfg.wavelength_count && i < kMaxDeviceWaves; ++i) {
        const double delta = fabs(cfg.wavelength_values[i] - wavelength_um);
        if (delta < best_delta) {
            best = i;
            best_delta = delta;
        }
    }
    return best_delta <= 1.0e-10 ? best : -1;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_surface_refractive_index(
    const DeviceSurface& surface,
    const DeviceConfig& cfg,
    double wavelength_um) {
    const int wave_index = d_configured_wave_index(cfg, wavelength_um);
    if (wave_index >= 0 && wave_index < kMaxDeviceMaterialIndices &&
        (surface.evaluator_material_index_mask & (1 << wave_index)) != 0) {
        const double exact = surface.evaluator_material_indices[wave_index];
        if (d_finite(exact) && exact > 0.0) {
            return exact;
        }
    }
    if (surface.material_index_offset >= 0 && cfg.material_indices) {
        if (wave_index >= 0) {
            const double exact =
                cfg.material_indices[surface.material_index_offset + wave_index];
            if (d_finite(exact) && exact > 0.0) {
                return exact;
            }
        }
    }
    return d_refractive_index(surface.nd, surface.vd, wavelength_um);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_radius_from_curvature(double curvature) {
    if (!d_finite(curvature) || fabs(curvature) < 1.0e-14) {
        return kDeviceInf;
    }
    return 1.0 / curvature;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_surface_vertex_z(
    const DeviceSurface* surfaces,
    int surface_count,
    int index) {
    double z = 0.0;
    for (int i = 0; i < index && i < surface_count; ++i) {
        if (d_finite(surfaces[i].thickness) &&
            fabs(surfaces[i].thickness) < 0.5 * kDeviceInf) {
            z += surfaces[i].thickness;
        }
    }
    return z;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_sag(const DeviceSurface& s, double r) {
    if (d_is_plane(s)) {
        return 0.0;
    }
    const double radius = s.radius;
    const double r2 = r * r;
    double z = 0.0;
    if (d_finite(radius) && fabs(radius) >= kDeviceEps &&
        fabs(radius) <= 0.5 * kDeviceInf) {
        const double under =
            1.0 - (1.0 + s.conic) * r2 / (radius * radius);
        if (under < 0.0) {
            return NAN;
        }
        const double denom = radius * (1.0 + sqrt(fmax(0.0, under)));
        if (fabs(denom) < kDeviceEps) {
            return NAN;
        }
        z = r2 / denom;
    }
    if (s.kind == kKindEvenAsphere) {
        double power = r2;
        for (int i = 0;
             i < s.coeff_count && i < kMaxDeviceAsphereCoefficients;
             ++i) {
            z += s.coeffs[i] * power;
            power *= r2;
        }
    }
    return z;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_clear_aperture_radius(const DeviceSurface& s) {
    return fmax(s.semi_aperture, s.aperture_radius);
}
} // namespace
} // namespace optiland
