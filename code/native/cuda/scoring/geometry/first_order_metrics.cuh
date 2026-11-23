#pragma once
#include "../prelude.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "asphere_slope.cuh"
#include "material_and_sag.cuh"

namespace optiland {
namespace {
__device__ double d_efl(const DeviceSurface* surfaces, const DeviceConfig& cfg) {
    double y = 1.0;
    double u = 0.0;
    double z_current = -1.0;
    double vertex_z = 0.0;
    double n_before = 1.0;
    for (int i = cfg.first_optical_surface; i < cfg.surface_count; ++i) {
        const DeviceSurface& surface = surfaces[i];
        y += u * (vertex_z - z_current);
        z_current = vertex_z;
        if (surface.kind != kKindObject && surface.kind != kKindImage) {
            const double n1 = n_before;
            const double n2 =
                d_surface_refractive_index(surface, cfg, cfg.primary_wavelength);
            if (fabs(n2) > kDeviceEps) {
                const double power =
                    (d_finite(surface.radius) && fabs(surface.radius) >= kDeviceEps &&
                     fabs(surface.radius) < 0.5 * kDeviceInf)
                        ? (n2 - n1) / surface.radius
                        : 0.0;
                u = (n1 * u - y * power) / n2;
            }
        }
        n_before = d_surface_refractive_index(surface, cfg, cfg.primary_wavelength);
        if (d_finite(surface.thickness) &&
            fabs(surface.thickness) < 0.5 * kDeviceInf) {
            vertex_z += surface.thickness;
        }
    }
    if (fabs(u) <= kDeviceEps) {
        return NAN;
    }
    return -1.0 / u;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_paraxial_focus_position(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    double wavelength_um) {
    double y = 1.0;
    double u = 0.0;
    double z_current = -1.0;
    double vertex_z = 0.0;
    double n_before = 1.0;
    double focus_z = NAN;
    for (int i = cfg.first_optical_surface; i < cfg.surface_count; ++i) {
        const DeviceSurface& surface = surfaces[i];
        y += u * (vertex_z - z_current);
        z_current = vertex_z;
        if (surface.kind != kKindObject && surface.kind != kKindImage) {
            const double n1 = n_before;
            const double n2 = d_surface_refractive_index(surface, cfg, wavelength_um);
            if (fabs(n2) > kDeviceEps) {
                const double power =
                    (d_finite(surface.radius) && fabs(surface.radius) >= kDeviceEps &&
                     fabs(surface.radius) < 0.5 * kDeviceInf)
                        ? (n2 - n1) / surface.radius
                        : 0.0;
                u = (n1 * u - y * power) / n2;
                if (fabs(u) > kDeviceEps) {
                    focus_z = z_current - y / u;
                }
            }
        }
        n_before = d_surface_refractive_index(surface, cfg, wavelength_um);
        if (d_finite(surface.thickness) &&
            fabs(surface.thickness) < 0.5 * kDeviceInf) {
            vertex_z += surface.thickness;
        }
    }
    return focus_z;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_paraxial_axial_color_focus_shift(
    const DeviceSurface* surfaces,
    const DeviceWave* waves,
    const DeviceConfig& cfg) {
    double min_focus = INFINITY;
    double max_focus = -INFINITY;
    int active_count = 0;
    int valid_count = 0;
    for (int wi = 0; wi < cfg.wavelength_count; ++wi) {
        if (waves[wi].weight <= 0.0) {
            continue;
        }
        ++active_count;
        const double focus =
            d_paraxial_focus_position(surfaces, cfg, waves[wi].value_um);
        if (!d_finite(focus)) {
            continue;
        }
        min_focus = fmin(min_focus, focus);
        max_focus = fmax(max_focus, focus);
        ++valid_count;
    }
    return active_count > 0 && valid_count == active_count
               ? fmax(0.0, max_focus - min_focus)
               : NAN;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ int d_stop_index(const DeviceSurface* surfaces, const DeviceConfig& cfg) {
    for (int i = cfg.first_optical_surface; i < cfg.surface_count; ++i) {
        if (surfaces[i].is_stop) {
            return i;
        }
    }
    return cfg.first_optical_surface;
}
} // namespace
} // namespace optiland
