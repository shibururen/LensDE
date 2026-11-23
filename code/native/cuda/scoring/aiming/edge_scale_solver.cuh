#pragma once
#include "../prelude.cuh"
#include "chief_geometry.cuh"
#include "seed_candidates.cuh"
#include "stop_samples.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/calibration_tasks.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../diagnostics/builder_instrumentation.cuh"
#include "../geometry/asphere_slope.cuh"

namespace optiland {
namespace {
__device__ bool d_push_unique_scale(double* scales, int* count, int capacity, double scale) {
    if (!scales || !count || *count >= capacity ||
        !d_finite(scale) || scale <= 0.0) {
        return false;
    }
    const double clamped = d_clamp(scale, 1.0e-3, 1.25);
    for (int i = 0; i < *count; ++i) {
        if (fabs(scales[i] - clamped) <= 1.0e-7 * fmax(1.0, fabs(clamped))) {
            return false;
        }
    }
    scales[*count] = clamped;
    ++(*count);
    return true;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_push_unique_seed(
    DeviceSeed* seeds,
    int* count,
    int capacity,
    DeviceSeed seed) {
    if (!seeds || !count || *count >= capacity ||
        !seed.valid || !d_finite(seed.x) || !d_finite(seed.y)) {
        return false;
    }
    for (int i = 0; i < *count; ++i) {
        if (hypot(seeds[i].x - seed.x, seeds[i].y - seed.y) <= 1.0e-9) {
            return false;
        }
    }
    seeds[*count] = seed;
    ++(*count);
    return true;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_accept_calibrated_edge_sample(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    double wavelength_um,
    DeviceCalibratedEdgeAnchor* anchor,
    const DeviceField& field,
    const DeviceSeed& seed,
    const DeviceSeed& chief_seed,
    double edge_radius,
    double edge_px,
    double edge_py,
    bool enforce_branch_continuity,
    double rs,
    double sx,
    double sy,
    double residual_limit,
    double* max_residual) {
    if (!anchor ||
        !d_seed_in_first_surface_bounds(surfaces, cfg, wavelength_um, seed)) {
        return false;
    }
    if (enforce_branch_continuity) {
        constexpr double kEdgeContinuityFraction = 0.5;
        const double continuity_x = chief_seed.x + edge_radius * edge_px;
        const double continuity_y = chief_seed.y + edge_radius * edge_py;
        const double continuity_limit =
            fmax(1.0e-6, kEdgeContinuityFraction * edge_radius);
        const double continuity_dx = seed.x - continuity_x;
        const double continuity_dy = seed.y - continuity_y;
        if (!chief_seed.valid || !d_finite(edge_radius) || edge_radius <= 0.0 ||
            continuity_dx * continuity_dx + continuity_dy * continuity_dy >
                continuity_limit * continuity_limit) {
            return false;
        }
    }
    const double target_x = (field.fvcx + edge_px * (1.0 - field.fvdx)) * rs;
    const double target_y = (field.fvcy + edge_py * (1.0 - field.fvdy)) * rs;
    const double residual = hypot(sx - target_x, sy - target_y);
    if (!d_finite(residual) || residual > residual_limit) {
        return false;
    }
    anchor->seed = seed;
    anchor->stop_x = sx;
    anchor->stop_y = sy;
    anchor->correction_x = 0.0;
    anchor->correction_y = 0.0;
    anchor->valid = 1;
    if (max_residual) {
        *max_residual = fmax(*max_residual, residual);
    }
    return true;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __noinline__ DeviceEdgeStopSample d_probe_calibrated_edge_seed(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceField& field,
    double wavelength_um,
    const DeviceSeed& seed,
    double edge_px,
    double edge_py,
    double rs,
    bool ignore_clip,
    DeviceBuilderWork* builder_work = nullptr) {
    DeviceEdgeStopSample sample;
    if (!d_seed_in_first_surface_bounds(surfaces, cfg, wavelength_um, seed)) {
        return sample;
    }
    const double target_x = (field.fvcx + edge_px * (1.0 - field.fvdx)) * rs;
    const double target_y = (field.fvcy + edge_py * (1.0 - field.fvdy)) * rs;
    double sx = 0.0;
    double sy = 0.0;
    if (builder_work) ++builder_work->probes;
    if (!d_sample_stop_from_seed(
            surfaces,
            cfg,
            field,
            wavelength_um,
            seed,
            &sx,
            &sy,
            nullptr,
            ignore_clip,
            ignore_clip,
            kAsphereDeepRoleEdge)) {
        return sample;
    }
    const double residual = hypot(sx - target_x, sy - target_y);
    if (!d_finite(residual)) {
        return sample;
    }
    sample.valid = 1;
    sample.seed = seed;
    sample.sx = sx;
    sample.sy = sy;
    sample.residual = residual;
    return sample;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
// Kept as a separate noinline device routine so the production-asphere
// recovery specialization does not inflate the strict Round17 fast kernel.
__device__ __noinline__ bool d_solve_calibrated_edge_by_safeguarded_scale(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceField& field,
    double wavelength_um,
    DeviceCalibratedModel& model,
    double first_surface_edge_radius,
    bool enforce_edge_branch_continuity,
    double rs,
    double residual_limit,
    DeviceCalibratedEdgeAnchor* anchor,
    double requested_edge_px,
    double requested_edge_py,
    const DeviceSeed& full_seed,
    double* max_residual) {
    if (!anchor || !full_seed.valid || !model.chief_seed.valid || !max_residual) {
        return false;
    }
    const double target_x =
        (field.fvcx + requested_edge_px * (1.0 - field.fvdx)) * rs;
    const double target_y =
        (field.fvcy + requested_edge_py * (1.0 - field.fvdy)) * rs;
    const double target_dx = target_x - model.chief_stop_x;
    const double target_dy = target_y - model.chief_stop_y;
    const double target_dr = hypot(target_dx, target_dy);
    if (!d_finite(target_dr) || target_dr <= 1.0e-12) {
        return false;
    }

    struct ScaleStopSample {
        int valid = 0;
        double scale = 0.0;
        DeviceSeed seed;
        double sx = 0.0;
        double sy = 0.0;
        double projected_error = 0.0;
    };

    auto probe_scale = [&](double scale, ScaleStopSample* sample) {
        if (!sample || !d_finite(scale) || scale <= 0.0) {
            return 0;
        }
        const DeviceSeed seed =
            d_scaled_edge_seed(model.chief_seed, full_seed, scale);
        DeviceEdgeStopSample stop_sample = d_probe_calibrated_edge_seed(
            surfaces,
            cfg,
            field,
            wavelength_um,
            seed,
            requested_edge_px,
            requested_edge_py,
            rs,
            true);
        if (!stop_sample.valid) {
            return 0;
        }
        const double projected =
            ((stop_sample.sx - model.chief_stop_x) * target_dx +
             (stop_sample.sy - model.chief_stop_y) * target_dy) /
            target_dr;
        if (!d_finite(projected)) {
            return 0;
        }
        sample->valid = 1;
        sample->scale = scale;
        sample->seed = stop_sample.seed;
        sample->sx = stop_sample.sx;
        sample->sy = stop_sample.sy;
        sample->projected_error = projected - target_dr;
        if (d_accept_calibrated_edge_sample(
                surfaces,
                cfg,
                wavelength_um,
                anchor,
                field,
                stop_sample.seed,
                model.chief_seed,
                first_surface_edge_radius,
                requested_edge_px,
                requested_edge_py,
                enforce_edge_branch_continuity,
                rs,
                stop_sample.sx,
                stop_sample.sy,
                residual_limit,
                max_residual)) {
            return 2;
        }
        return 1;
    };

    auto refine_bracket = [&](ScaleStopSample left, ScaleStopSample right) {
        if (!left.valid || !right.valid ||
            left.projected_error * right.projected_error > 0.0) {
            return false;
        }
        constexpr int kBracketIterations = 32;
        for (int iteration = 0; iteration < kBracketIterations; ++iteration) {
            const double width = right.scale - left.scale;
            if (!d_finite(width) || width <= 1.0e-12) {
                break;
            }
            double scale = 0.5 * (left.scale + right.scale);
            const double denominator =
                right.projected_error - left.projected_error;
            if (d_finite(denominator) && fabs(denominator) > 1.0e-18) {
                const double secant =
                    (left.scale * right.projected_error -
                     right.scale * left.projected_error) /
                    denominator;
                const double guard = 0.1 * width;
                scale = d_clamp(
                    secant,
                    left.scale + guard,
                    right.scale - guard);
            }
            ScaleStopSample middle;
            int status = probe_scale(scale, &middle);
            if (status == 2) {
                return true;
            }
            if (status == 0) {
                scale = 0.5 * (left.scale + right.scale);
                status = probe_scale(scale, &middle);
                if (status == 2) {
                    return true;
                }
                if (status == 0) {
                    break;
                }
            }
            if (middle.projected_error == 0.0) {
                break;
            }
            if (left.projected_error * middle.projected_error <= 0.0) {
                right = middle;
            } else {
                left = middle;
            }
        }
        return false;
    };

    ScaleStopSample previous;
    previous.valid = 1;
    previous.scale = 0.0;
    previous.seed = model.chief_seed;
    previous.sx = model.chief_stop_x;
    previous.sy = model.chief_stop_y;
    previous.projected_error = -target_dr;
    bool have_previous = true;
    constexpr int kScaleSteps = 160;
    constexpr double kMaxScale = 1.25;
    for (int step = 1; step <= kScaleSteps; ++step) {
        const double scale =
            kMaxScale * static_cast<double>(step) /
            static_cast<double>(kScaleSteps);
        ScaleStopSample current;
        const int status = probe_scale(scale, &current);
        if (status == 2) {
            return true;
        }
        if (status == 0) {
            if (have_previous) {
                ScaleStopSample boundary = previous;
                double invalid_scale = scale;
                for (int iteration = 0; iteration < 20; ++iteration) {
                    const double boundary_scale =
                        0.5 * (boundary.scale + invalid_scale);
                    ScaleStopSample candidate;
                    const int boundary_status =
                        probe_scale(boundary_scale, &candidate);
                    if (boundary_status == 2) {
                        return true;
                    }
                    if (boundary_status == 0) {
                        invalid_scale = boundary_scale;
                        continue;
                    }
                    if (boundary.projected_error *
                            candidate.projected_error <=
                        0.0) {
                        if (refine_bracket(boundary, candidate)) {
                            return true;
                        }
                    }
                    boundary = candidate;
                }
            }
            have_previous = false;
            continue;
        }
        if (have_previous &&
            previous.projected_error * current.projected_error <= 0.0 &&
            refine_bracket(previous, current)) {
            return true;
        }
        previous = current;
        have_previous = true;
    }
    return false;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ int d_field_pupil_symmetry(
    const DeviceConfig& cfg,
    const DeviceField& field);
} // namespace
} // namespace optiland
