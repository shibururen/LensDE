#pragma once
#include "../prelude.cuh"
#include "chief_geometry.cuh"
#include "chief_optical_context.cuh"
#include "seed_candidates.cuh"
#include "stop_samples.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/calibration_tasks.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../contracts/ray_record.cuh"
#include "../diagnostics/asphere_profile_records.cuh"
#include "../diagnostics/builder_instrumentation.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../ray/stop_trace.cuh"

namespace optiland {
namespace {
template <bool kTrackRay>
__device__ __noinline__ bool d_solve_seed_to_stop_secant(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceField& field,
    double wavelength_um,
    DeviceSeed start_seed,
    double target_x,
    double target_y,
    double residual_limit,
    int iterations,
    double h11,
    double h12,
    double h21,
    double h22,
    bool ignore_clip,
    DeviceSeed* out_seed,
    double* out_sx,
    double* out_sy,
    double* out_residual,
    DeviceRay* out_ray,
    const DeviceEdgeStopSample* initial_sample = nullptr,
    const DeviceEdgeStopSample* previous_sample = nullptr,
    bool probe_final_seed = false,
    int failure_backtracks = 0,
    int deep_profile_role = kAsphereDeepRoleAuxiliary,
    double step_cap = INFINITY,
    DeviceBuilderWork* builder_work = nullptr) {
    if (builder_work) ++builder_work->calls;
    const int uc5_counter_cell = (deep_profile_role >> 8) - 1;
    const int semantic_deep_profile_role = deep_profile_role & 0xff;
    DeviceSeed seed = start_seed;
    DeviceSeed previous = seed;
    double previous_ex = 0.0;
    double previous_ey = 0.0;
    bool have_previous = false;
    bool use_initial_sample =
        initial_sample &&
        initial_sample->valid &&
        initial_sample->seed.valid &&
        initial_sample->seed.x == seed.x &&
        initial_sample->seed.y == seed.y &&
        d_finite(initial_sample->sx) &&
        d_finite(initial_sample->sy);
    if (previous_sample &&
        previous_sample->valid &&
        previous_sample->seed.valid &&
        d_finite(previous_sample->sx) &&
        d_finite(previous_sample->sy) &&
        hypot(previous_sample->seed.x - seed.x,
              previous_sample->seed.y - seed.y) > 1.0e-12) {
        previous = previous_sample->seed;
        previous_ex = previous_sample->sx - target_x;
        previous_ey = previous_sample->sy - target_y;
        have_previous = d_finite(previous_ex) && d_finite(previous_ey);
    }
    [[maybe_unused]] DeviceRay last_ray;
    DeviceSeed last_sampled_seed;
    bool have_last = false;
    double last_sx = NAN;
    double last_sy = NAN;
    double residual = INFINITY;
    const int max_iterations = max(1, iterations);
    for (int iter = 0; iter < max_iterations; ++iter) {
        if (builder_work) ++builder_work->iterations;
        DeviceAsphereUC5Profile* uc5_profile = g_asphere_uc5_profile;
        if (uc5_profile && uc5_counter_cell >= 0 &&
            uc5_counter_cell < kUC5CounterCells) {
            atomicAdd(
                &uc5_profile->secant_iterations[uc5_counter_cell], 1ULL);
        }
        double sx = 0.0;
        double sy = 0.0;
        [[maybe_unused]] DeviceRay ray;
        if (use_initial_sample) {
            sx = initial_sample->sx;
            sy = initial_sample->sy;
                if constexpr (kTrackRay) {
                ray = d_generate_first_surface_point_ray(
                    surfaces,
                    cfg,
                    field,
                    wavelength_um,
                    seed.x,
                    seed.y,
                    1.0);
            }
            use_initial_sample = false;
        } else {
            auto sample_candidate = [&](const DeviceSeed& candidate,
                                        double* candidate_sx,
                                        double* candidate_sy,
                                        DeviceRay* candidate_ray) {
                if (!d_seed_in_first_surface_bounds(
                        surfaces, cfg, wavelength_um, candidate)) {
                    return false;
                }
                if (builder_work) ++builder_work->probes;
                if constexpr (kTrackRay) {
                    return d_sample_stop_from_seed(
                        surfaces,
                        cfg,
                        field,
                        wavelength_um,
                        candidate,
                        candidate_sx,
                        candidate_sy,
                        candidate_ray,
                        ignore_clip,
                        ignore_clip,
                        semantic_deep_profile_role);
                } else {
                    return d_sample_stop_from_seed(
                        surfaces,
                        cfg,
                        field,
                        wavelength_um,
                        candidate,
                        candidate_sx,
                        candidate_sy,
                        nullptr,
                        ignore_clip,
                        ignore_clip,
                        semantic_deep_profile_role);
                }
                return false;
            };
            bool sampled = sample_candidate(seed, &sx, &sy, &ray);
            if (!sampled && have_previous && failure_backtracks > 0) {
                const DeviceSeed rejected_seed = seed;
                double fraction = 0.5;
                for (int trial = 0;
                     trial < failure_backtracks && !sampled;
                     ++trial, fraction *= 0.5) {
                    if (uc5_profile && uc5_counter_cell >= 0 &&
                        uc5_counter_cell < kUC5CounterCells) {
                        atomicAdd(
                            &uc5_profile
                                 ->secant_backtracks[uc5_counter_cell],
                            1ULL);
                    }
                    if (builder_work) ++builder_work->backtracks;
                    DeviceSeed backtracked;
                    backtracked.x = previous.x +
                                    fraction * (rejected_seed.x - previous.x);
                    backtracked.y = previous.y +
                                    fraction * (rejected_seed.y - previous.y);
                    backtracked.valid =
                        d_finite(backtracked.x) && d_finite(backtracked.y);
                    if (sample_candidate(backtracked, &sx, &sy, &ray)) {
                        seed = backtracked;
                        sampled = true;
                    }
                }
            }
            if (!sampled) {
                if (builder_work) {
                    ++builder_work->failed_iterations;
                    if (probe_final_seed && have_last) ++builder_work->duplicate_final;
                }
                // The final probe has identical seed, bounds and trace policy.
                if ((g_edge_path_mode & 1) && probe_final_seed) return false;
                break;
            }
        }
        have_last = true;
        if constexpr (kTrackRay) {
            last_ray = ray;
        }
        last_sampled_seed = seed;
        last_sx = sx;
        last_sy = sy;
        const double ex = sx - target_x;
        const double ey = sy - target_y;
        residual = hypot(ex, ey);
        if (d_finite(residual) && residual <= residual_limit) {
            break;
        }
        if (have_previous) {
            const double dx = seed.x - previous.x;
            const double dy = seed.y - previous.y;
            const double yx = ex - previous_ex;
            const double yy = ey - previous_ey;
            const double hyx = h11 * yx + h12 * yy;
            const double hyy = h21 * yx + h22 * yy;
            const double ux = dx - hyx;
            const double uy = dy - hyy;
            const double denom = yx * yx + yy * yy;
            if (d_finite(denom) && denom > 1.0e-24) {
                const double scale = 1.0 / denom;
                h11 += ux * yx * scale;
                h12 += ux * yy * scale;
                h21 += uy * yx * scale;
                h22 += uy * yy * scale;
            }
        }
        previous = seed;
        previous_ex = ex;
        previous_ey = ey;
        have_previous = true;
        const double step_x = h11 * ex + h12 * ey;
        const double step_y = h21 * ex + h22 * ey;
        if (!d_finite(step_x) || !d_finite(step_y)) {
            break;
        }
        const double bounded_step_x = d_finite(step_cap)
                                          ? d_clamp(step_x, -step_cap, step_cap)
                                          : step_x;
        const double bounded_step_y = d_finite(step_cap)
                                          ? d_clamp(step_y, -step_cap, step_cap)
                                          : step_y;
        if (builder_work && builder_work->calls == 1 && iter == 0) {
            builder_work->first_step_x = bounded_step_x;
            builder_work->first_step_y = bounded_step_y;
            builder_work->first_ex = ex;
            builder_work->first_ey = ey;
        }
        seed.x -= bounded_step_x;
        seed.y -= bounded_step_y;
    }
    if (probe_final_seed && have_last &&
        (seed.valid != last_sampled_seed.valid ||
         seed.x != last_sampled_seed.x || seed.y != last_sampled_seed.y)) {
        if (builder_work) ++builder_work->probes;
        double final_sx = 0.0;
        double final_sy = 0.0;
        [[maybe_unused]] DeviceRay final_ray;
        if constexpr (kTrackRay) {
            if (!d_sample_stop_from_seed(
                    surfaces,
                    cfg,
                    field,
                    wavelength_um,
                    seed,
                    &final_sx,
                    &final_sy,
                    &final_ray,
                    ignore_clip,
                    ignore_clip,
                    semantic_deep_profile_role)) {
                return false;
            }
            last_ray = final_ray;
        } else {
            if (!d_sample_stop_from_seed(
                    surfaces,
                    cfg,
                    field,
                    wavelength_um,
                    seed,
                    &final_sx,
                    &final_sy,
                    nullptr,
                    ignore_clip,
                    ignore_clip,
                    semantic_deep_profile_role)) {
                return false;
            }
        }
        last_sampled_seed = seed;
        last_sx = final_sx;
        last_sy = final_sy;
        residual = hypot(final_sx - target_x, final_sy - target_y);
    }
    if (!have_last || !d_finite(residual)) {
        return false;
    }
    if (out_seed) {
        if constexpr (kTrackRay) {
            *out_seed = seed;
            if (!d_seed_in_first_surface_bounds(
                    surfaces, cfg, wavelength_um, *out_seed)) {
                *out_seed = d_seed_from_ray(
                    surfaces, cfg, last_ray, semantic_deep_profile_role);
            }
        } else {
            *out_seed = last_sampled_seed;
        }
    }
    if (out_sx) {
        *out_sx = last_sx;
    }
    if (out_sy) {
        *out_sy = last_sy;
    }
    if (out_residual) {
        *out_residual = residual;
    }
    if constexpr (kTrackRay) {
        if (out_ray) {
            *out_ray = last_ray;
        }
    }
    return residual <= residual_limit;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __forceinline__ bool
d_prepare_budget_chief_solve_context_precomputed(
    const DeviceBudgetChiefOpticalBaseContext& optical_context,
    const DeviceField& field,
    double rs,
    DeviceBudgetChiefSolveContext* context) {
    if (!context) {
        return false;
    }
    *context = DeviceBudgetChiefSolveContext{};
    if (!optical_context.valid) {
        return false;
    }
    context->inverse_a = optical_context.inverse_a;
    context->target_x = field.fvcx * rs;
    context->target_y = field.fvcy * rs;
    context->physical_stop_radius = optical_context.physical_stop_radius;
    context->convergence_limit = optical_context.convergence_limit;
    context->accept_limit = optical_context.accept_limit;
    context->valid = 1;
    return true;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __forceinline__ bool d_prepare_budget_chief_solve_context(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceField& field,
    double wavelength_um,
    double rs,
    DeviceBudgetChiefSolveContext* context) {
    if (!context) {
        return false;
    }
    *context = DeviceBudgetChiefSolveContext{};
    double a = 0.0;
    double b = 0.0;
    if (!d_first_surface_to_stop_abcd(surfaces, cfg, wavelength_um, &a, &b) ||
        !d_finite(a) ||
        fabs(a) < fmax(1.0e-12, cfg.calibrated_budget_min_a)) {
        return false;
    }
    context->inverse_a = 1.0 / a;
    context->target_x = field.fvcx * rs;
    context->target_y = field.fvcy * rs;
    context->physical_stop_radius =
        d_physical_stop_radius(surfaces, cfg, rs);
    context->convergence_limit = fmax(
        fmax(0.0, cfg.ray_aiming_tolerance),
        fmax(0.0, cfg.calibrated_budget_residual_stop_fraction) *
            context->physical_stop_radius);
    context->accept_limit = fmax(
        cfg.ray_aiming_tolerance,
        fmax(0.0, cfg.calibrated_budget_accept_stop_fraction) *
            fmax(1.0e-9, fabs(rs)));
    context->valid = 1;
    return true;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
template <bool kTrackRay = true>
__device__ __noinline__ bool d_try_budget_chief_seed_precomputed(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceField& field,
    double wavelength_um,
    const DeviceBudgetChiefSolveContext& context,
    const DeviceSeed& start_seed,
    bool require_convergence,
    DeviceSeed* chief_seed,
    double* stop_x,
    double* stop_y,
    double* residual_out,
    DeviceBuilderWork* builder_work = nullptr) {
    if (!context.valid ||
        !d_seed_in_first_surface_bounds(
            surfaces,
            cfg,
            wavelength_um,
            start_seed)) {
        return false;
    }
    DeviceSeed solved_seed;
    double sx = 0.0;
    double sy = 0.0;
    double residual = INFINITY;
    [[maybe_unused]] DeviceRay ray;
    const bool converged = d_solve_seed_to_stop_secant<kTrackRay>(
        surfaces,
        cfg,
        field,
        wavelength_um,
        start_seed,
        context.target_x,
        context.target_y,
        context.convergence_limit,
        cfg.calibrated_budget_iterations,
        context.inverse_a,
        0.0,
        0.0,
        context.inverse_a,
        cfg.calibrated_budget_ignore_aperture_clip != 0,
        &solved_seed,
        &sx,
        &sy,
        &residual,
        kTrackRay ? &ray : nullptr,
        nullptr,
        nullptr,
        false,
        0,
        kAsphereDeepRoleChief,
        INFINITY,
        builder_work);
    if (!converged) {
        const bool inside_physical_stop =
            d_finite(sx) && d_finite(sy) &&
            hypot(sx, sy) <=
                context.physical_stop_radius * (1.0 + 1.0e-10);
        const bool residual_accepted =
            d_finite(residual) && residual <= context.accept_limit;
        if (require_convergence || !inside_physical_stop ||
            (!residual_accepted && !cfg.calibrated_budget_allow_inside_stop)) {
            return false;
        }
    }
    DeviceSeed extracted = solved_seed;
    if constexpr (kTrackRay) {
        extracted = d_seed_from_ray(
            surfaces,
            cfg,
            ray,
            kAsphereDeepRoleChief,
            kAsphereDeepSiteExactFirstSurfaceSeed);
        if (!extracted.valid) {
            extracted = solved_seed;
        }
    }
    if (!d_seed_in_first_surface_bounds(
            surfaces,
            cfg,
            wavelength_um,
            extracted)) {
        return false;
    }
    if (chief_seed) {
        *chief_seed = extracted;
    }
    if (stop_x) {
        *stop_x = sx;
    }
    if (stop_y) {
        *stop_y = sy;
    }
    if (residual_out) {
        *residual_out = residual;
    }
    return true;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
template <bool kTrackRay = true>
__device__ __forceinline__ bool d_try_budget_chief_seed(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceField& field,
    double wavelength_um,
    double rs,
    const DeviceSeed& start_seed,
    bool require_convergence,
    DeviceSeed* chief_seed,
    double* stop_x,
    double* stop_y,
    double* residual_out,
    DeviceBuilderWork* builder_work = nullptr) {
    DeviceBudgetChiefSolveContext context;
    if (!d_prepare_budget_chief_solve_context(
            surfaces,
            cfg,
            field,
            wavelength_um,
            rs,
            &context)) {
        return false;
    }
    return d_try_budget_chief_seed_precomputed<kTrackRay>(
        surfaces,
        cfg,
        field,
        wavelength_um,
        context,
        start_seed,
        require_convergence,
        chief_seed,
        stop_x,
        stop_y,
        residual_out,
        builder_work);
}
} // namespace
} // namespace optiland
