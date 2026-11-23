#pragma once
#include "../prelude.cuh"
#include "../aiming/seed_candidates.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../contracts/ray_record.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/material_and_sag.cuh"
#include "../geometry/pupil_paraxial.cuh"
#include "asphere_intersection.cuh"
#include "intersection_gate.cuh"
#include "paraxial_generation.cuh"
#include "refraction.cuh"

namespace optiland {
namespace {
__device__ DeviceRay d_generate_surface_point_ray(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceField& field,
    double wavelength_um,
    int first,
    double x,
    double y,
    double nominal_weight) {
    DeviceRay ray;
    ray.wavelength_um = wavelength_um;
    ray.nominal_weight = fmax(0.0, nominal_weight);
    ray.intensity = ray.nominal_weight;
    if (first < 0 || first >= cfg.surface_count) {
        d_mark_ray_failure(&ray, 8, -1, 1.0);
        return ray;
    }
    const DeviceSurface& first_surface = surfaces[first];
    const double sag = d_sag(first_surface, hypot(x, y));
    if (!d_finite(sag)) {
        d_mark_ray_failure(&ray, 7, first, 1.0);
        return ray;
    }
    double clip_radius = first_surface.aperture_radius;
    if (clip_radius <= 0.0 && first_surface.clear_aperture_fixed) {
        clip_radius = first_surface.semi_aperture;
    }
    if (!d_defer_secondary_apertures(cfg, wavelength_um) &&
        clip_radius > 0.0 && hypot(x, y) > 1.01 * clip_radius) {
        d_mark_ray_failure(&ray, 3, first, 1.0);
        return ray;
    }
    d_field_direction(cfg, field, &ray.l, &ray.m, &ray.n);
    const double target_z =
        d_surface_vertex_z(surfaces, cfg.surface_count, first) + sag;
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
        ray.x = x - ray.l * distance_to_target;
        ray.y = y - ray.m * distance_to_target;
        ray.z = target_z - ray.n * distance_to_target;
    } else {
        const double aperture_radius =
            fmax(d_entrance_pupil_radius_from_cfg(surfaces, cfg, wavelength_um),
                 hypot(x, y));
        const double start_distance =
            d_infinite_object_start_distance(
                surfaces,
                cfg,
                aperture_radius,
                target_z);
        ray.x = x - ray.l * start_distance;
        ray.y = y - ray.m * start_distance;
        ray.z = target_z - ray.n * start_distance;
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
__device__ DeviceRay d_generate_first_surface_point_ray(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceField& field,
    double wavelength_um,
    double x,
    double y,
    double nominal_weight) {
    const int first = d_first_ray_aiming_surface(surfaces, cfg, wavelength_um);
    const double object_distance =
        (cfg.surface_count > 0 && surfaces[0].kind == kKindObject)
            ? surfaces[0].thickness
            : kDeviceInf;

    // The ordinary helper first builds a long-distance infinite-object ray,
    // then this function recomputes the same sag and overwrites that origin
    // with the semantic exact-S1 backstep.  UC4 removes only that dead first
    // construction for the isolated even-asphere initial-model case.  The
    // returned fields below are the existing overwrite branch in the same
    // operation order; root solving and subsequent propagation stay UC3.
    if ((cfg.asphere_initial_root_mode >= 10 &&
         cfg.asphere_initial_root_mode <= 31) &&
        !d_effectively_finite_distance(object_distance) &&
        first == cfg.first_optical_surface &&
        first >= 0 && first < cfg.surface_count &&
        surfaces[first].kind == kKindEvenAsphere) {
        DeviceRay ray;
        ray.wavelength_um = wavelength_um;
        ray.nominal_weight = fmax(0.0, nominal_weight);
        ray.intensity = ray.nominal_weight;
        const DeviceSurface& first_surface = surfaces[first];
        const double sag = d_sag(first_surface, hypot(x, y));
        if (!d_finite(sag)) {
            d_mark_ray_failure(&ray, 7, first, 1.0);
            return ray;
        }
        double clip_radius = first_surface.aperture_radius;
        if (clip_radius <= 0.0 && first_surface.clear_aperture_fixed) {
            clip_radius = first_surface.semi_aperture;
        }
        if (!d_defer_secondary_apertures(cfg, wavelength_um) &&
            clip_radius > 0.0 && hypot(x, y) > 1.01 * clip_radius) {
            d_mark_ray_failure(&ray, 3, first, 1.0);
            return ray;
        }
        d_field_direction(cfg, field, &ray.l, &ray.m, &ray.n);
        const double scale = fmax(
            1.0,
            fmax(fabs(first_surface.aperture_radius),
                 fabs(first_surface.semi_aperture)));
        const double backstep = 1.0e-4 * scale;
        const double target_z =
            d_surface_vertex_z(surfaces, cfg.surface_count, first) + sag;
        ray.x = x - ray.l * backstep;
        ray.y = y - ray.m * backstep;
        ray.z = target_z - ray.n * backstep;
        if (!d_finite(ray.x) || !d_finite(ray.y) || !d_finite(ray.z)) {
            d_mark_ray_failure(&ray, 8, -1, 1.0);
        }
        return ray;
    }

    DeviceRay ray = d_generate_surface_point_ray(
        surfaces,
        cfg,
        field,
        wavelength_um,
        first,
        x,
        y,
        nominal_weight);
    if (ray.valid &&
        !d_effectively_finite_distance(object_distance) &&
        first == cfg.first_optical_surface &&
        first >= 0 && first < cfg.surface_count) {
        const DeviceSurface& first_surface = surfaces[first];
        const double sag = d_sag(first_surface, hypot(x, y));
        if (d_finite(sag)) {
            const double scale = fmax(
                1.0,
                fmax(fabs(first_surface.aperture_radius),
                     fabs(first_surface.semi_aperture)));
            const double backstep = 1.0e-4 * scale;
            const double target_z =
                d_surface_vertex_z(surfaces, cfg.surface_count, first) + sag;
            ray.x = x - ray.l * backstep;
            ray.y = y - ray.m * backstep;
            ray.z = target_z - ray.n * backstep;
        }
    }
    return ray;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_trace_ray_to_surface(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    DeviceRay ray,
    int surface_index,
    double* hit_x,
    double* hit_y,
    bool ignore_target_aperture = false,
    bool ignore_intermediate_apertures = false,
    int deep_profile_role = kAsphereDeepRoleAuxiliary,
    int deep_profile_site = kAsphereDeepSiteOther) {
    if (surface_index < 0 || surface_index >= cfg.surface_count) {
        return false;
    }
    if (surface_index < cfg.first_optical_surface ||
        surfaces[surface_index].kind == kKindObject) {
        if (hit_x) {
            *hit_x = ray.x;
        }
        if (hit_y) {
            *hit_y = ray.y;
        }
        return ray.valid != 0;
    }
    double vertex_z = 0.0;
    double n_before = 1.0;
    const int max_surface = min(surface_index + 1, cfg.surface_count);
    for (int i = 0; i < max_surface; ++i) {
        const DeviceSurface& surface = surfaces[i];
        if (i < cfg.first_optical_surface || surface.kind == kKindObject) {
            continue;
        }
        const double local_z = ray.z - vertex_z;
        double t = 0.0;
        if (!ray.valid) {
            ray.intensity = 0.0;
        } else {
            const bool ignore_stop_domain =
                d_defer_secondary_apertures(cfg, ray.wavelength_um) ||
                ignore_intermediate_apertures ||
                (ignore_target_aperture && i == surface_index);
            const bool intersection_ok =
                surface.kind == kKindEvenAsphere
                    ? d_intersect_even_asphere_configured(
                          surface,
                          cfg,
                          ray.x,
                          ray.y,
                          local_z,
                          ray.l,
                          ray.m,
                          ray.n,
                          false,
                          cfg.asphere_aiming_local_branch != 0,
                          ignore_stop_domain,
                          nullptr,
                          &t,
                          i,
                          deep_profile_role,
                          deep_profile_site)
                    : d_intersect_surface(
                          surface,
                          ray.x,
                          ray.y,
                          local_z,
                          ray.l,
                          ray.m,
                          ray.n,
                          &t);
            if (!intersection_ok) {
                d_mark_ray_failure(&ray, 2, i, 1.0);
            } else {
            ray.x += ray.l * t;
            ray.y += ray.m * t;
            ray.z += ray.n * t;
            const bool skip_target_clip =
                ignore_target_aperture && i == surface_index;
            const bool skip_clip =
                d_defer_secondary_apertures(cfg, ray.wavelength_um) ||
                ignore_intermediate_apertures || skip_target_clip;
            if (!skip_clip) {
                const double clip_radius =
                    d_effective_aperture_clip_radius(
                        surfaces,
                        cfg,
                        i,
                        ray.wavelength_um);
                if (clip_radius > 0.0) {
                    const double radial = hypot(ray.x, ray.y);
                    const double limit = 1.01 * clip_radius;
                    if (radial > limit) {
                        d_mark_ray_failure(
                            &ray,
                            3,
                            i,
                            (radial - clip_radius) /
                                fmax(clip_radius, kDeviceEps));
                    }
                }
            }
            if (ray.valid && surface.kind != kKindImage) {
                double nx = 0.0;
                double ny = 0.0;
                double nz = -1.0;
                if (!d_surface_normal(surface, ray.x, ray.y, &nx, &ny, &nz)) {
                    d_mark_ray_failure(&ray, 7, i, 1.0);
                } else {
                    const double n_post =
                        d_surface_refractive_index(surface, cfg, ray.wavelength_um);
                    double ox = ray.l;
                    double oy = ray.m;
                    double oz = ray.n;
                    if (!d_refract(
                            ray.l,
                            ray.m,
                            ray.n,
                            nx,
                            ny,
                            nz,
                            n_before,
                            n_post,
                            &ox,
                            &oy,
                            &oz)) {
                        d_mark_ray_failure(&ray, 4, i, 1.0);
                    } else {
                        ray.l = ox;
                        ray.m = oy;
                        ray.n = oz;
                    }
                }
            }
            }
        }
        if (i == surface_index) {
            if (hit_x) {
                *hit_x = ray.x;
            }
            if (hit_y) {
                *hit_y = ray.y;
            }
            return ray.valid != 0 && d_finite(ray.x) && d_finite(ray.y);
        }
        n_before = d_surface_refractive_index(surface, cfg, ray.wavelength_um);
        if (d_finite(surface.thickness) &&
            fabs(surface.thickness) < 0.5 * kDeviceInf) {
            vertex_z += surface.thickness;
        }
    }
    return false;
}
} // namespace
} // namespace optiland
