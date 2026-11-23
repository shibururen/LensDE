#pragma once
#include "../prelude.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../contracts/ray_record.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/material_and_sag.cuh"
#include "../geometry/mechanical_constraints.cuh"
#include "first_order_constraints.cuh"
#include "residual_primitives.cuh"
#include "supported_ray_generation.cuh"
#include "../ray/sequential_trace.cuh"

namespace optiland {
namespace {
__device__ void d_add_constraint_delta(
    double* sum,
    const DeviceConfig& cfg,
    double delta,
    int tolerance_kind) {
    if (!sum || fabs(cfg.weight_edge) <= 0.0) {
        return;
    }
    const double tolerance = d_constraint_tolerance(cfg, tolerance_kind);
    const double residual = d_equivalent_spot_residual(cfg, delta, tolerance);
    *sum += residual * residual;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_thickness_constraint_active(
    const DeviceConfig& cfg,
    int surface_index) {
    if (surface_index < 0 || surface_index >= 64) {
        return false;
    }
    // Match configure_variable_thickness_constraints() on the CPU.  A
    // non-empty mask means that only variable thickness intervals are
    // constrained; an empty mask is the frozen-geometry contract and keeps
    // every physical interval active.  Treating an empty mask as "no
    // constraints" silently dropped constant manufacturing penalties from
    // material-only and vignetting-only CUDA scoring.
    return cfg.thickness_variable_mask == 0ULL ||
           (cfg.thickness_variable_mask & (1ULL << surface_index)) != 0ULL;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_same_sign_tight_triplet_prior(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg) {
    if (!surfaces || cfg.structural_prior_tight_triplet_weight <= 0.0 ||
        cfg.structural_prior_gap_tolerance <= 0.0) {
        return 0.0;
    }

    int fronts[3] = {-1, -1, -1};
    int rears[3] = {-1, -1, -1};
    int element_count = 0;
    double penalty = 0.0;
    int i = cfg.first_optical_surface;
    while (i + 1 < cfg.surface_count) {
        if (!d_interval_is_glass(surfaces[i])) {
            ++i;
            continue;
        }
        const int front = i;
        while (i + 1 < cfg.surface_count && d_interval_is_glass(surfaces[i])) {
            ++i;
        }
        const int slot = element_count < 3 ? element_count : 2;
        if (element_count >= 3) {
            fronts[0] = fronts[1];
            fronts[1] = fronts[2];
            rears[0] = rears[1];
            rears[1] = rears[2];
        }
        fronts[slot] = front;
        rears[slot] = i;
        ++element_count;
        if (element_count < 3) {
            continue;
        }
        if (rears[0] + 1 != fronts[1] || rears[1] + 1 != fronts[2]) {
            continue;
        }

        int sign = 0;
        bool same_sign = true;
        for (int element = 0; element < 3 && same_sign; ++element) {
            const int indices[2] = {fronts[element], rears[element]};
            for (int side = 0; side < 2; ++side) {
                const DeviceSurface& surface = surfaces[indices[side]];
                if (d_is_plane(surface) || !d_finite(surface.radius) ||
                    fabs(surface.radius) < kDeviceEps) {
                    same_sign = false;
                    break;
                }
                const double curvature = 1.0 / surface.radius;
                if (!d_finite(curvature) ||
                    fabs(curvature) <= cfg.structural_prior_curvature_epsilon) {
                    same_sign = false;
                    break;
                }
                const int current_sign = curvature > 0.0 ? 1 : -1;
                if (sign == 0) {
                    sign = current_sign;
                } else if (current_sign != sign) {
                    same_sign = false;
                    break;
                }
            }
        }
        if (!same_sign || sign == 0 ||
            !d_thickness_constraint_active(cfg, rears[0]) ||
            !d_thickness_constraint_active(cfg, rears[1]) ||
            d_interval_is_glass(surfaces[rears[0]]) ||
            d_interval_is_glass(surfaces[rears[1]])) {
            continue;
        }
        const double tolerance = cfg.structural_prior_gap_tolerance;
        const double first_activation = d_clamp01(
            (cfg.air_center_lower + tolerance - surfaces[rears[0]].thickness) /
            tolerance);
        const double second_activation = d_clamp01(
            (cfg.air_center_lower + tolerance - surfaces[rears[1]].thickness) /
            tolerance);
        const double activation = fmin(first_activation, second_activation);
        penalty += cfg.structural_prior_tight_triplet_weight *
                   activation * activation;
    }
    return penalty;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_constraint_group_error(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    double min_edge,
    double guard_surface_order_edge_deficit_squared) {
    double constraint_sum = 0.0;
    if (cfg.stop_air_edge_surface >= 0 && cfg.stop_air_edge_weight > 0.0) {
        const double value = d_stop_air_edge_thickness_at(
            surfaces, cfg, cfg.stop_air_edge_surface);
        const double residual = d_equivalent_spot_residual(
            cfg, d_lower_delta(value, cfg.stop_air_edge_lower),
            fmax(1.0e-9, cfg.quality_edge_tol));
        constraint_sum += cfg.stop_air_edge_weight * residual * residual;
    }
    {
        double constrained_min_edge = min_edge;
        if (cfg.thickness_variable_mask != 0ULL) {
            constrained_min_edge = NAN;
            for (int i = cfg.first_optical_surface;
                 i + 1 < cfg.surface_count;
                 ++i) {
                const DeviceSurface& surface = surfaces[i];
                if (!d_thickness_constraint_active(cfg, i) ||
                    surface.kind == kKindObject ||
                    d_image_space_interval(surfaces, cfg.surface_count, i) ||
                    !d_finite(surface.thickness)) {
                    continue;
                }
                const double edge = d_edge_thickness_at(surfaces, cfg, i);
                constrained_min_edge = !d_finite(constrained_min_edge)
                                           ? edge
                                           : fmin(constrained_min_edge, edge);
            }
        }
        d_add_constraint_delta(
            &constraint_sum,
            cfg,
            d_lower_delta(constrained_min_edge, cfg.min_edge_thickness),
            0);
    }
    d_add_constraint_delta(
        &constraint_sum,
        cfg,
        sqrt(fmax(0.0, guard_surface_order_edge_deficit_squared)),
        0);

    if (cfg.surface_tangent_angle_lower_deg > 0.0) {
        const double lower = fmin(90.0, cfg.surface_tangent_angle_lower_deg);
        for (int i = cfg.first_optical_surface; i < cfg.surface_count; ++i) {
            const DeviceSurface& surface = surfaces[i];
            if (surface.kind == kKindObject || surface.kind == kKindImage) {
                continue;
            }
            const double min_angle = d_minimum_surface_tangent_axis_angle_deg(
                surface,
                cfg.surface_tangent_angle_samples);
            d_add_constraint_delta(
                &constraint_sum,
                cfg,
                d_lower_delta(min_angle, lower),
                2);
        }
    }

    for (int i = cfg.first_optical_surface; i + 1 < cfg.surface_count; ++i) {
        const DeviceSurface& surface = surfaces[i];
        if (surface.kind == kKindObject ||
            d_image_space_interval(surfaces, cfg.surface_count, i) ||
            !d_finite(surface.thickness)) {
            continue;
        }
        // The aggregate BFL path and the physical thickness of its reference
        // interval are independent constraints. Keep the reference interval
        // in the ordinary center/edge thickness checks, matching the CPU path.
        if (!d_thickness_constraint_active(cfg, i)) {
            continue;
        }
        const bool glass = d_interval_is_glass(surface);
        const double center_lower = glass ? cfg.glass_center_lower : cfg.air_center_lower;
        const double center_upper = glass ? cfg.glass_center_upper : cfg.air_center_upper;
        const double edge_lower = glass ? cfg.glass_edge_lower : cfg.air_edge_lower;
        const double edge_upper = glass ? cfg.glass_edge_upper : cfg.air_edge_upper;
        d_add_constraint_delta(
            &constraint_sum,
            cfg,
            d_range_delta(surface.thickness, center_lower, center_upper),
            0);
        const double edge = d_edge_thickness_at(surfaces, cfg, i);
        d_add_constraint_delta(
            &constraint_sum,
            cfg,
            d_range_delta(edge, edge_lower, edge_upper),
            0);
        if (glass) {
            const double radius =
                d_edge_thickness_sample_radius(surface, surfaces[i + 1], cfg.entrance_pupil_radius);
            const double diameter = radius > 0.0 ? 2.0 * radius : NAN;
            const double atc = diameter > 0.0 ? surface.thickness / diameter : NAN;
            const double ate = diameter > 0.0 ? edge / diameter : NAN;
            d_add_constraint_delta(
                &constraint_sum,
                cfg,
                d_range_delta(
                    atc,
                    d_active_lower_bound(cfg.atc_lower),
                    d_active_upper_bound(cfg.atc_upper)),
                1);
            d_add_constraint_delta(
                &constraint_sum,
                cfg,
                d_range_delta(
                    ate,
                    d_active_lower_bound(cfg.ate_lower),
                    d_active_upper_bound(cfg.ate_upper)),
                1);
        }
    }

    for (int i = cfg.first_optical_surface; i < cfg.surface_count; ++i) {
        const DeviceSurface& surface = surfaces[i];
        if (surface.kind == kKindObject || surface.kind == kKindImage ||
            (surface.is_stop && !d_interval_is_glass(surface))) {
            continue;
        }
        double bl0 = NAN;
        if (d_is_plane(surface)) {
            bl0 = 0.0;
        } else {
            double aperture = d_clear_aperture_radius(surface);
            if (!(aperture > 0.0)) {
                aperture = d_mechanical_aperture_radius(surface);
            }
            if (aperture > 0.0) {
                const double radius = fabs(surface.radius);
                if (!d_finite(radius) || radius > 0.5 * kDeviceInf) {
                    bl0 = 0.0;
                } else if (radius <= kDeviceEps) {
                    bl0 = INFINITY;
                } else {
                    bl0 = aperture / radius;
                }
            }
        }
        d_add_constraint_delta(
            &constraint_sum,
            cfg,
            d_range_delta(
                bl0,
                d_active_lower_bound(cfg.bl0_lower),
                d_active_upper_bound(cfg.bl0_upper)),
            1);
    }
    if (cfg.conic_soft_bound_mask != 0ULL) {
        const double tolerance = fmax(1.0e-9, cfg.quality_conic_coefficient_tol);
        for (int i = cfg.first_optical_surface; i < cfg.surface_count && i < 64; ++i) {
            if ((cfg.conic_soft_bound_mask & (1ULL << i)) == 0ULL) {
                continue;
            }
            const double delta = d_range_delta(
                surfaces[i].conic,
                cfg.conic_soft_bound_lower,
                cfg.conic_soft_bound_upper);
            const double residual = d_equivalent_spot_residual(cfg, delta, tolerance);
            constraint_sum += residual * residual;
        }
    }
    return constraint_sum;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kConstraintDebugStride = 17;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void constraint_debug_kernel(
    const DeviceSurface* candidate_surfaces,
    DeviceConfig cfg,
    int row,
    double* values) {
    if (!candidate_surfaces || !values || blockIdx.x != 0 || threadIdx.x != 0 || row < 0) {
        return;
    }
    const DeviceSurface* surfaces =
        candidate_surfaces + static_cast<std::size_t>(row) * cfg.surface_count;
    for (int i = 0; i < cfg.surface_count; ++i) {
        double* out = values + static_cast<std::size_t>(i) * kConstraintDebugStride;
        const DeviceSurface& surface = surfaces[i];
        const bool has_interval =
            i + 1 < cfg.surface_count && surface.kind != kKindObject &&
            !d_image_space_interval(surfaces, cfg.surface_count, i) &&
            d_finite(surface.thickness);
        const bool glass = has_interval && d_interval_is_glass(surface);
        const double edge = has_interval ? d_edge_thickness_at(surfaces, cfg, i) : NAN;
        const double radius = has_interval
                                  ? d_edge_thickness_sample_radius(
                                        surface,
                                        surfaces[i + 1],
                                        cfg.entrance_pupil_radius)
                                  : NAN;
        const double diameter = radius > 0.0 ? 2.0 * radius : NAN;
        const double atc = glass && diameter > 0.0 ? surface.thickness / diameter : NAN;
        const double ate = glass && diameter > 0.0 ? edge / diameter : NAN;
        const double edge_lower =
            glass ? cfg.glass_edge_lower : cfg.air_edge_lower;
        const double edge_upper =
            glass ? cfg.glass_edge_upper : cfg.air_edge_upper;
        const double edge_delta = has_interval
                                      ? d_range_delta(edge, edge_lower, edge_upper)
                                      : NAN;
        const double ate_delta = glass
                                     ? d_range_delta(
                                           ate,
                                           d_active_lower_bound(cfg.ate_lower),
                                           d_active_upper_bound(cfg.ate_upper))
                                     : NAN;
        out[0] = static_cast<double>(surface.kind);
        out[1] = surface.semi_aperture;
        out[2] = surface.aperture_radius;
        out[3] = surface.mechanical_aperture;
        out[4] = surface.thickness;
        out[5] = edge;
        out[6] = radius;
        out[7] = atc;
        out[8] = ate;
        out[9] = edge_delta;
        out[10] = ate_delta;
        for (int slot = 11; slot < kConstraintDebugStride; ++slot) {
            out[slot] = NAN;
        }
    }
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void aperture_trace_debug_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    DeviceConfig cfg,
    int row,
    double* values) {
    if (!candidate_surfaces || !fields || !waves || !values ||
        blockIdx.x != 0 || threadIdx.x != 0 || row < 0) {
        return;
    }
    const DeviceSurface* surfaces =
        candidate_surfaces + static_cast<std::size_t>(row) * cfg.surface_count;
    DeviceConfig aperture_cfg = cfg;
    aperture_cfg.fixed_budget_sampling = 0; // Physical aperture envelope is unchanged.
    aperture_cfg.num_rays = max(1, cfg.clear_aperture_rings);
    aperture_cfg.pupil_arms = cfg.clear_aperture_arms;
    aperture_cfg.pupil_sampling_type = kPupilSamplingGaussianRingArm;
    const int pupil_count = d_shadow_aperture_pupil_count(aperture_cfg);
    for (int fi = 0; fi < cfg.field_count; ++fi) {
        for (int wi = 0; wi < cfg.wavelength_count; ++wi) {
            const DeviceWave wave = waves[wi];
            if (wave.weight <= 0.0) {
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
                    d_aperture_guard_pupil(
                        pi - pupil_count, &px, &py);
                }
                DeviceRay ray = d_generate_supported_ray(
                    surfaces,
                    cfg,
                    fields[fi],
                    px,
                    py,
                    pw,
                    wave.value_um);
                double heights[64] = {};
                if (pi >= pupil_count) {
                    d_trace_ray_for_aperture_envelope<true>(
                        surfaces, cfg, &ray, heights, false);
                } else {
                    d_trace_ray_for_aperture_envelope<true>(
                        surfaces, cfg, &ray, heights, false);
                }
                for (int i = 0; i < cfg.surface_count && i < 64; ++i) {
                    double* out =
                        values + static_cast<std::size_t>(i) * kConstraintDebugStride;
                    if (!d_finite(out[11]) || heights[i] > out[11]) {
                        out[11] = heights[i];
                        out[12] = static_cast<double>(fi);
                        out[13] = static_cast<double>(wi);
                        out[14] = static_cast<double>(pi);
                        out[15] = px;
                        out[16] = py;
                    }
                }
            }
        }
    }
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_trace_priority_metric(
    const CudaScoreSummary& summary,
    const DeviceConfig& cfg) {
    if (!cfg.soft_trace) {
        return 0.0;
    }
    double metric = 0.0;
    if (cfg.require_chief_ray && summary.chief_ray_failed_fields > 0) {
        metric += static_cast<double>(summary.chief_ray_failed_fields);
    }
    if (cfg.require_off_axis_field_coverage &&
        summary.zero_coverage_off_axis_fields > 0) {
        metric += static_cast<double>(summary.zero_coverage_off_axis_fields);
    }
    if (d_finite(summary.trace_coverage)) {
        const double deficit =
            fmax(0.0, cfg.min_system_coverage - summary.trace_coverage);
        metric += deficit * deficit;
    } else {
        metric += 1.0;
    }
    if (d_finite(summary.min_field_coverage)) {
        const double deficit =
            fmax(0.0, cfg.min_field_coverage - summary.min_field_coverage);
        metric += deficit * deficit;
    } else {
        metric += 1.0;
    }
    if (d_finite(summary.trace_progress)) {
        const double deficit = fmax(0.0, 1.0 - summary.trace_progress);
        metric += 0.5 * deficit * deficit;
    } else {
        metric += 0.5;
    }
    if (d_finite(summary.min_trace_progress)) {
        const double deficit = fmax(0.0, 1.0 - summary.min_trace_progress);
        metric += 0.5 * deficit * deficit;
    } else {
        metric += 0.5;
    }
    if (cfg.require_chief_ray) {
        if (d_finite(summary.min_chief_progress)) {
            const double deficit = fmax(0.0, 1.0 - summary.min_chief_progress);
            metric += 0.5 * deficit * deficit;
        } else {
            metric += 0.5;
        }
    }
    return metric;
}
} // namespace
} // namespace optiland
