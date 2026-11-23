#pragma once
#include "../prelude.cuh"
#include "seed_candidates.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../contracts/ray_record.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/first_order_metrics.cuh"
#include "../geometry/material_and_sag.cuh"
#include "../geometry/pupil_paraxial.cuh"
#include "../ray/paraxial_generation.cuh"
#include "../ray/stop_trace.cuh"

namespace optiland {
namespace {
__device__ double d_real_stop_radius(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    double wavelength_um) {
    const int stop = d_stop_index(surfaces, cfg);
    if (stop < 0 || stop >= cfg.surface_count) {
        return d_paraxial_stop_radius(surfaces, cfg, wavelength_um);
    }

    DeviceRay ray;
    const double epd_radius =
        d_entrance_pupil_radius_from_cfg(surfaces, cfg, wavelength_um);
    const double object_distance =
        cfg.surface_count > 0 && surfaces[0].kind == kKindObject
            ? surfaces[0].thickness
            : kDeviceInf;
    if (d_effectively_finite_distance(object_distance)) {
        const double epl =
            d_entrance_pupil_location(surfaces, cfg, wavelength_um);
        ray.x = 0.0;
        ray.y = 0.0;
        ray.z = -object_distance;
        ray.l = 0.0;
        ray.m = epd_radius;
        ray.n = epl + object_distance;
        d_normalize3(&ray.l, &ray.m, &ray.n);
    } else {
        ray.x = 0.0;
        ray.y = epd_radius;
        ray.z = -100.0;
        ray.l = 0.0;
        ray.m = 0.0;
        ray.n = 1.0;
    }
    ray.wavelength_um = wavelength_um;
    ray.nominal_weight = 1.0;
    ray.intensity = 1.0;
    ray.valid = 1;

    double stop_x = 0.0;
    double stop_y = 0.0;
    if (d_trace_ray_to_surface(
            surfaces,
            cfg,
            ray,
            stop,
            &stop_x,
            &stop_y,
            true) &&
        d_finite(stop_x) && d_finite(stop_y)) {
        return fmax(kDeviceEps, hypot(stop_x, stop_y));
    }
    return d_paraxial_stop_radius(surfaces, cfg, wavelength_um);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ void d_initialize_real_stop_aperture(
    DeviceSurface* surfaces,
    const DeviceConfig& cfg) {
    if (!surfaces || cfg.aiming_stop_radius_source != 1 ||
        cfg.aperture_type == 3 ||
        cfg.aiming_mode == 0 || cfg.aiming_mode == 1) {
        return;
    }
    const int stop = d_stop_index(surfaces, cfg);
    if (stop < 0 || stop >= cfg.surface_count ||
        surfaces[stop].clear_aperture_fixed) {
        return;
    }
    const double stop_wavelength =
        d_finite(cfg.primary_wavelength) && cfg.primary_wavelength > 0.0
            ? cfg.primary_wavelength
            : 0.5875618;
    const double radius =
        d_real_stop_radius(surfaces, cfg, stop_wavelength);
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
__device__ DeviceSeed d_seed_from_ray(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    DeviceRay ray,
    int deep_profile_role = kAsphereDeepRoleAuxiliary,
    int deep_profile_site = kAsphereDeepSiteOther) {
    DeviceSeed seed;
    const int first =
        d_first_ray_aiming_surface(surfaces, cfg, ray.wavelength_um);
    double x = 0.0;
    double y = 0.0;
    if (d_trace_ray_to_surface(
            surfaces,
            cfg,
            ray,
            first,
            &x,
            &y,
            false,
            false,
            deep_profile_role,
            deep_profile_site)) {
        seed.x = x;
        seed.y = y;
        seed.valid = d_finite(x) && d_finite(y) ? 1 : 0;
    }
    return seed;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_sample_stop_from_seed(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceField& field,
    double wavelength_um,
    const DeviceSeed& seed,
    double* stop_x,
    double* stop_y,
    DeviceRay* traced_ray = nullptr,
    bool ignore_target_aperture = false,
    bool ignore_intermediate_apertures = false,
    int deep_profile_role = kAsphereDeepRoleAuxiliary) {
    if (!d_seed_in_first_surface_bounds(surfaces, cfg, wavelength_um, seed)) {
        return false;
    }
    DeviceRay ray = d_generate_first_surface_point_ray(
        surfaces,
        cfg,
        field,
        wavelength_um,
        seed.x,
        seed.y,
        1.0);
    if (traced_ray) {
        *traced_ray = ray;
    }
    const int stop = d_stop_index(surfaces, cfg);
    return d_trace_ray_to_surface(
        surfaces,
        cfg,
        ray,
        stop,
        stop_x,
        stop_y,
        ignore_target_aperture,
        ignore_intermediate_apertures,
        deep_profile_role,
        kAsphereDeepSiteExactFirstSurfaceSeed);
}
} // namespace
} // namespace optiland
