#pragma once
#include "../prelude.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "asphere_slope.cuh"
#include "first_order_metrics.cuh"
#include "material_and_sag.cuh"

namespace optiland {
namespace {
__device__ void d_paraxial_refract_forward(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    int surface_index,
    double wavelength_um,
    double y,
    double* u) {
    if (!u || surface_index < 0 || surface_index >= cfg.surface_count) {
        return;
    }
    const DeviceSurface& surface = surfaces[surface_index];
    if (surface.kind == kKindObject || surface.kind == kKindImage) {
        return;
    }
    const double n1 = surface_index > 0
                          ? d_surface_refractive_index(
                                surfaces[surface_index - 1], cfg, wavelength_um)
                          : 1.0;
    const double n2 = d_surface_refractive_index(surface, cfg, wavelength_um);
    if (fabs(n2) <= kDeviceEps) {
        return;
    }
    const double power =
        (d_finite(surface.radius) && fabs(surface.radius) >= kDeviceEps &&
         fabs(surface.radius) < 0.5 * kDeviceInf)
            ? (n2 - n1) / surface.radius
            : 0.0;
    *u = (n1 * (*u) - y * power) / n2;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ void d_paraxial_refract_reverse(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    int surface_index,
    double wavelength_um,
    double y,
    double* u) {
    if (!u || surface_index < 0 || surface_index >= cfg.surface_count) {
        return;
    }
    const DeviceSurface& surface = surfaces[surface_index];
    if (surface.kind == kKindObject || surface.kind == kKindImage) {
        return;
    }
    const double n1 = d_surface_refractive_index(surface, cfg, wavelength_um);
    const double n2 = surface_index > 0
                          ? d_surface_refractive_index(
                                surfaces[surface_index - 1], cfg, wavelength_um)
                          : 1.0;
    if (fabs(n2) <= kDeviceEps) {
        return;
    }
    const double power =
        (d_finite(surface.radius) && fabs(surface.radius) >= kDeviceEps &&
         fabs(surface.radius) < 0.5 * kDeviceInf)
            ? (n2 - n1) / (-surface.radius)
            : 0.0;
    *u = (n1 * (*u) - y * power) / n2;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_paraxial_trace_to_surface(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    double y0,
    double u0,
    double z0,
    double wavelength_um,
    int surface_index,
    double* y_at_surface) {
    if (surface_index < cfg.first_optical_surface ||
        surface_index >= cfg.surface_count) {
        return false;
    }
    double y = y0;
    double u = u0;
    double z = z0;
    for (int i = cfg.first_optical_surface; i <= surface_index; ++i) {
        const double vertex_z = d_surface_vertex_z(surfaces, cfg.surface_count, i);
        y += (vertex_z - z) * u;
        z = vertex_z;
        d_paraxial_refract_forward(surfaces, cfg, i, wavelength_um, y, &u);
    }
    if (y_at_surface) {
        *y_at_surface = y;
    }
    return d_finite(y) && d_finite(u);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_paraxial_trace_to_surface_from(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    double y0,
    double u0,
    double z0,
    double wavelength_um,
    int surface_index,
    int start_surface,
    double* y_at_surface) {
    if (surface_index < start_surface ||
        start_surface < cfg.first_optical_surface ||
        surface_index >= cfg.surface_count) {
        return false;
    }
    double y = y0;
    double u = u0;
    double z = z0;
    for (int i = start_surface; i <= surface_index; ++i) {
        const double vertex_z =
            d_surface_vertex_z(surfaces, cfg.surface_count, i);
        y += (vertex_z - z) * u;
        z = vertex_z;
        d_paraxial_refract_forward(surfaces, cfg, i, wavelength_um, y, &u);
    }
    if (y_at_surface) {
        *y_at_surface = y;
    }
    return d_finite(y) && d_finite(u);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_entrance_pupil_radius_from_cfg(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    double wavelength_um) {
    (void)surfaces;
    (void)wavelength_um;
    // ApertureType::EntrancePupilDiameter is the 9P/global-explore path.
    if (cfg.aperture_type == 0 && cfg.aperture_value > 0.0) {
        return 0.5 * cfg.aperture_value;
    }
    return cfg.entrance_pupil_radius > 0.0 ? cfg.entrance_pupil_radius : 1.0;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_entrance_pupil_location(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    double wavelength_um) {
    const int stop = d_stop_index(surfaces, cfg);
    const int first = cfg.first_optical_surface;
    if (stop <= first || stop >= cfg.surface_count) {
        return 0.0;
    }
    const double image_z =
        d_surface_vertex_z(surfaces, cfg.surface_count, cfg.surface_count - 1);
    double y = 0.0;
    double u = 0.1;
    double z = image_z - d_surface_vertex_z(surfaces, cfg.surface_count, stop);
    for (int idx = stop; idx > first; --idx) {
        const int i = idx - 1;
        const double vertex_z =
            image_z - d_surface_vertex_z(surfaces, cfg.surface_count, i);
        y += (vertex_z - z) * u;
        z = vertex_z;
        d_paraxial_refract_reverse(surfaces, cfg, i, wavelength_um, y, &u);
    }
    if (fabs(u) <= kDeviceEps || !d_finite(y) || !d_finite(u)) {
        return 0.0;
    }
    return y / u;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_paraxial_stop_radius(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    double wavelength_um) {
    const int stop = d_stop_index(surfaces, cfg);
    if (stop < 0 || stop >= cfg.surface_count) {
        return d_entrance_pupil_radius_from_cfg(surfaces, cfg, wavelength_um);
    }
    const double ep_radius =
        d_entrance_pupil_radius_from_cfg(surfaces, cfg, wavelength_um);
    double y_stop = 0.0;
    if (d_paraxial_trace_to_surface(
            surfaces,
            cfg,
            ep_radius,
            0.0,
            -10.0,
            wavelength_um,
            stop,
            &y_stop)) {
        return fmax(kDeviceEps, fabs(y_stop));
    }
    return ep_radius;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_aperture_stop_radius(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    double wavelength_um) {
    const int stop = d_stop_index(surfaces, cfg);
    if (cfg.aperture_type == 3 && stop >= 0 && stop < cfg.surface_count) {
        const DeviceSurface& stop_surface = surfaces[stop];
        if (stop_surface.aperture_radius > 0.0) {
            return stop_surface.aperture_radius;
        }
        if (stop_surface.semi_aperture > 0.0) {
            return stop_surface.semi_aperture;
        }
    }
    // Direct/paraxial launch modes have no prescribed aiming target at the
    // stop.  Their physical radius is the actual-ray envelope stored by the
    // aperture trace.  Paraxial/real model radii are reserved for true aiming
    // solvers (calibrated, iterative, robust, ...).
    if ((cfg.aiming_mode == 0 || cfg.aiming_mode == 1) &&
        stop >= 0 && stop < cfg.surface_count) {
        const DeviceSurface& stop_surface = surfaces[stop];
        if (stop_surface.aperture_radius > 0.0) {
            return stop_surface.aperture_radius;
        }
        if (stop_surface.semi_aperture > 0.0) {
            return stop_surface.semi_aperture;
        }
        return 0.0;
    }
    const double stop_wavelength =
        d_finite(cfg.primary_wavelength) && cfg.primary_wavelength > 0.0
            ? cfg.primary_wavelength
            : wavelength_um;
    if (cfg.aiming_stop_radius_source == 1 &&
        stop >= 0 && stop < cfg.surface_count) {
        // Candidate preparation traces the EPD edge at the primary wavelength
        // and stores the resulting real-stop radius on the stop surface.  Use
        // that stable candidate-level value for all later wavelengths/rays.
        const DeviceSurface& stop_surface = surfaces[stop];
        if (stop_surface.aperture_radius > 0.0) {
            return stop_surface.aperture_radius;
        }
        if (stop_surface.semi_aperture > 0.0) {
            return stop_surface.semi_aperture;
        }
    }
    return d_paraxial_stop_radius(surfaces, cfg, stop_wavelength);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ void d_sync_stop_aperture_to_aiming_radius(
    DeviceSurface* surfaces,
    const DeviceConfig& cfg) {
    const int stop = d_stop_index(surfaces, cfg);
    if (stop < 0 || stop >= cfg.surface_count ||
        surfaces[stop].clear_aperture_fixed) {
        return;
    }
    if (cfg.aperture_type != 3 &&
        (cfg.aiming_mode == 0 || cfg.aiming_mode == 1)) {
        return;
    }
    const double radius =
        d_aperture_stop_radius(surfaces, cfg, cfg.primary_wavelength);
    if (!d_finite(radius) || radius <= 0.0) {
        return;
    }
    surfaces[stop].aperture_radius = radius;
    surfaces[stop].semi_aperture = radius;
    if (!surfaces[stop].mechanical_aperture_fixed) {
        surfaces[stop].mechanical_aperture = radius;
    }
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ void d_update_mechanical_apertures_from_groups(
    DeviceSurface* surfaces,
    const DeviceConfig& cfg) {
    int i = cfg.first_optical_surface;
    while (i < cfg.surface_count) {
        if (surfaces[i].kind == kKindObject ||
            !d_interval_is_glass(surfaces[i])) {
            ++i;
            continue;
        }
        const int first = i;
        while (i < cfg.surface_count && d_interval_is_glass(surfaces[i])) {
            ++i;
        }
        const int last = min(i, cfg.surface_count - 1);
        double element_radius = 0.0;
        for (int si = first; si <= last; ++si) {
            element_radius = fmax(
                element_radius,
                d_clear_aperture_radius(surfaces[si]));
        }
        if (element_radius > 0.0) {
            for (int si = first; si <= last; ++si) {
                if (!surfaces[si].mechanical_aperture_fixed) {
                    surfaces[si].mechanical_aperture = element_radius;
                }
            }
        }
    }

    for (int i = cfg.first_optical_surface; i < cfg.surface_count; ++i) {
        if (surfaces[i].kind == kKindObject ||
            surfaces[i].mechanical_aperture_fixed ||
            d_interval_is_glass(surfaces[i]) ||
            (i > 0 && d_interval_is_glass(surfaces[i - 1]))) {
            continue;
        }
        const double clear = d_clear_aperture_radius(surfaces[i]);
        if (clear > 0.0) {
            surfaces[i].mechanical_aperture = clear;
        }
    }
}
} // namespace
} // namespace optiland
