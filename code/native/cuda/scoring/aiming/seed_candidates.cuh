#pragma once
#include "../prelude.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../contracts/ray_record.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/material_and_sag.cuh"
#include "../geometry/mechanical_constraints.cuh"
#include "../geometry/pupil_paraxial.cuh"

namespace optiland {
namespace {
__device__ int d_first_ray_aiming_surface(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    double wavelength_um) {
    const int first = cfg.first_optical_surface;
    if (first >= cfg.surface_count) {
        return first;
    }
    double n_before = 1.0;
    if (cfg.surface_count > 0 && surfaces[0].kind == kKindObject) {
        const double object_n =
            d_surface_refractive_index(surfaces[0], cfg, wavelength_um);
        if (d_finite(object_n) && object_n > kDeviceEps) {
            n_before = object_n;
        }
    }
    for (int i = first; i < cfg.surface_count; ++i) {
        const DeviceSurface& surface = surfaces[i];
        if (surface.kind == kKindImage) {
            break;
        }
        const double n_after =
            d_surface_refractive_index(surface, cfg, wavelength_um);
        if (!d_finite(n_after) || n_after <= kDeviceEps) {
            continue;
        }
        const double scale = fmax(1.0, fmax(fabs(n_before), fabs(n_after)));
        if (surface.reflective || fabs(n_after - n_before) > 1.0e-9 * scale) {
            return i;
        }
        n_before = n_after;
    }
    return first;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_first_surface_hard_radius(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    double wavelength_um) {
    if (d_defer_secondary_apertures(cfg, wavelength_um)) return 0.0;
    const int first = d_first_ray_aiming_surface(surfaces, cfg, wavelength_um);
    if (first >= 0 && first < cfg.surface_count) {
        const DeviceSurface& surface = surfaces[first];
        if (surface.aperture_radius > 0.0) {
            return surface.aperture_radius;
        }
        if (surface.clear_aperture_fixed && surface.semi_aperture > 0.0) {
            return surface.semi_aperture;
        }
    }
    return 0.0;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_first_surface_search_radius(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    double wavelength_um) {
    const double hard_radius =
        d_first_surface_hard_radius(surfaces, cfg, wavelength_um);
    if (hard_radius > 0.0) {
        return hard_radius;
    }
    const double ep_radius =
        fmax(0.0, d_entrance_pupil_radius_from_cfg(surfaces, cfg, wavelength_um));
    const double track = d_finite(d_total_track(surfaces, cfg))
                             ? fabs(d_total_track(surfaces, cfg))
                             : 0.0;
    const double theta = d_finite(cfg.max_field_deg)
                             ? fabs(cfg.max_field_deg) *
                                   3.14159265358979323846 / 180.0
                             : 0.0;
    const double field_extent = track * fmin(1.0, fabs(sin(theta)));
    const double field_slope = d_finite(theta) ? fmin(64.0, fabs(tan(theta))) : 0.0;
    const double slope_extent = track * field_slope;
    return fmax(
        1.0,
        fmax(
            4.0 * ep_radius,
            fmax(
                2.0 * field_extent + 4.0 * ep_radius,
                0.75 * slope_extent + 4.0 * ep_radius)));
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_seed_in_first_surface_bounds(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    double wavelength_um,
    const DeviceSeed& seed) {
    if (!seed.valid || !d_finite(seed.x) || !d_finite(seed.y)) {
        return false;
    }
    const double hard_radius =
        d_first_surface_hard_radius(surfaces, cfg, wavelength_um);
    return hard_radius <= 0.0 ||
           hypot(seed.x, seed.y) <= 1.01 * hard_radius;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __noinline__ DeviceSeed d_clip_seed_to_first_surface(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    double wavelength_um,
    DeviceSeed seed) {
    if (!seed.valid || !d_finite(seed.x) || !d_finite(seed.y)) {
        return DeviceSeed{};
    }
    const double hard_radius =
        d_first_surface_hard_radius(surfaces, cfg, wavelength_um);
    const double radius = hypot(seed.x, seed.y);
    if (hard_radius > 0.0 && d_finite(radius) &&
        radius > 1.01 * hard_radius) {
        const double scale = 1.01 * hard_radius * (1.0 - 1.0e-8) / radius;
        seed.x *= scale;
        seed.y *= scale;
    }
    return seed;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __noinline__ bool d_append_unique_clipped_seed(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    double wavelength_um,
    DeviceSeed* seeds,
    int* count,
    int capacity,
    DeviceSeed seed) {
    if (!seeds || !count || *count >= capacity) {
        return false;
    }
    seed = d_clip_seed_to_first_surface(
        surfaces,
        cfg,
        wavelength_um,
        seed);
    if (!d_seed_in_first_surface_bounds(
            surfaces,
            cfg,
            wavelength_um,
            seed)) {
        return false;
    }
    const double duplicate_limit =
        1.0e-8 * fmax(
            1.0,
            d_first_surface_search_radius(surfaces, cfg, wavelength_um));
    for (int i = 0; i < *count; ++i) {
        const double dx = seeds[i].x - seed.x;
        const double dy = seeds[i].y - seed.y;
        // Preserve the distance test at its boundary; most candidates are distinct.
        if (fabs(dx) > duplicate_limit || fabs(dy) > duplicate_limit) continue;
        if (hypot(dx, dy) <= duplicate_limit) {
            return false;
        }
    }
    seeds[(*count)++] = seed;
    return true;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __noinline__ DeviceSeed d_inward_seed(DeviceSeed seed, double distance) {
    if (!seed.valid || !(distance > 0.0)) {
        return seed;
    }
    const double radius = hypot(seed.x, seed.y);
    if (!d_finite(radius) || radius <= 1.0e-12) {
        return seed;
    }
    const double scale = fmax(0.0, radius - distance) / radius;
    seed.x *= scale;
    seed.y *= scale;
    return seed;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __noinline__ void d_append_boundary_scan_seeds(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    double wavelength_um,
    DeviceSeed* seeds,
    int* count,
    int capacity,
    const DeviceSeed& start,
    double dx,
    double dy) {
    if (!start.valid || !d_finite(start.x) || !d_finite(start.y)) {
        return;
    }
    double norm = hypot(dx, dy);
    if (!d_finite(norm) || norm <= 1.0e-12) {
        return;
    }
    dx /= norm;
    dy /= norm;
    const double hard_radius =
        d_first_surface_hard_radius(surfaces, cfg, wavelength_um);
    const double soft_radius =
        d_first_surface_search_radius(surfaces, cfg, wavelength_um);
    const double scan_radius = hard_radius > 0.0 ? hard_radius : soft_radius;
    if (!(scan_radius > 0.0)) {
        return;
    }
    const double boundary = scan_radius * (1.0 - 1.0e-8);
    const double b = start.x * dx + start.y * dy;
    const double c = start.x * start.x + start.y * start.y - boundary * boundary;
    const double disc = b * b - c;
    if (!d_finite(disc) || disc <= 0.0) {
        return;
    }
    const double distance = -b + sqrt(disc);
    if (!d_finite(distance) || distance <= 0.0) {
        return;
    }
    const double fractions[4] = {0.25, 0.5, 0.75, 1.0};
    for (int i = 0; i < 4; ++i) {
        DeviceSeed seed;
        seed.x = start.x + fractions[i] * distance * dx;
        seed.y = start.y + fractions[i] * distance * dy;
        seed.valid = 1;
        d_append_unique_clipped_seed(
            surfaces,
            cfg,
            wavelength_um,
            seeds,
            count,
            capacity,
            seed);
    }
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __noinline__ void d_append_local_budget_seed_family(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    double wavelength_um,
    DeviceSeed* seeds,
    int* count,
    int capacity,
    double center_radius,
    const DeviceSeed& origin,
    const DeviceSeed* toward) {
    if (!origin.valid) {
        return;
    }
    d_append_unique_clipped_seed(
        surfaces,
        cfg,
        wavelength_um,
        seeds,
        count,
        capacity,
        origin);
    const double inward_factors[3] = {0.5, 1.0, 1.5};
    for (int i = 0; i < 3; ++i) {
        d_append_unique_clipped_seed(
            surfaces,
            cfg,
            wavelength_um,
            seeds,
            count,
            capacity,
            d_inward_seed(origin, inward_factors[i] * center_radius));
    }
    double dx = 0.0;
    double dy = 0.0;
    if (toward && toward->valid) {
        dx = toward->x - origin.x;
        dy = toward->y - origin.y;
    }
    double norm = hypot(dx, dy);
    if ((!d_finite(norm) || norm <= 1.0e-12) && origin.valid) {
        dx = origin.x;
        dy = origin.y;
        norm = hypot(dx, dy);
    }
    if (!d_finite(norm) || norm <= 1.0e-12) {
        return;
    }
    dx /= norm;
    dy /= norm;
    const double scan_step = fmax(1.0, center_radius);
    const double radial_offsets[6] = {0.25, -0.25, 0.5, -0.5, 1.0, -1.0};
    for (int i = 0; i < 6; ++i) {
        DeviceSeed seed;
        seed.x = origin.x + radial_offsets[i] * scan_step * dx;
        seed.y = origin.y + radial_offsets[i] * scan_step * dy;
        seed.valid = 1;
        d_append_unique_clipped_seed(
            surfaces,
            cfg,
            wavelength_um,
            seeds,
            count,
            capacity,
            seed);
    }
    const double px = -dy;
    const double py = dx;
    const double transverse_offsets[4] = {0.25, -0.25, 0.5, -0.5};
    for (int i = 0; i < 4; ++i) {
        DeviceSeed seed;
        seed.x = origin.x + transverse_offsets[i] * scan_step * px;
        seed.y = origin.y + transverse_offsets[i] * scan_step * py;
        seed.valid = 1;
        d_append_unique_clipped_seed(
            surfaces,
            cfg,
            wavelength_um,
            seeds,
            count,
            capacity,
            seed);
    }
    d_append_boundary_scan_seeds(
        surfaces,
        cfg,
        wavelength_um,
        seeds,
        count,
        capacity,
        origin,
        dx,
        dy);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __noinline__ void d_append_field_continuation_seeds(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceField& field,
    double wavelength_um,
    int field_index,
    DeviceSeed* seeds,
    int* count,
    int capacity,
    double center_radius,
    const DeviceSeed* previous_seed,
    const DeviceField* previous_field,
    const DeviceSeed* previous2_seed,
    const DeviceField* previous2_field) {
    if (field_index <= 0 || !previous_seed || !previous_seed->valid ||
        !previous_field) {
        return;
    }
    d_append_unique_clipped_seed(
        surfaces,
        cfg,
        wavelength_um,
        seeds,
        count,
        capacity,
        *previous_seed);
    if (field_index > 1) {
        if (!previous2_seed || !previous2_seed->valid || !previous2_field) {
            return;
        }
        const double rho = hypot(field.hx, field.hy);
        const double rho0 = hypot(previous_field->hx, previous_field->hy);
        const double rho1 = hypot(previous2_field->hx, previous2_field->hy);
        double alpha = (rho - rho0) / fmax(1.0e-12, rho0 - rho1);
        alpha = d_clamp(
            alpha,
            0.0,
            fmax(0.0, cfg.calibrated_max_field_extrapolation_factor));
        DeviceSeed extrapolated;
        extrapolated.x =
            previous_seed->x + alpha * (previous_seed->x - previous2_seed->x);
        extrapolated.y =
            previous_seed->y + alpha * (previous_seed->y - previous2_seed->y);
        extrapolated.valid = 1;
        d_append_unique_clipped_seed(
            surfaces,
            cfg,
            wavelength_um,
            seeds,
            count,
            capacity,
            extrapolated);

        double dx = previous_seed->x - previous2_seed->x;
        double dy = previous_seed->y - previous2_seed->y;
        double norm = hypot(dx, dy);
        if ((!d_finite(norm) || norm <= 1.0e-12) && extrapolated.valid) {
            dx = extrapolated.x;
            dy = extrapolated.y;
            norm = hypot(dx, dy);
        }
        if (!d_finite(norm) || norm <= 1.0e-12) {
            return;
        }
        dx /= norm;
        dy /= norm;
        const double scan_step = fmax(1.0, center_radius);
        const double offsets[17] = {
            0.0, 0.5, -0.5, 1.0, -1.0,
            1.25, 1.5, 1.75, -2.0,
            2.0, 2.125, 2.25, 2.5, 2.75,
            3.0, 3.5, 4.0};
        for (int i = 0; i < 17; ++i) {
            DeviceSeed scan;
            scan.x = extrapolated.x + offsets[i] * scan_step * dx;
            scan.y = extrapolated.y + offsets[i] * scan_step * dy;
            scan.valid = 1;
            d_append_unique_clipped_seed(
                surfaces,
                cfg,
                wavelength_um,
                seeds,
                count,
                capacity,
                scan);
        }
        d_append_boundary_scan_seeds(
            surfaces,
            cfg,
            wavelength_um,
            seeds,
            count,
            capacity,
            *previous_seed,
            dx,
            dy);
        return;
    }

    double dx = previous_seed->x;
    double dy = previous_seed->y;
    const double norm = hypot(dx, dy);
    if (!d_finite(norm) || norm <= 1.0e-12) {
        return;
    }
    dx /= norm;
    dy /= norm;
    d_append_boundary_scan_seeds(
        surfaces,
        cfg,
        wavelength_um,
        seeds,
        count,
        capacity,
        *previous_seed,
        dx,
        dy);
}
} // namespace
} // namespace optiland
