#pragma once
#include "../prelude.cuh"
#include "../aiming/chief_secant_solver.cuh"
#include "../aiming/chromatic_anchor.cuh"
#include "../aiming/seed_candidates.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/calibration_tasks.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "cooperative_bridge_prepare.cuh"

namespace optiland {
namespace {
__global__ void prepare_cooperative_target_stage_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    DeviceConfig cfg,
    int population_size,
    int field_index,
    int wave_index,
    DeviceOrderedChiefTask* ordered_tasks,
    DeviceCooperativeBridgeTask* bridge_tasks,
    DeviceOrderedChiefStats* stats) {
    const int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= population_size) {
        return;
    }
    fields = d_candidate_row_fields(fields, cfg, row);
    DeviceCooperativeBridgeTask& state = bridge_tasks[row];
    if (state.status == kCooperativeBridgeReady &&
        state.next_bridge_index >= 3) {
        state.status =
            state.built_bridge || state.has_inner
                ? kCooperativeTargetReady
                : kCooperativeBridgeFailed;
    }
    if (state.status != kCooperativeTargetReady) {
        return;
    }
    DeviceOrderedChiefTask& ordered = ordered_tasks[row];
    if (ordered.chief_valid) {
        state.status = kCooperativeBridgeSolved;
        return;
    }
    const DeviceSurface* surfaces =
        candidate_surfaces + static_cast<long long>(row) * cfg.surface_count;
    const double wavelength_um = waves[wave_index].value_um;
    state.solve_field = fields[field_index];
    state.trial_count =
        max(0, min(ordered.trial_count, kBudgetChiefSeedCapacity));
    for (int i = 0; i < state.trial_count; ++i) {
        state.trial_seeds[i] = ordered.trial_seeds[i];
    }
    state.trial_begin = state.trial_count;
    d_append_unique_clipped_seed(
        surfaces,
        cfg,
        wavelength_um,
        state.trial_seeds,
        &state.trial_count,
        kBudgetChiefSeedCapacity,
        state.base_seed);
    if (state.base_rho > 1.0e-12 &&
        state.target_rho > state.base_rho + 1.0e-9) {
        DeviceSeed scaled = state.base_seed;
        const double scale = state.target_rho / state.base_rho;
        scaled.x *= scale;
        scaled.y *= scale;
        if (scaled.valid) {
            d_append_unique_clipped_seed(
                surfaces,
                cfg,
                wavelength_um,
                state.trial_seeds,
                &state.trial_count,
                kBudgetChiefSeedCapacity,
                scaled);
            d_append_local_budget_seed_family(
                surfaces,
                cfg,
                wavelength_um,
                state.trial_seeds,
                &state.trial_count,
                kBudgetChiefSeedCapacity,
                state.center_radius,
                scaled,
                &state.base_seed);
            DeviceSeed midpoint;
            midpoint.x = 0.5 * (state.base_seed.x + scaled.x);
            midpoint.y = 0.5 * (state.base_seed.y + scaled.y);
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
        atomicAdd(&stats->cooperative_target_stages, 1ull);
    }
    if (state.trial_begin >= state.trial_count ||
        !d_prepare_budget_chief_solve_context(
            surfaces,
            cfg,
            state.solve_field,
            wavelength_um,
            state.stop_radius,
            &state.solve_context)) {
        state.status = kCooperativeBridgeFailed;
        if (stats) {
            atomicAdd(&stats->cooperative_failed, 1ull);
        }
        return;
    }
    DeviceOrderedChiefTrialResult result;
    result.success = d_try_budget_chief_seed_precomputed<true>(
                         surfaces,
                         cfg,
                         state.solve_field,
                         wavelength_um,
                         state.solve_context,
                         state.trial_seeds[state.trial_begin],
                         state.require_convergence != 0,
                         &result.chief_seed,
                         &result.stop_x,
                         &result.stop_y,
                         &result.residual)
                         ? 1
                         : 0;
    if (result.success && result.chief_seed.valid) {
        ordered.chief_seed = result.chief_seed;
        ordered.stop_x = result.stop_x;
        ordered.stop_y = result.stop_y;
        ordered.residual = result.residual;
        ordered.chief_valid = 1;
        state.status = kCooperativeBridgeSolved;
        if (stats) {
            atomicAdd(&stats->cooperative_target_seed0_solved, 1ull);
        }
    } else if (state.trial_count > state.trial_begin + 1) {
        state.status = kCooperativeTargetWaiting;
    } else {
        state.status = kCooperativeBridgeFailed;
        if (stats) {
            atomicAdd(&stats->cooperative_failed, 1ull);
        }
    }
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void solve_cooperative_bridge_tile_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceWave* waves,
    DeviceConfig cfg,
    int population_size,
    int wave_index,
    int tile_begin,
    int tile_size,
    const DeviceCooperativeBridgeTask* bridge_tasks,
    DeviceOrderedChiefTrialResult* results) {
    const int item = blockIdx.x * blockDim.x + threadIdx.x;
    const int item_count = population_size * tile_size;
    if (item >= item_count) {
        return;
    }
    const int row = item / tile_size;
    const int lane = item - row * tile_size;
    const DeviceCooperativeBridgeTask& state = bridge_tasks[row];
    DeviceOrderedChiefTrialResult result;
    const bool waiting =
        state.status == kCooperativeBridgeWaiting ||
        state.status == kCooperativeTargetWaiting;
    const int trial_index = state.trial_begin + tile_begin + lane;
    if (waiting && trial_index < state.trial_count) {
        const DeviceSurface* surfaces =
            candidate_surfaces +
            static_cast<long long>(row) * cfg.surface_count;
        result.success = d_try_budget_chief_seed_precomputed<true>(
                             surfaces,
                             cfg,
                             state.solve_field,
                             waves[wave_index].value_um,
                             state.solve_context,
                             state.trial_seeds[trial_index],
                             state.require_convergence != 0,
                             &result.chief_seed,
                             &result.stop_x,
                             &result.stop_y,
                             &result.residual)
                             ? 1
                             : 0;
    }
    results[item] = result;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void reduce_cooperative_bridge_tile_kernel(
    int population_size,
    int tile_begin,
    int tile_size,
    DeviceOrderedChiefTask* ordered_tasks,
    DeviceCooperativeBridgeTask* bridge_tasks,
    const DeviceOrderedChiefTrialResult* results,
    DeviceOrderedChiefStats* stats) {
    const int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= population_size) {
        return;
    }
    DeviceCooperativeBridgeTask& state = bridge_tasks[row];
    const bool target = state.status == kCooperativeTargetWaiting;
    if (!target && state.status != kCooperativeBridgeWaiting) {
        return;
    }
    const int base = row * tile_size;
    for (int lane = 0; lane < tile_size; ++lane) {
        const int trial_index = state.trial_begin + tile_begin + lane;
        if (trial_index >= state.trial_count) {
            break;
        }
        const DeviceOrderedChiefTrialResult result = results[base + lane];
        if (!result.success || !result.chief_seed.valid) {
            continue;
        }
        if (target) {
            DeviceOrderedChiefTask& ordered = ordered_tasks[row];
            ordered.chief_seed = result.chief_seed;
            ordered.stop_x = result.stop_x;
            ordered.stop_y = result.stop_y;
            ordered.residual = result.residual;
            ordered.chief_valid = 1;
            state.status = kCooperativeBridgeSolved;
            if (stats) {
                atomicAdd(&stats->cooperative_target_tile_solved, 1ull);
            }
        } else {
            state.base_seed = result.chief_seed;
            state.base_rho = state.candidate_rho;
            state.built_bridge = 1;
            state.status = kCooperativeBridgeReady;
            if (stats) {
                atomicAdd(&stats->cooperative_bridge_tile_solved, 1ull);
            }
        }
        return;
    }
    if (state.trial_begin + tile_begin + tile_size < state.trial_count) {
        return;
    }
    if (target) {
        state.status = kCooperativeBridgeFailed;
        if (stats) {
            atomicAdd(&stats->cooperative_failed, 1ull);
        }
    } else {
        d_finish_cooperative_bridge_stage_failure(&state, stats);
    }
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void publish_cooperative_bridge_exhaustion_kernel(
    int population_size,
    DeviceOrderedChiefTask* ordered_tasks,
    const DeviceCooperativeBridgeTask* bridge_tasks) {
    const int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= population_size) {
        return;
    }
    if (bridge_tasks[row].status == kCooperativeBridgeFailed) {
        ordered_tasks[row].cooperative_exhausted = 1;
    }
}
} // namespace
} // namespace optiland
