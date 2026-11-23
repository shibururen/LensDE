#pragma once
#include "../prelude.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../contracts/ray_record.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/first_order_metrics.cuh"
#include "../geometry/material_and_sag.cuh"
#include "../geometry/mechanical_constraints.cuh"
#include "../geometry/pupil_paraxial.cuh"

namespace optiland {
namespace {
__device__ void d_normalize3(double* x, double* y, double* z) {
    const double norm = sqrt((*x) * (*x) + (*y) * (*y) + (*z) * (*z));
    if (norm > 0.0 && d_finite(norm)) {
        *x /= norm;
        *y /= norm;
        *z /= norm;
    }
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ void d_field_direction(
    const DeviceConfig& cfg,
    const DeviceField& field,
    double* lx,
    double* my,
    double* nz) {
    const double field_radius = hypot(field.hx, field.hy);
    if (field_radius <= kDeviceEps || fabs(cfg.max_field_deg) <= kDeviceEps) {
        *lx = 0.0;
        *my = 0.0;
        *nz = 1.0;
        return;
    }
    const double theta = (3.14159265358979323846 / 180.0) *
                         cfg.max_field_deg * field_radius;
    const double transverse = sin(theta);
    *lx = transverse * field.hx / field_radius;
    *my = transverse * field.hy / field_radius;
    *nz = cos(theta);
    d_normalize3(lx, my, nz);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_infinite_object_start_distance(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    double aperture_radius,
    double target_z) {
    return fmax(
        10.0,
        fabs(d_total_track(surfaces, cfg)) + fabs(target_z) +
            4.0 * fabs(aperture_radius) + 10.0);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ void d_mark_ray_failure(
    DeviceRay* ray,
    int code,
    int surface,
    double severity,
    bool invalidate = true) {
    if (!ray) {
        return;
    }
    if (code != 1 || ray->failure_code == 0) {
        ray->failure_code = code;
        ray->failure_surface = surface;
        ray->failure_severity =
            d_finite(severity) ? fmax(0.0, severity) : 1.0;
    }
    if (invalidate) {
        ray->valid = 0;
        ray->intensity = 0.0;
    }
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ void d_mark_backward_warning(
    DeviceRay* ray,
    int surface,
    double severity) {
    if (!ray) {
        return;
    }
    if (ray->backward_surface < 0 || surface < ray->backward_surface) {
        ray->backward_surface = surface;
    }
    ray->backward_propagation = 1;
    ray->backward_severity =
        d_finite(severity) ? fmax(ray->backward_severity, severity) : 1.0;
    if (ray->failure_code == 0) {
        ray->failure_code = 6;
        ray->failure_surface = surface;
        ray->failure_severity = ray->backward_severity;
    }
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ DeviceRay d_generate_surface_point_ray(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceField& field,
    double wavelength_um,
    int surface_index,
    double x,
    double y,
    double nominal_weight);
} // namespace
} // namespace optiland

namespace optiland {
namespace {
template <bool kApplyVignetting>
__device__ DeviceRay d_generate_paraxial_ray_impl(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceField& field,
    double pupil_x,
    double pupil_y,
    double pupil_weight,
    double wavelength_um) {
    DeviceRay ray;
    d_field_direction(cfg, field, &ray.l, &ray.m, &ray.n);
    ray.nominal_weight = fmax(0.0, pupil_weight);
    ray.intensity = ray.nominal_weight;
    ray.wavelength_um = wavelength_um;
    const double pxf = kApplyVignetting
                           ? field.fvcx + pupil_x * (1.0 - field.fvdx)
                           : pupil_x;
    const double pyf = kApplyVignetting
                           ? field.fvcy + pupil_y * (1.0 - field.fvdy)
                           : pupil_y;
    const double ep_radius =
        d_entrance_pupil_radius_from_cfg(surfaces, cfg, wavelength_um);

    // A physical S1 stop is the entrance pupil itself.  Target the requested
    // local point on its sag surface so an off-axis field cannot translate the
    // bundle across S1 merely because a vertex-plane pupil was used.
    const int first = cfg.first_optical_surface;
    if (first >= 0 && first < cfg.surface_count && surfaces[first].is_stop) {
        const DeviceSurface& stop_surface = surfaces[first];
        double target_radius = ep_radius;
        if (stop_surface.clear_aperture_fixed || cfg.aperture_type == 3) {
            if (stop_surface.aperture_radius > 0.0) {
                target_radius = stop_surface.aperture_radius;
            } else if (stop_surface.semi_aperture > 0.0) {
                target_radius = stop_surface.semi_aperture;
            }
        }
        if (!d_finite(target_radius) || target_radius <= kDeviceEps) {
            target_radius = ep_radius;
        }
        DeviceRay ray = d_generate_surface_point_ray(
            surfaces,
            cfg,
            field,
            wavelength_um,
            first,
            pxf * target_radius,
            pyf * target_radius,
            pupil_weight);
        if (pxf * pxf + pyf * pyf > 1.0 + 1.0e-10) {
            d_mark_ray_failure(
                &ray,
                3,
                first,
                fmax(0.0, sqrt(pxf * pxf + pyf * pyf) - 1.0));
        }
        return ray;
    }

    const double pupil_wavelength =
        d_finite(cfg.primary_wavelength) && cfg.primary_wavelength > 0.0
            ? cfg.primary_wavelength
            : wavelength_um;
    const double epl =
        d_entrance_pupil_location(surfaces, cfg, pupil_wavelength);
    const double target_x = pxf * ep_radius;
    const double target_y = pyf * ep_radius;
    const double target_z = epl;
    const double object_distance =
        (cfg.surface_count > 0 && surfaces[0].kind == kKindObject)
            ? surfaces[0].thickness
            : kDeviceInf;
    if (d_effectively_finite_distance(object_distance) &&
        fabs(ray.n) > kDeviceEps) {
        const double distance_to_target = (target_z + object_distance) / ray.n;
        if (!d_finite(distance_to_target) || distance_to_target <= kDeviceEps) {
            d_mark_ray_failure(&ray, 8, -1, 1.0);
            return ray;
        }
        ray.x = target_x - ray.l * distance_to_target;
        ray.y = target_y - ray.m * distance_to_target;
        ray.z = target_z - ray.n * distance_to_target;
    } else {
        const double start_distance =
            d_infinite_object_start_distance(surfaces, cfg, ep_radius, target_z);
        ray.x = target_x - ray.l * start_distance;
        ray.y = target_y - ray.m * start_distance;
        ray.z = target_z - ray.n * start_distance;
    }
    if (pxf * pxf + pyf * pyf > 1.0 + 1.0e-10) {
        d_mark_ray_failure(
            &ray,
            3,
            -1,
            fmax(0.0, sqrt(pxf * pxf + pyf * pyf) - 1.0));
    }
    if (!d_finite(ray.x) || !d_finite(ray.y) || !d_finite(ray.z)) {
        d_mark_ray_failure(&ray, 8, -1, 1.0);
    }
    return ray;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ DeviceRay d_generate_paraxial_ray(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceField& field,
    double pupil_x,
    double pupil_y,
    double pupil_weight,
    double wavelength_um) {
    return d_generate_paraxial_ray_impl<true>(
        surfaces,
        cfg,
        field,
        pupil_x,
        pupil_y,
        pupil_weight,
        wavelength_um);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ DeviceRay d_generate_full_pupil_paraxial_ray(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceField& field,
    double pupil_x,
    double pupil_y,
    double pupil_weight,
    double wavelength_um) {
    return d_generate_paraxial_ray_impl<false>(
        surfaces,
        cfg,
        field,
        pupil_x,
        pupil_y,
        pupil_weight,
        wavelength_um);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_effective_aperture_clip_radius(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    int surface_index,
    double wavelength_um) {
    if (surface_index < 0 || surface_index >= cfg.surface_count) {
        return 0.0;
    }
    const DeviceSurface& surface = surfaces[surface_index];
    if (surface.is_stop && !surface.clear_aperture_fixed &&
        cfg.aperture_type != 3) {
        const double radius = d_aperture_stop_radius(surfaces, cfg, wavelength_um);
        if (d_finite(radius) && radius > 0.0) {
            return radius;
        }
    }
    double clip_radius = surface.aperture_radius;
    if (clip_radius <= 0.0 && surface.clear_aperture_fixed) {
        clip_radius = surface.semi_aperture;
    }
    return clip_radius;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_guard_ray_reached_inside_physical_stop(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    double wavelength_um,
    int stop_reached,
    double stop_x,
    double stop_y) {
    if (!surfaces || !stop_reached || !d_finite(stop_x) || !d_finite(stop_y)) {
        return false;
    }
    const int stop = d_stop_index(surfaces, cfg);
    if (stop < 0 || stop >= cfg.surface_count || !surfaces[stop].is_stop) {
        return false;
    }
    const double stop_radius =
        d_effective_aperture_clip_radius(surfaces, cfg, stop, wavelength_um);
    if (!d_finite(stop_radius) || stop_radius <= 0.0) {
        return false;
    }
    return hypot(stop_x, stop_y) <= 1.01 * stop_radius;
}
} // namespace
} // namespace optiland
