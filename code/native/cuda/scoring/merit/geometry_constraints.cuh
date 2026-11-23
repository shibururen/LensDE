#pragma once
#include "../prelude.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../contracts/ray_record.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/first_order_metrics.cuh"
#include "../geometry/pupil_paraxial.cuh"
#include "first_order_constraints.cuh"
#include "residual_primitives.cuh"
#include "supported_ray_generation.cuh"
#include "../ray/sequential_trace.cuh"

namespace optiland {
namespace {
__device__ void d_update_clear_apertures_from_trace(
    DeviceSurface* surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    const DeviceConfig& cfg) {
    if (!surfaces || !fields || !waves || cfg.surface_count <= 0) {
        return;
    }
    double max_heights[64];
    double original[64];
    const int surface_count = min(cfg.surface_count, 64);
    for (int i = 0; i < surface_count; ++i) {
        max_heights[i] = 0.0;
        original[i] = surfaces[i].semi_aperture;
    }
    if (cfg.aperture_type == 3 ||
        (cfg.aiming_mode != 0 && cfg.aiming_mode != 1)) {
        d_sync_stop_aperture_to_aiming_radius(surfaces, cfg);
    }

    DeviceConfig aperture_cfg = cfg;
    aperture_cfg.fixed_budget_sampling = 0; // Physical aperture envelope is unchanged.
    aperture_cfg.num_rays = max(1, cfg.clear_aperture_rings);
    aperture_cfg.pupil_arms = cfg.clear_aperture_arms;
    aperture_cfg.pupil_sampling_type = kPupilSamplingGaussianRingArm;
    const int pupil_count = d_shadow_aperture_pupil_count(aperture_cfg);
    for (int fi = 0; fi < cfg.field_count; ++fi) {
        const DeviceField field = fields[fi];
        for (int wi = 0; wi < cfg.wavelength_count; ++wi) {
            const DeviceWave wave = waves[wi];
            if ((cfg.primary_wavelength_apertures && wi != cfg.primary_wave_index) || wave.weight <= 0.0) {
                continue;
            }
            for (int pi = 0; pi < pupil_count + kApertureGuardCount; ++pi) {
                double px = 0.0;
                double py = 0.0;
                double pw = 1.0;
                if (pi < pupil_count) {
                    d_shadow_aperture_pupil_at(
                        aperture_cfg, pi, &px, &py, &pw);
                } else {
                    d_aperture_guard_pupil(pi - pupil_count, &px, &py);
                }
                DeviceRay ray = d_generate_supported_ray(
                    surfaces,
                    cfg,
                    field,
                    px,
                    py,
                    pw,
                    wave.value_um);
                d_trace_ray_for_aperture_envelope<true>(
                    surfaces,
                    cfg,
                    &ray,
                    max_heights,
                    false,
                    false,
                    cfg.aiming_mode == 0 || cfg.aiming_mode == 1);
            }
        }
    }
    const double scale = fmax(1.0, cfg.clear_aperture_margin);
    const int stop = d_stop_index(surfaces, cfg);
    for (int i = 0; i < surface_count; ++i) {
        DeviceSurface& surface = surfaces[i];
        if (surface.kind == kKindObject) {
            continue;
        }
        const bool explicit_stop =
            surface.clear_aperture_fixed ||
            (cfg.aperture_type == 3 && surface.aperture_radius > 0.0);
        if (surface.is_stop) {
            if (explicit_stop) {
                if (surface.clear_aperture_fixed &&
                    surface.semi_aperture <= 0.0 &&
                    surface.aperture_radius > 0.0) {
                    surface.semi_aperture = surface.aperture_radius;
                }
                continue;
            }
            const double stop_radius =
                (cfg.aperture_type != 3 &&
                 (cfg.aiming_mode == 0 || cfg.aiming_mode == 1))
                    ? max_heights[i]
                    : d_aperture_stop_radius(
                          surfaces, cfg, cfg.primary_wavelength);
            if (d_finite(stop_radius) && stop_radius > 0.0) {
                surface.semi_aperture = stop_radius;
                if (!surface.clear_aperture_fixed) {
                    surface.aperture_radius = stop_radius;
                }
                if (!surface.mechanical_aperture_fixed) {
                    surface.mechanical_aperture = stop_radius;
                }
            }
            continue;
        }
        (void)stop;
        if (surface.clear_aperture_fixed) {
            if (surface.semi_aperture <= 0.0 && surface.aperture_radius > 0.0) {
                surface.semi_aperture = surface.aperture_radius;
            }
            continue;
        }
        if (surface.aperture_radius > 0.0) {
            surface.semi_aperture = fmax(original[i], surface.aperture_radius);
            continue;
        }
        if (max_heights[i] > 0.0) {
            surface.semi_aperture = max_heights[i] * scale;
        } else if (original[i] > 0.0) {
            surface.semi_aperture = original[i];
        }
    }
    d_update_mechanical_apertures_from_groups(surfaces, cfg);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_glass_proximity_distance(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceGlassProximity& constraint,
    const DeviceGlassProximityPoint* points) {
    if (!points || constraint.index < 0 || constraint.index >= cfg.surface_count ||
        constraint.count <= 0 || constraint.offset < 0 ||
        constraint.offset + constraint.count > cfg.glass_proximity_point_count) {
        return NAN;
    }
    const DeviceSurface& surface = surfaces[constraint.index];
    if (!d_finite(surface.nd) || !d_finite(surface.vd) ||
        surface.nd <= 1.0005 || surface.vd <= 0.0) {
        return NAN;
    }
    const double nd_scale = fmax(1.0e-12, fabs(constraint.nd_scale));
    const double vd_scale = fmax(1.0e-12, fabs(constraint.vd_scale));
    const double vd_relative_scale =
        fmax(1.0e-12, fabs(constraint.vd_relative_scale));
    const bool use_relative_log_vd = constraint.vd_metric == 1;
    double best = INFINITY;
    for (int i = 0; i < constraint.count; ++i) {
        const DeviceGlassProximityPoint point = points[constraint.offset + i];
        if (!d_finite(point.nd) || !d_finite(point.vd) ||
            point.nd <= 1.0 || point.vd <= 0.0) {
            continue;
        }
        const double d_nd = (surface.nd - point.nd) / nd_scale;
        const double d_vd = use_relative_log_vd
                                ? log(surface.vd / point.vd) / vd_relative_scale
                                : (surface.vd - point.vd) / vd_scale;
        best = fmin(best, sqrt(d_nd * d_nd + d_vd * d_vd));
    }
    if (!d_finite(best) || constraint.smooth_temperature <= 0.0) {
        return best;
    }
    const double temperature = fmax(1.0e-12, constraint.smooth_temperature);
    double weighted_sum = 0.0;
    double weight_sum = 0.0;
    for (int i = 0; i < constraint.count; ++i) {
        const DeviceGlassProximityPoint point = points[constraint.offset + i];
        if (!d_finite(point.nd) || !d_finite(point.vd) ||
            point.nd <= 1.0 || point.vd <= 0.0) {
            continue;
        }
        const double d_nd = (surface.nd - point.nd) / nd_scale;
        const double d_vd = use_relative_log_vd
                                ? log(surface.vd / point.vd) / vd_relative_scale
                                : (surface.vd - point.vd) / vd_scale;
        const double distance = sqrt(d_nd * d_nd + d_vd * d_vd);
        const double weight = exp(-(distance - best) / temperature);
        weighted_sum += weight * distance;
        weight_sum += weight;
    }
    return weight_sum > 0.0 ? weighted_sum / weight_sum : best;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_glass_region_distance(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceGlassProximity& constraint,
    const DeviceGlassProximityPoint* points) {
    if (!points || constraint.index < 0 || constraint.index >= cfg.surface_count ||
        constraint.region_count <= 0 || constraint.region_offset < 0 ||
        constraint.region_offset + constraint.region_count >
            cfg.glass_proximity_point_count) {
        return NAN;
    }
    const DeviceSurface& surface = surfaces[constraint.index];
    if (!d_finite(surface.nd) || !d_finite(surface.vd) ||
        surface.nd <= 1.0005 || surface.vd <= 0.0) {
        return NAN;
    }
    const double nd_scale = fmax(1.0e-12, fabs(constraint.nd_scale));
    const double vd_scale = fmax(1.0e-12, fabs(constraint.vd_scale));
    const double vd_relative_scale =
        fmax(1.0e-12, fabs(constraint.vd_relative_scale));
    const bool use_relative_log_vd = constraint.vd_metric == 1;
    const double query_x = surface.nd / nd_scale;
    const double query_y = use_relative_log_vd
                               ? log(surface.vd) / vd_relative_scale
                               : surface.vd / vd_scale;
    double edge_distance = INFINITY;
    bool inside = false;
    for (int i = 0; i < constraint.region_count; ++i) {
        const int j = i == 0 ? constraint.region_count - 1 : i - 1;
        const DeviceGlassProximityPoint a =
            points[constraint.region_offset + j];
        const DeviceGlassProximityPoint b =
            points[constraint.region_offset + i];
        if (!d_finite(a.nd) || !d_finite(a.vd) || a.nd <= 1.0 || a.vd <= 0.0 ||
            !d_finite(b.nd) || !d_finite(b.vd) || b.nd <= 1.0 || b.vd <= 0.0) {
            return NAN;
        }
        const double ax = a.nd / nd_scale;
        const double ay = use_relative_log_vd
                              ? log(a.vd) / vd_relative_scale
                              : a.vd / vd_scale;
        const double bx = b.nd / nd_scale;
        const double by = use_relative_log_vd
                              ? log(b.vd) / vd_relative_scale
                              : b.vd / vd_scale;
        const double dx = bx - ax;
        const double dy = by - ay;
        const double length_sq = dx * dx + dy * dy;
        const double projection = length_sq > 0.0
                                      ? d_clamp(
                                            ((query_x - ax) * dx +
                                             (query_y - ay) * dy) /
                                                length_sq,
                                            0.0,
                                            1.0)
                                      : 0.0;
        const double ex = query_x - (ax + projection * dx);
        const double ey = query_y - (ay + projection * dy);
        edge_distance = fmin(edge_distance, sqrt(ex * ex + ey * ey));
        if (constraint.region_count >= 3 && ((ay > query_y) != (by > query_y))) {
            const double crossing_x = ax + (query_y - ay) * (bx - ax) / (by - ay);
            if (query_x < crossing_x) {
                inside = !inside;
            }
        }
    }
    return (constraint.region_count >= 3 && inside) || edge_distance <= 1.0e-12
               ? 0.0
               : edge_distance;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_glass_proximity_penalty(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceGlassProximity* constraints,
    const DeviceGlassProximityPoint* points) {
    if (!constraints || cfg.glass_proximity_count <= 0) {
        return 0.0;
    }
    double penalty = 0.0;
    for (int i = 0; i < cfg.glass_proximity_count; ++i) {
        const DeviceGlassProximity constraint = constraints[i];
        if (fabs(constraint.weight) > 0.0) {
            const double value =
                d_glass_proximity_distance(surfaces, cfg, constraint, points);
            if (!d_finite(value)) {
                penalty += fabs(constraint.weight) * 1.0e12;
            } else {
                const double excess =
                    fmax(0.0, value - fmax(0.0, constraint.tolerance));
                penalty += fabs(constraint.weight) * excess * excess;
            }
        }
        if (fabs(constraint.region_weight) > 0.0) {
            const double value =
                d_glass_region_distance(surfaces, cfg, constraint, points);
            if (!d_finite(value)) {
                penalty += fabs(constraint.region_weight) * 1.0e12;
            } else {
                const double excess =
                    fmax(0.0, value - fmax(0.0, constraint.region_tolerance));
                penalty += fabs(constraint.region_weight) * excess * excess;
            }
        }
    }
    return penalty;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ void d_add_pseudo_group_term(
    double* sum,
    double* weight,
    double base_weight,
    double delta,
    double tolerance,
    const DeviceConfig& cfg) {
    const double base = fmax(0.0, base_weight);
    if (!sum || !weight || base <= 0.0) {
        return;
    }
    const double normalized = delta / fmax(1.0e-9, tolerance);
    *sum += base * d_pseudo_huber(normalized, cfg.unified_loss_delta);
    *weight += base;
}
} // namespace
} // namespace optiland
