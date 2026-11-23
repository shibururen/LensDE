#pragma once
#include "../prelude.cuh"
#include "chief_geometry.cuh"
#include "chief_optical_context.cuh"
#include "chief_secant_solver.cuh"
#include "seed_candidates.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/calibration_tasks.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../diagnostics/builder_instrumentation.cuh"
#include "../geometry/asphere_slope.cuh"

namespace optiland {
namespace {
__device__ __noinline__ bool d_try_calibrated_budget_chief(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceField& field,
    double wavelength_um,
    double rs,
    const DeviceSeed* center_chief_seed,
    const DeviceField* center_field,
    const DeviceSeed* inner_chief_seed,
    double inner_field_rho,
    int field_index,
    const DeviceSeed* previous_chief_seed,
    const DeviceField* previous_field,
    const DeviceSeed* previous2_chief_seed,
    const DeviceField* previous2_field,
    const DeviceSeed* same_field_wave_chief_seed,
    int initial_trial_start,
    DeviceSeed* chief_seed,
    double* stop_x,
    double* stop_y,
    double* residual_out,
    DeviceBuilderDiagnostic* builder_diag = nullptr) {
    DeviceBuilderWork* builder_work = builder_diag && g_builder_diagnostic_level > 1 ? &builder_diag->chief_work : nullptr;
    const double field_angle_deg =
        hypot(field.hx, field.hy) * fmax(0.0, cfg.max_field_deg);
    const bool skip_abcd_seed =
        cfg.calibrated_budget_skip_abcd_angle_deg > 0.0 &&
        field_angle_deg > cfg.calibrated_budget_skip_abcd_angle_deg + 1.0e-9;
    bool singular = false;
    DeviceSeed abcd_seed;
    if (!skip_abcd_seed) {
        abcd_seed = d_estimate_abcd_chief_seed(
            surfaces,
            cfg,
            field,
            wavelength_um,
            rs,
            &singular);
    }
    const bool abcd_seed_invalid =
        !skip_abcd_seed && (singular || !abcd_seed.valid);
    const bool abcd_seed_unusable = skip_abcd_seed || abcd_seed_invalid;
    const bool abcd_seed_oob =
        !abcd_seed_unusable &&
        !d_seed_in_first_surface_bounds(
            surfaces,
            cfg,
            wavelength_um,
            abcd_seed);
    const bool abcd_seed_usable = !abcd_seed_unusable && !abcd_seed_oob;
    DeviceBudgetChiefSolveContext target_solve_context;
    bool target_solve_context_ready = false;
    auto try_target_seed = [&](const DeviceSeed& seed,
                               DeviceSeed* solved,
                               double* sx,
                               double* sy,
                               double* residual) {
        if (builder_diag) ++builder_diag->chief_trials;
        if (cfg.calibrated_budget_reuse_solve_context) {
            if (!target_solve_context_ready) {
                d_prepare_budget_chief_solve_context(
                    surfaces,
                    cfg,
                    field,
                    wavelength_um,
                    rs,
                    &target_solve_context);
                target_solve_context_ready = true;
            }
            return d_try_budget_chief_seed_precomputed(
                surfaces,
                cfg,
                field,
                wavelength_um,
                target_solve_context,
                seed,
                abcd_seed_unusable,
                solved,
                sx,
                sy,
                residual, builder_work);
        }
        return d_try_budget_chief_seed(
            surfaces,
            cfg,
            field,
            wavelength_um,
            rs,
            seed,
            abcd_seed_unusable,
            solved,
            sx,
            sy,
            residual, builder_work);
    };
    constexpr int kBudgetSeedCapacity = 64;
    DeviceSeed trial_seeds[kBudgetSeedCapacity];
    int trial_count = 0;
    auto append_trial = [&](DeviceSeed seed) {
        d_append_unique_clipped_seed(
            surfaces,
            cfg,
            wavelength_um,
            trial_seeds,
            &trial_count,
            kBudgetSeedCapacity,
            seed);
    };
    // Preserve the global replay index while consuming candidate families in
    // their original order. Unneeded later families perform no virtual solves.
    int initial_trial_cursor = max(0, initial_trial_start);
    auto try_initial_prefix = [&]() {
    for (; initial_trial_cursor < trial_count; ++initial_trial_cursor) {
        const int trial = initial_trial_cursor;
            DeviceSeed extracted;
            double sx = 0.0;
            double sy = 0.0;
            double residual = INFINITY;
            if (!try_target_seed(
                trial_seeds[trial],
                &extracted,
                &sx,
                &sy,
                &residual)) {
                continue;
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
        return false;
    };
    const double target_rho = hypot(field.hx, field.hy);
    const bool prefer_angle_bridge =
        cfg.calibrated_angle_bridge_fast &&
        (abcd_seed_unusable || abcd_seed_oob ||
         field_angle_deg > 35.0 + 1.0e-9);
    const bool prefer_field_continuation =
        cfg.calibrated_angle_bridge_fast &&
        (abcd_seed_unusable || abcd_seed_oob ||
         field_angle_deg >=
             cfg.calibrated_budget_field_continuation_angle_deg);
    if ((abcd_seed_unusable || abcd_seed_oob || prefer_angle_bridge ||
         prefer_field_continuation) &&
        same_field_wave_chief_seed && same_field_wave_chief_seed->valid) {
        append_trial(*same_field_wave_chief_seed);
    }
    if (try_initial_prefix()) return true;
    if (prefer_angle_bridge && center_chief_seed && center_chief_seed->valid) {
        DeviceSeed base_seed = *center_chief_seed;
        double base_rho = center_field
                              ? hypot(center_field->hx, center_field->hy)
                              : 0.0;
        if (inner_chief_seed && inner_chief_seed->valid &&
            inner_field_rho > base_rho) {
            base_seed = *inner_chief_seed;
            base_rho = inner_field_rho;
        }
        append_trial(base_seed);
        if (base_rho > 1.0e-12 && target_rho > base_rho + 1.0e-9) {
            DeviceSeed scaled = base_seed;
            const double scale = target_rho / base_rho;
            scaled.x *= scale;
            scaled.y *= scale;
            append_trial(scaled);
            DeviceSeed midpoint;
            midpoint.x = 0.5 * (base_seed.x + scaled.x);
            midpoint.y = 0.5 * (base_seed.y + scaled.y);
            midpoint.valid = 1;
            append_trial(midpoint);
        } else if (abcd_seed_usable) {
            append_trial(abcd_seed);
            DeviceSeed midpoint;
            midpoint.x = 0.5 * (base_seed.x + abcd_seed.x);
            midpoint.y = 0.5 * (base_seed.y + abcd_seed.y);
            midpoint.valid = 1;
            append_trial(midpoint);
        }
    }
    if (try_initial_prefix()) return true;
    if (prefer_field_continuation) {
        const DeviceField& radius_center_field = center_field ? *center_field : field;
        const double center_radius = d_center_meridional_radius(
            surfaces,
            cfg,
            radius_center_field,
            wavelength_um);
        d_append_field_continuation_seeds(
            surfaces,
            cfg,
            field,
            wavelength_um,
            field_index,
            trial_seeds,
            &trial_count,
            kBudgetSeedCapacity,
            center_radius,
            previous_chief_seed,
            previous_field,
            previous2_chief_seed,
            previous2_field);
    }
    if (try_initial_prefix()) return true;
    if (abcd_seed_usable) {
        const DeviceField& radius_center_field = center_field ? *center_field : field;
        const double center_radius = d_center_meridional_radius(
            surfaces,
            cfg,
            radius_center_field,
            wavelength_um);
        const double inward_factors[15] = {
            0.0, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75,
            2.0, 2.25, 2.5, 3.0, 3.5, 4.0, 4.5, 5.0};
        for (int i = 0; i < 15; ++i) {
            DeviceSeed inward =
                d_inward_seed(abcd_seed, inward_factors[i] * center_radius);
            if (d_seed_in_first_surface_bounds(
                    surfaces,
                    cfg,
                    wavelength_um,
                    inward)) {
                append_trial(inward);
            }
        }
    }
    if (try_initial_prefix()) return true;
    if (prefer_angle_bridge && center_chief_seed && center_chief_seed->valid &&
        center_field && target_rho > 1.0e-9) {
        DeviceSeed base_seed = *center_chief_seed;
        double base_rho = hypot(center_field->hx, center_field->hy);
        if (inner_chief_seed && inner_chief_seed->valid &&
            inner_field_rho > base_rho) {
            base_seed = *inner_chief_seed;
            base_rho = inner_field_rho;
        }
        const bool has_inner =
            inner_chief_seed && inner_chief_seed->valid &&
            inner_field_rho > hypot(center_field->hx, center_field->hy);
        const double center_radius = d_center_meridional_radius(
            surfaces,
            cfg,
            *center_field,
            wavelength_um);
        bool built_bridge = false;
        const double bridge_angles[3] = {35.0, 60.0, 80.0};
        for (int bridge_index = 0; bridge_index < 3; ++bridge_index) {
            const double bridge_angle = bridge_angles[bridge_index];
            if (!(bridge_angle > base_rho * cfg.max_field_deg + 1.0e-9) ||
                !(bridge_angle < field_angle_deg - 1.0e-9)) {
                continue;
            }
            const double bridge_rho = d_clamp(
                bridge_angle / fmax(1.0e-12, cfg.max_field_deg),
                0.0,
                target_rho);
            const double center_rho = hypot(center_field->hx, center_field->hy);
            const double t = d_clamp(
                (bridge_rho - center_rho) /
                    fmax(1.0e-12, target_rho - center_rho),
                0.0,
                1.0);
            DeviceField bridge_field = *center_field;
            bridge_field.hx = center_field->hx + t * (field.hx - center_field->hx);
            bridge_field.hy = center_field->hy + t * (field.hy - center_field->hy);
            bridge_field.fvcx =
                center_field->fvcx + t * (field.fvcx - center_field->fvcx);
            bridge_field.fvcy =
                center_field->fvcy + t * (field.fvcy - center_field->fvcy);
            bridge_field.fvdx =
                center_field->fvdx + t * (field.fvdx - center_field->fvdx);
            bridge_field.fvdy =
                center_field->fvdy + t * (field.fvdy - center_field->fvdy);
            DeviceSeed bridge_trials[kBudgetSeedCapacity];
            int bridge_trial_count = 0;
            bool bridge_singular = false;
            DeviceSeed bridge_abcd;
            bool bridge_abcd_valid = false;
            if (cfg.calibrated_budget_skip_abcd_angle_deg <= 0.0 ||
                bridge_angle <=
                    cfg.calibrated_budget_skip_abcd_angle_deg + 1.0e-9) {
                bridge_abcd = d_estimate_abcd_chief_seed(
                    surfaces,
                    cfg,
                    bridge_field,
                    wavelength_um,
                    rs,
                    &bridge_singular);
                bridge_abcd_valid = !bridge_singular && bridge_abcd.valid;
            }
            const bool first_explicit_bridge = !built_bridge && !has_inner;
            if (first_explicit_bridge) {
                d_append_unique_clipped_seed(
                    surfaces,
                    cfg,
                    wavelength_um,
                    bridge_trials,
                    &bridge_trial_count,
                    kBudgetSeedCapacity,
                    bridge_abcd_valid ? bridge_abcd : base_seed);
            } else {
                DeviceSeed scaled_guess;
                if (abcd_seed.valid && target_rho > 1.0e-12) {
                    scaled_guess = abcd_seed;
                    const double scale = bridge_rho / target_rho;
                    scaled_guess.x *= scale;
                    scaled_guess.y *= scale;
                }
                const DeviceSeed& first_origin =
                    bridge_abcd_valid ? bridge_abcd : base_seed;
                const DeviceSeed* first_toward =
                    bridge_abcd_valid
                        ? &base_seed
                        : (scaled_guess.valid ? &scaled_guess : nullptr);
                d_append_local_budget_seed_family(
                    surfaces,
                    cfg,
                    wavelength_um,
                    bridge_trials,
                    &bridge_trial_count,
                    kBudgetSeedCapacity,
                    center_radius,
                    first_origin,
                    first_toward);
                if (bridge_abcd_valid) {
                    d_append_local_budget_seed_family(
                        surfaces,
                        cfg,
                        wavelength_um,
                        bridge_trials,
                        &bridge_trial_count,
                        kBudgetSeedCapacity,
                        center_radius,
                        base_seed,
                        &bridge_abcd);
                    DeviceSeed midpoint;
                    midpoint.x = 0.5 * (base_seed.x + bridge_abcd.x);
                    midpoint.y = 0.5 * (base_seed.y + bridge_abcd.y);
                    midpoint.valid = 1;
                    d_append_unique_clipped_seed(
                        surfaces,
                        cfg,
                        wavelength_um,
                        bridge_trials,
                        &bridge_trial_count,
                        kBudgetSeedCapacity,
                        midpoint);
                } else {
                    d_append_local_budget_seed_family(
                        surfaces,
                        cfg,
                        wavelength_um,
                        bridge_trials,
                        &bridge_trial_count,
                        kBudgetSeedCapacity,
                        center_radius,
                        base_seed,
                        scaled_guess.valid ? &scaled_guess : nullptr);
                }
                if (scaled_guess.valid) {
                    d_append_local_budget_seed_family(
                        surfaces,
                        cfg,
                        wavelength_um,
                        bridge_trials,
                        &bridge_trial_count,
                        kBudgetSeedCapacity,
                        center_radius,
                        scaled_guess,
                        &base_seed);
                    DeviceSeed midpoint;
                    midpoint.x = 0.5 * (base_seed.x + scaled_guess.x);
                    midpoint.y = 0.5 * (base_seed.y + scaled_guess.y);
                    midpoint.valid = 1;
                    d_append_unique_clipped_seed(
                        surfaces,
                        cfg,
                        wavelength_um,
                        bridge_trials,
                        &bridge_trial_count,
                        kBudgetSeedCapacity,
                        midpoint);
                }
            }
            bool bridge_ok = false;
            DeviceBudgetChiefSolveContext bridge_solve_context;
            if (cfg.calibrated_budget_reuse_solve_context &&
                bridge_trial_count > 0) {
                d_prepare_budget_chief_solve_context(
                    surfaces,
                    cfg,
                    bridge_field,
                    wavelength_um,
                    rs,
                    &bridge_solve_context);
            }
            for (int i = 0; i < bridge_trial_count; ++i) {
                DeviceSeed solved;
                if (builder_diag) ++builder_diag->chief_trials;
                const bool solved_ok =
                    cfg.calibrated_budget_reuse_solve_context
                        ? d_try_budget_chief_seed_precomputed(
                              surfaces,
                              cfg,
                              bridge_field,
                              wavelength_um,
                              bridge_solve_context,
                              bridge_trials[i],
                              abcd_seed_unusable,
                              &solved,
                              nullptr,
                              nullptr,
                              nullptr, builder_work)
                        : d_try_budget_chief_seed(
                              surfaces,
                              cfg,
                              bridge_field,
                              wavelength_um,
                              rs,
                              bridge_trials[i],
                              abcd_seed_unusable,
                              &solved,
                              nullptr,
                              nullptr,
                              nullptr, builder_work);
                if (solved_ok) {
                    base_seed = solved;
                    base_rho = bridge_rho;
                    bridge_ok = true;
                    built_bridge = true;
                    break;
                }
            }
            if (!bridge_ok) {
                break;
            }
        }
        if (built_bridge || has_inner) {
            const int retry_begin = trial_count;
            append_trial(base_seed);
            if (base_rho > 1.0e-12 && target_rho > base_rho + 1.0e-9) {
                DeviceSeed scaled = base_seed;
                const double scale = target_rho / base_rho;
                scaled.x *= scale;
                scaled.y *= scale;
                if (scaled.valid) {
                    append_trial(scaled);
                    d_append_local_budget_seed_family(
                        surfaces,
                        cfg,
                        wavelength_um,
                        trial_seeds,
                        &trial_count,
                        kBudgetSeedCapacity,
                        center_radius,
                        scaled,
                        &base_seed);
                    DeviceSeed midpoint;
                    midpoint.x = 0.5 * (base_seed.x + scaled.x);
                    midpoint.y = 0.5 * (base_seed.y + scaled.y);
                    midpoint.valid = 1;
                    append_trial(midpoint);
                }
            }
            for (int i = retry_begin; i < trial_count; ++i) {
                DeviceSeed solved;
                double sx = 0.0;
                double sy = 0.0;
                double residual = INFINITY;
                if (!try_target_seed(
                        trial_seeds[i],
                        &solved,
                        &sx,
                        &sy,
                        &residual)) {
                    continue;
                }
                if (chief_seed) {
                    *chief_seed = solved;
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
        }
    }
    return false;
}
} // namespace
} // namespace optiland
