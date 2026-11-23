#pragma once
#include "../prelude.cuh"
#include "../aiming/chief_optical_context.cuh"
#include "../aiming/chief_secant_solver.cuh"
#include "../aiming/chromatic_anchor.cuh"
#include "../aiming/seed_candidates.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/calibration_tasks.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../geometry/asphere_slope.cuh"

namespace optiland {
namespace {
__device__ __forceinline__ void d_finish_cooperative_bridge_stage_failure(
    DeviceCooperativeBridgeTask* state,
    DeviceOrderedChiefStats* stats) {
    if (!state) {
        return;
    }
    if (state->built_bridge || state->has_inner) {
        state->status = kCooperativeTargetReady;
    } else {
        state->status = kCooperativeBridgeFailed;
        if (stats) {
            atomicAdd(&stats->cooperative_failed, 1ull);
        }
    }
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void prepare_cooperative_bridge_stage_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    DeviceConfig cfg,
    int population_size,
    int center_field_index,
    int field_index,
    int wave_index,
    DeviceCooperativeBridgeTask* bridge_tasks,
    DeviceOrderedChiefStats* stats) {
    const int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= population_size) {
        return;
    }
    fields = d_candidate_row_fields(fields, cfg, row);
    DeviceCooperativeBridgeTask& state = bridge_tasks[row];
    if (state.status != kCooperativeBridgeReady) {
        return;
    }
    const double bridge_angles[3] = {35.0, 60.0, 80.0};
    int bridge_index = state.next_bridge_index;
    for (; bridge_index < 3; ++bridge_index) {
        const double bridge_angle = bridge_angles[bridge_index];
        state.next_bridge_index = bridge_index + 1;
        if (!(bridge_angle >
                  state.base_rho * cfg.max_field_deg + 1.0e-9) ||
            !(bridge_angle <
                  state.target_rho * cfg.max_field_deg - 1.0e-9)) {
            continue;
        }
        const DeviceSurface* surfaces =
            candidate_surfaces +
            static_cast<long long>(row) * cfg.surface_count;
        const double wavelength_um = waves[wave_index].value_um;
        const DeviceField& field = fields[field_index];
        const DeviceField& center_field = fields[center_field_index];
        const double bridge_rho = d_clamp(
            bridge_angle / fmax(1.0e-12, cfg.max_field_deg),
            0.0,
            state.target_rho);
        const double center_rho =
            hypot(center_field.hx, center_field.hy);
        const double t = d_clamp(
            (bridge_rho - center_rho) /
                fmax(1.0e-12, state.target_rho - center_rho),
            0.0,
            1.0);
        DeviceField bridge_field = center_field;
        bridge_field.hx = center_field.hx + t * (field.hx - center_field.hx);
        bridge_field.hy = center_field.hy + t * (field.hy - center_field.hy);
        bridge_field.fvcx =
            center_field.fvcx + t * (field.fvcx - center_field.fvcx);
        bridge_field.fvcy =
            center_field.fvcy + t * (field.fvcy - center_field.fvcy);
        bridge_field.fvdx =
            center_field.fvdx + t * (field.fvdx - center_field.fvdx);
        bridge_field.fvdy =
            center_field.fvdy + t * (field.fvdy - center_field.fvdy);
        state.solve_field = bridge_field;
        state.candidate_rho = bridge_rho;
        state.trial_begin = 0;
        state.trial_count = 0;
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
                state.stop_radius,
                &bridge_singular);
            bridge_abcd_valid = !bridge_singular && bridge_abcd.valid;
        }
        const bool first_explicit_bridge =
            !state.built_bridge && !state.has_inner;
        if (first_explicit_bridge) {
            d_append_unique_clipped_seed(
                surfaces,
                cfg,
                wavelength_um,
                state.trial_seeds,
                &state.trial_count,
                kBudgetChiefSeedCapacity,
                bridge_abcd_valid ? bridge_abcd : state.base_seed);
        } else {
            DeviceSeed scaled_guess;
            if (state.target_abcd_seed.valid && state.target_rho > 1.0e-12) {
                scaled_guess = state.target_abcd_seed;
                const double scale = bridge_rho / state.target_rho;
                scaled_guess.x *= scale;
                scaled_guess.y *= scale;
            }
            const DeviceSeed& first_origin =
                bridge_abcd_valid ? bridge_abcd : state.base_seed;
            const DeviceSeed* first_toward =
                bridge_abcd_valid
                    ? &state.base_seed
                    : (scaled_guess.valid ? &scaled_guess : nullptr);
            d_append_local_budget_seed_family(
                surfaces,
                cfg,
                wavelength_um,
                state.trial_seeds,
                &state.trial_count,
                kBudgetChiefSeedCapacity,
                state.center_radius,
                first_origin,
                first_toward);
            if (bridge_abcd_valid) {
                d_append_local_budget_seed_family(
                    surfaces,
                    cfg,
                    wavelength_um,
                    state.trial_seeds,
                    &state.trial_count,
                    kBudgetChiefSeedCapacity,
                    state.center_radius,
                    state.base_seed,
                    &bridge_abcd);
                DeviceSeed midpoint;
                midpoint.x = 0.5 * (state.base_seed.x + bridge_abcd.x);
                midpoint.y = 0.5 * (state.base_seed.y + bridge_abcd.y);
                midpoint.valid = 1;
                d_append_unique_clipped_seed(
                    surfaces,
                    cfg,
                    wavelength_um,
                    state.trial_seeds,
                    &state.trial_count,
                    kBudgetChiefSeedCapacity,
                    midpoint);
            } else {
                d_append_local_budget_seed_family(
                    surfaces,
                    cfg,
                    wavelength_um,
                    state.trial_seeds,
                    &state.trial_count,
                    kBudgetChiefSeedCapacity,
                    state.center_radius,
                    state.base_seed,
                    scaled_guess.valid ? &scaled_guess : nullptr);
            }
            if (scaled_guess.valid) {
                d_append_local_budget_seed_family(
                    surfaces,
                    cfg,
                    wavelength_um,
                    state.trial_seeds,
                    &state.trial_count,
                    kBudgetChiefSeedCapacity,
                    state.center_radius,
                    scaled_guess,
                    &state.base_seed);
                DeviceSeed midpoint;
                midpoint.x = 0.5 * (state.base_seed.x + scaled_guess.x);
                midpoint.y = 0.5 * (state.base_seed.y + scaled_guess.y);
                midpoint.valid = 1;
                d_append_unique_clipped_seed(
                    surfaces,
                    cfg,
                    wavelength_um,
                    state.trial_seeds,
                    &state.trial_count,
                    kBudgetChiefSeedCapacity,
                    midpoint);
            }
        }
        if (stats) {
            atomicAdd(&stats->cooperative_bridge_stages, 1ull);
        }
        if (state.trial_count <= 0 ||
            !d_prepare_budget_chief_solve_context(
                surfaces,
                cfg,
                bridge_field,
                wavelength_um,
                state.stop_radius,
                &state.solve_context)) {
            d_finish_cooperative_bridge_stage_failure(&state, stats);
            return;
        }
        DeviceSeed solved;
        const bool solved_ok = d_try_budget_chief_seed_precomputed<true>(
            surfaces,
            cfg,
            bridge_field,
            wavelength_um,
            state.solve_context,
            state.trial_seeds[0],
            state.require_convergence != 0,
            &solved,
            nullptr,
            nullptr,
            nullptr);
        if (solved_ok && solved.valid) {
            state.base_seed = solved;
            state.base_rho = bridge_rho;
            state.built_bridge = 1;
            state.status = kCooperativeBridgeReady;
            if (stats) {
                atomicAdd(&stats->cooperative_bridge_seed0_solved, 1ull);
            }
            return;
        }
        if (state.trial_count > 1) {
            state.status = kCooperativeBridgeWaiting;
        } else {
            d_finish_cooperative_bridge_stage_failure(&state, stats);
        }
        return;
    }
    state.status =
        state.built_bridge || state.has_inner
            ? kCooperativeTargetReady
            : kCooperativeBridgeFailed;
    if (state.status == kCooperativeBridgeFailed && stats) {
        atomicAdd(&stats->cooperative_failed, 1ull);
    }
}
} // namespace
} // namespace optiland
