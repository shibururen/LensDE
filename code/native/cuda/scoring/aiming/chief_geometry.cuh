#pragma once
#include "../prelude.cuh"
#include "seed_candidates.cuh"
#include "stop_samples.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../contracts/ray_record.cuh"
#include "../diagnostics/intersection_shadow.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/first_order_metrics.cuh"
#include "../geometry/material_and_sag.cuh"
#include "../geometry/pupil_paraxial.cuh"
#include "../ray/asphere_intersection.cuh"
#include "../ray/intersection_gate.cuh"
#include "../ray/paraxial_generation.cuh"
#include "../ray/stop_trace.cuh"

namespace optiland {
namespace {
__device__ bool d_first_surface_to_stop_abcd(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    double wavelength_um,
    double* a,
    double* b) {
    if (!a || !b) {
        return false;
    }
    const int stop = d_stop_index(surfaces, cfg);
    const int first = d_first_ray_aiming_surface(surfaces, cfg, wavelength_um);
    if (first < 0 || first >= cfg.surface_count || stop < first) {
        return false;
    }
    const double first_z = d_surface_vertex_z(surfaces, cfg.surface_count, first);
    double y_from_height = 0.0;
    double y_from_slope = 0.0;
    if (!d_paraxial_trace_to_surface_from(
            surfaces,
            cfg,
            1.0,
            0.0,
            first_z,
            wavelength_um,
            stop,
            first,
            &y_from_height) ||
        !d_paraxial_trace_to_surface_from(
            surfaces,
            cfg,
            0.0,
            1.0,
            first_z,
            wavelength_um,
            stop,
            first,
            &y_from_slope)) {
        return false;
    }
    *a = y_from_height;
    *b = y_from_slope;
    return d_finite(*a) && d_finite(*b);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ DeviceRay d_make_failed_aim_ray(
    const DeviceConfig& cfg,
    double wavelength_um,
    double pupil_weight,
    double residual = INFINITY) {
    DeviceRay failed;
    failed.wavelength_um = wavelength_um;
    failed.nominal_weight = fmax(0.0, pupil_weight);
    failed.intensity = failed.nominal_weight;
    failed.aim_converged = 0;
    failed.aim_residual = residual;
    failed.aim_target_radius = cfg.entrance_pupil_radius;
    d_mark_ray_failure(&failed, 1, -1, 1.0);
    return failed;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_physical_stop_radius(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    double fallback_radius) {
    const int stop = d_stop_index(surfaces, cfg);
    double radius = fallback_radius;
    if (stop >= 0 && stop < cfg.surface_count) {
        const DeviceSurface& stop_surface = surfaces[stop];
        if (stop_surface.aperture_radius > 0.0) {
            radius = stop_surface.aperture_radius;
        } else if (stop_surface.semi_aperture > 0.0) {
            radius = stop_surface.semi_aperture;
        }
    }
    return fmax(1.0e-9, fabs(radius));
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __noinline__ double d_center_meridional_radius(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceField& center_field,
    double wavelength_um) {
    DeviceRay plus = d_generate_full_pupil_paraxial_ray(
        surfaces, cfg, center_field, 0.0, 1.0, 1.0, wavelength_um);
    DeviceRay minus = d_generate_full_pupil_paraxial_ray(
        surfaces, cfg, center_field, 0.0, -1.0, 1.0, wavelength_um);
    DeviceSeed plus_seed = d_seed_from_ray(surfaces, cfg, plus);
    DeviceSeed minus_seed = d_seed_from_ray(surfaces, cfg, minus);
    if (!plus_seed.valid || !minus_seed.valid) {
        return 0.0;
    }
    const double radius = 0.5 * hypot(plus_seed.x - minus_seed.x,
                                      plus_seed.y - minus_seed.y);
    return d_finite(radius) ? radius : 0.0;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ DeviceSeed d_scaled_edge_seed(
    const DeviceSeed& chief_seed,
    const DeviceSeed& full_seed,
    double scale) {
    DeviceSeed seed;
    if (!chief_seed.valid || !full_seed.valid ||
        !d_finite(scale) || scale <= 0.0) {
        return seed;
    }
    seed.x = chief_seed.x + scale * (full_seed.x - chief_seed.x);
    seed.y = chief_seed.y + scale * (full_seed.y - chief_seed.y);
    seed.valid = d_finite(seed.x) && d_finite(seed.y) ? 1 : 0;
    return seed;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __noinline__ DeviceSeed d_shifted_chief_seed_by_transverse_radius(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceField& field,
    double wavelength_um,
    const DeviceSeed& chief_seed,
    double edge_px,
    double edge_py,
    double beam_radius) {
    DeviceSeed out;
    if (!chief_seed.valid || !d_finite(chief_seed.x) || !d_finite(chief_seed.y) ||
        !d_finite(beam_radius) || beam_radius <= 0.0) {
        return out;
    }
    const int first = d_first_ray_aiming_surface(surfaces, cfg, wavelength_um);
    if (first < 0 || first >= cfg.surface_count) {
        return out;
    }
    const DeviceSurface& first_surface = surfaces[first];
    const double chief_sag = d_sag(first_surface, hypot(chief_seed.x, chief_seed.y));
    if (!d_finite(chief_sag)) {
        return out;
    }

    DeviceRay chief_ray = d_generate_first_surface_point_ray(
        surfaces,
        cfg,
        field,
        wavelength_um,
        chief_seed.x,
        chief_seed.y,
        1.0);
    double lx = chief_ray.l;
    double my = chief_ray.m;
    double nz = chief_ray.n;
    if (!chief_ray.valid) {
        d_field_direction(cfg, field, &lx, &my, &nz);
    }
    if (!d_finite(lx) || !d_finite(my) || !d_finite(nz)) {
        return out;
    }
    d_normalize3(&lx, &my, &nz);
    const double field_radius = hypot(field.hx, field.hy);
    double mer_x = 0.0;
    double mer_y = 1.0;
    if (field_radius > 1.0e-12) {
        mer_x = field.hx / field_radius;
        mer_y = field.hy / field_radius;
    }
    const double transverse_sin = hypot(lx, my);
    double mx = nz * mer_x;
    double myv = nz * mer_y;
    double mz = -transverse_sin;
    d_normalize3(&mx, &myv, &mz);
    double sx = mer_y;
    double sy = -mer_x;
    double sz = 0.0;
    d_normalize3(&sx, &sy, &sz);
    double tx = sx * edge_px + mx * edge_py;
    double ty = sy * edge_px + myv * edge_py;
    double tz = sz * edge_px + mz * edge_py;
    const double tnorm = sqrt(tx * tx + ty * ty + tz * tz);
    if (!d_finite(tnorm) || tnorm <= 1.0e-12) {
        return out;
    }
    tx /= tnorm;
    ty /= tnorm;
    tz /= tnorm;

    const double first_z = d_surface_vertex_z(surfaces, cfg.surface_count, first);
    const double shifted_x = chief_seed.x + tx * beam_radius;
    const double shifted_y = chief_seed.y + ty * beam_radius;
    const double shifted_z = first_z + chief_sag + tz * beam_radius;
    const double backstep = fmax(1.0, 4.0 * d_first_surface_search_radius(surfaces, cfg, wavelength_um));
    const double start_x = shifted_x - lx * backstep;
    const double start_y = shifted_y - my * backstep;
    const double start_z = shifted_z - nz * backstep;
    double t = 0.0;
    bool local_hit = false;
    if (first_surface.kind != kKindEvenAsphere) {
        local_hit = d_intersect_surface(
            first_surface, start_x, start_y, start_z - first_z,
            lx, my, nz, &t);
    } else if (!cfg.asphere_slope_root_domain_enabled) {
        DeviceSurface p0_surface = first_surface;
        if (cfg.asphere_deep_profile && cfg.asphere_deep_profile_row >= 0) {
            p0_surface.slope_root_domain_radius_squared = INFINITY;
            p0_surface.slope_root_domain_cached_reference_radius = -1.0;
            p0_surface.slope_root_domain_cached_clear_radius = -1.0;
        }
        local_hit = de_cuda_detail::intersect_asphere_from_local_seed(
            p0_surface, start_x, start_y, start_z - first_z,
            lx, my, nz, backstep, &t);
        if (cfg.asphere_deep_profile && cfg.asphere_deep_profile_row >= 0) {
            double u1_t = NAN;
            int u1_failure_reason = 0;
            const bool u1_ok =
                de_cuda_detail::intersect_asphere_nearest_forward_certified(
                    first_surface,
                    start_x,
                    start_y,
                    start_z - first_z,
                    lx,
                    my,
                    nz,
                    4,
                    cfg.asphere_confirmation_segments,
                    nullptr,
                    nullptr,
                    false,
                    false,
                    &u1_t,
                    &u1_failure_reason);
            d_record_asphere_deep_comparison(
                cfg,
                first,
                kAsphereDeepRoleEdge,
                kAsphereDeepSiteTransverseChiefShift,
                u1_ok,
                u1_t,
                local_hit,
                t,
                u1_failure_reason);
        }
    } else {
        if (cfg.asphere_aiming_local_branch) {
            local_hit = de_cuda_detail::intersect_asphere_from_local_seed(
                first_surface, start_x, start_y, start_z - first_z,
                lx, my, nz, backstep, &t);
            if (local_hit && !d_asphere_root_in_slope_domain(
                    first_surface, start_x, start_y, lx, my, t)) {
                local_hit = false;
            }
        }
        if (!local_hit &&
            !(cfg.asphere_upper_bound_mode == 2 &&
              cfg.asphere_aiming_local_branch)) {
            local_hit = d_intersect_even_asphere_configured(
                first_surface, cfg, start_x, start_y, start_z - first_z,
                lx, my, nz, true, false, false, nullptr, &t,
                first, kAsphereDeepRoleEdge);
        }
    }
    if (!local_hit ||
        !d_finite(t) ||
        t < -1.0e-9) {
        return out;
    }
    out.x = start_x + lx * t;
    out.y = start_y + my * t;
    out.valid = d_finite(out.x) && d_finite(out.y) ? 1 : 0;
    if (!d_seed_in_first_surface_bounds(surfaces, cfg, wavelength_um, out)) {
        out.valid = 0;
    }
    return out;
}
} // namespace
} // namespace optiland
