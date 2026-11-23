#pragma once
#include "../prelude.cuh"
#include "chief_geometry.cuh"
#include "chief_optical_context.cuh"
#include "chief_secant_solver.cuh"
#include "seed_candidates.cuh"
#include "stop_samples.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/calibration_tasks.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../contracts/ray_record.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/first_order_metrics.cuh"
#include "../geometry/pupil_paraxial.cuh"
#include "../ray/paraxial_generation.cuh"
#include "../ray/stop_trace.cuh"

namespace optiland {
namespace {
__device__ __noinline__ void d_prepare_ordered_chief_task(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceField& field,
    double wavelength_um,
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
    int seed_capacity,
    DeviceOrderedChiefTask* output) {
    if (!output) {
        return;
    }
    seed_capacity = max(1, min(seed_capacity, kBudgetChiefSeedCapacity));
    if (seed_capacity == kBudgetChiefSeedCapacity) {
        *output = DeviceOrderedChiefTask{};
    } else {
        output->solve_context = DeviceBudgetChiefSolveContext{};
        output->chief_seed = DeviceSeed{};
        output->stop_x = 0.0;
        output->stop_y = 0.0;
        output->residual = INFINITY;
        output->trial_count = 0;
        output->require_convergence = 0;
        output->chief_valid = 0;
        output->cooperative_exhausted = 0;
    }
    if (!cfg.calibrated_enabled) {
        return;
    }
    const double rs = d_aperture_stop_radius(surfaces, cfg, wavelength_um);
    const double stop_scale = fmax(1.0e-9, fabs(rs));
    const double target_x = field.fvcx * rs;
    const double target_y = field.fvcy * rs;
    DeviceRay paraxial_chief =
        d_generate_paraxial_ray(
            surfaces,
            cfg,
            field,
            0.0,
            0.0,
            1.0,
            wavelength_um);
    double sx = 0.0;
    double sy = 0.0;
    bool chief_ok = d_trace_ray_to_surface(
        surfaces,
        cfg,
        paraxial_chief,
        d_stop_index(surfaces, cfg),
        &sx,
        &sy,
        false,
        false,
        kAsphereDeepRoleChief,
        kAsphereDeepSiteFarParaxial);
    double paraxial_residual = INFINITY;
    if (chief_ok) {
        paraxial_residual = hypot(sx - target_x, sy - target_y);
        const double limit = fmax(
            cfg.ray_aiming_tolerance,
            fmax(0.0, cfg.calibrated_validation_residual_stop_fraction) *
                stop_scale);
        chief_ok = d_finite(paraxial_residual) &&
                   paraxial_residual <= limit;
    }
    if (chief_ok) {
        const DeviceSeed seed =
            d_seed_from_ray(
                surfaces,
                cfg,
                paraxial_chief,
                kAsphereDeepRoleChief,
                kAsphereDeepSiteFarParaxial);
        if (seed.valid) {
            output->chief_seed = seed;
            output->stop_x = sx;
            output->stop_y = sy;
            output->residual = paraxial_residual;
            output->chief_valid = 1;
            return;
        }
    }

    DeviceBudgetChiefOpticalBaseContext ordered_optical_context;
    if (cfg.calibrated_ordered_reuse_solve_context) {
        d_prepare_budget_chief_optical_base_context(
            surfaces,
            cfg,
            wavelength_um,
            rs,
            &ordered_optical_context);
        d_prepare_budget_chief_solve_context_precomputed(
            ordered_optical_context,
            field,
            rs,
            &output->solve_context);
    }
    const double field_angle_deg =
        hypot(field.hx, field.hy) * fmax(0.0, cfg.max_field_deg);
    const bool skip_abcd_seed =
        cfg.calibrated_budget_skip_abcd_angle_deg > 0.0 &&
        field_angle_deg >
            cfg.calibrated_budget_skip_abcd_angle_deg + 1.0e-9;
    bool singular = false;
    DeviceSeed abcd_seed;
    if (!skip_abcd_seed) {
        abcd_seed = cfg.calibrated_ordered_reuse_solve_context
                        ? d_estimate_abcd_chief_seed_precomputed(
                              cfg,
                              field,
                              rs,
                              ordered_optical_context,
                              &singular)
                        : d_estimate_abcd_chief_seed(
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
    output->require_convergence = abcd_seed_unusable ? 1 : 0;
    int& trial_count = output->trial_count;
    auto append_trial = [&](DeviceSeed seed) {
        return d_append_unique_clipped_seed(
            surfaces,
            cfg,
            wavelength_um,
            output->trial_seeds,
            &trial_count,
            seed_capacity,
            seed);
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
    if (trial_count >= seed_capacity) {
        return;
    }
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
    if (trial_count >= seed_capacity) {
        return;
    }
    if (prefer_field_continuation) {
        const DeviceField& radius_center_field =
            center_field ? *center_field : field;
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
            output->trial_seeds,
            &trial_count,
            seed_capacity,
            center_radius,
            previous_chief_seed,
            previous_field,
            previous2_chief_seed,
            previous2_field);
    }
    if (trial_count >= seed_capacity) {
        return;
    }
    if (abcd_seed_usable) {
        const DeviceField& radius_center_field =
            center_field ? *center_field : field;
        const double center_radius = d_center_meridional_radius(
            surfaces,
            cfg,
            radius_center_field,
            wavelength_um);
        const double inward_factors[15] = {
            0.0, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75,
            2.0, 2.25, 2.5, 3.0, 3.5, 4.0, 4.5, 5.0};
        for (int i = 0; i < 15; ++i) {
            const DeviceSeed inward =
                d_inward_seed(abcd_seed, inward_factors[i] * center_radius);
            if (d_seed_in_first_surface_bounds(
                    surfaces,
                    cfg,
                    wavelength_um,
                    inward)) {
                append_trial(inward);
                if (trial_count >= seed_capacity) {
                    return;
                }
            }
        }
    }
}
} // namespace
} // namespace optiland
