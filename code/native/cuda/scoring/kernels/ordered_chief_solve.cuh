#pragma once
#include "../prelude.cuh"
#include "../aiming/chief_geometry.cuh"
#include "../aiming/chief_optical_context.cuh"
#include "../aiming/chief_secant_solver.cuh"
#include "../aiming/chromatic_anchor.cuh"
#include "../aiming/seed_candidates.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/calibration_tasks.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../geometry/pupil_paraxial.cuh"

namespace optiland {
namespace {
template <bool kTrackRay = true>
__global__ void solve_ordered_chief_tile_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    DeviceConfig cfg,
    int population_size,
    int field_index,
    int wave_index,
    int tile_begin,
    int tile_size,
    const DeviceOrderedChiefTask* tasks,
    DeviceOrderedChiefTrialResult* results) {
    const int item = blockIdx.x * blockDim.x + threadIdx.x;
    const int item_count = population_size * tile_size;
    if (item >= item_count) {
        return;
    }
    const int row = item / tile_size;
    fields = d_candidate_row_fields(fields, cfg, row);
    const int lane = item - row * tile_size;
    DeviceOrderedChiefTrialResult result;
    const DeviceOrderedChiefTask& task = tasks[row];
    const int trial_index = tile_begin + lane;
    if (!task.chief_valid && trial_index < task.trial_count) {
        const DeviceSurface* surfaces =
            candidate_surfaces +
            static_cast<long long>(row) * cfg.surface_count;
        const double rs = d_aperture_stop_radius(
            surfaces,
            cfg,
            waves[wave_index].value_um);
        result.success =
            (cfg.calibrated_ordered_reuse_solve_context
                 ? d_try_budget_chief_seed_precomputed<kTrackRay>(
                       surfaces,
                       cfg,
                       fields[field_index],
                       waves[wave_index].value_um,
                       task.solve_context,
                       task.trial_seeds[trial_index],
                       task.require_convergence != 0,
                       &result.chief_seed,
                       &result.stop_x,
                       &result.stop_y,
                       &result.residual)
                 : d_try_budget_chief_seed<kTrackRay>(
                       surfaces,
                       cfg,
                       fields[field_index],
                       waves[wave_index].value_um,
                       rs,
                       task.trial_seeds[trial_index],
                       task.require_convergence != 0,
                       &result.chief_seed,
                       &result.stop_x,
                       &result.stop_y,
                       &result.residual))
                ? 1
                : 0;
    }
    results[item] = result;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void reduce_ordered_chief_tile_kernel(
    int population_size,
    int tile_begin,
    int tile_size,
    DeviceOrderedChiefTask* tasks,
    const DeviceOrderedChiefTrialResult* results,
    DeviceOrderedChiefStats* stats) {
    const int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= population_size || tasks[row].chief_valid) {
        return;
    }
    const int base = row * tile_size;
    for (int lane = 0; lane < tile_size; ++lane) {
        const DeviceOrderedChiefTrialResult result = results[base + lane];
        if (!result.success || !result.chief_seed.valid) {
            continue;
        }
        tasks[row].chief_seed = result.chief_seed;
        tasks[row].stop_x = result.stop_x;
        tasks[row].stop_y = result.stop_y;
        tasks[row].residual = result.residual;
        tasks[row].chief_valid = 1;
        if (stats) {
            const int trial_index = tile_begin + lane;
            const int bucket = trial_index <= 0
                                   ? 0
                                   : trial_index == 1
                                         ? 1
                                         : trial_index <= 3
                                               ? 2
                                               : trial_index <= 7
                                                     ? 3
                                                     : trial_index <= 15
                                                           ? 4
                                                           : trial_index <= 23
                                                                 ? 5
                                                                 : trial_index <= 31
                                                                       ? 6
                                                                       : 7;
            atomicAdd(&stats->solved_buckets[bucket], 1ull);
        }
        return;
    }
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void initialize_cooperative_bridge_tasks_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    DeviceConfig cfg,
    int population_size,
    int center_field_index,
    int field_index,
    int wave_index,
    int processed_trial_limit,
    const DeviceCalibratedModel* models,
    const DeviceOrderedChiefTask* ordered_tasks,
    DeviceCooperativeBridgeTask* bridge_tasks,
    DeviceOrderedChiefStats* stats) {
    const int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= population_size) {
        return;
    }
    fields = d_candidate_row_fields(fields, cfg, row);
    DeviceCooperativeBridgeTask& state = bridge_tasks[row];
    state = DeviceCooperativeBridgeTask{};
    const DeviceOrderedChiefTask& ordered = ordered_tasks[row];
    if (field_index < 0 || field_index >= cfg.field_count ||
        wave_index < 0 || wave_index >= cfg.wavelength_count ||
        waves[wave_index].weight <= 0.0 || ordered.chief_valid ||
        ordered.trial_count > processed_trial_limit) {
        return;
    }
    const DeviceField& field = fields[field_index];
    const DeviceField& center_field = fields[center_field_index];
    const double target_rho = hypot(field.hx, field.hy);
    if (!(target_rho > 1.0e-9)) {
        return;
    }
    const long long model_index =
        (static_cast<long long>(row) * cfg.field_count + field_index) *
            cfg.wavelength_count +
        wave_index;
    const long long center_model_index =
        (static_cast<long long>(row) * cfg.field_count + center_field_index) *
            cfg.wavelength_count +
        wave_index;
    const DeviceCalibratedCenterFootprint center_footprint =
        d_make_center_footprint(models[center_model_index]);
    if (!center_footprint.valid || !center_footprint.chief_seed.valid) {
        return;
    }
    const DeviceSurface* surfaces =
        candidate_surfaces + static_cast<long long>(row) * cfg.surface_count;
    const double wavelength_um = waves[wave_index].value_um;
    const double rs = d_aperture_stop_radius(surfaces, cfg, wavelength_um);
    const double field_angle_deg =
        target_rho * fmax(0.0, cfg.max_field_deg);
    const bool skip_abcd_seed =
        cfg.calibrated_budget_skip_abcd_angle_deg > 0.0 &&
        field_angle_deg >
            cfg.calibrated_budget_skip_abcd_angle_deg + 1.0e-9;
    bool singular = false;
    DeviceSeed abcd_seed;
    if (!skip_abcd_seed) {
        abcd_seed = d_estimate_abcd_chief_seed(
            surfaces, cfg, field, wavelength_um, rs, &singular);
    }
    const bool abcd_seed_invalid =
        !skip_abcd_seed && (singular || !abcd_seed.valid);
    const bool abcd_seed_unusable = skip_abcd_seed || abcd_seed_invalid;
    const bool abcd_seed_oob =
        !abcd_seed_unusable &&
        !d_seed_in_first_surface_bounds(
            surfaces, cfg, wavelength_um, abcd_seed);
    const bool prefer_angle_bridge =
        cfg.calibrated_angle_bridge_fast &&
        (abcd_seed_unusable || abcd_seed_oob ||
         field_angle_deg > 35.0 + 1.0e-9);
    if (!prefer_angle_bridge) {
        return;
    }

    const DeviceCalibratedModel* inner_model = nullptr;
    double best_inner_rho = -1.0;
    if (field_index != center_field_index) {
        for (int candidate = 0; candidate < field_index; ++candidate) {
            if (!d_same_field_direction(fields[candidate], field)) {
                continue;
            }
            const double candidate_rho =
                hypot(fields[candidate].hx, fields[candidate].hy);
            if (candidate_rho >= target_rho - 1.0e-9 ||
                candidate_rho <= best_inner_rho) {
                continue;
            }
            const long long candidate_model_index =
                (static_cast<long long>(row) * cfg.field_count + candidate) *
                    cfg.wavelength_count +
                wave_index;
            if (!models[candidate_model_index].valid ||
                !models[candidate_model_index].chief_seed.valid) {
                continue;
            }
            best_inner_rho = candidate_rho;
            inner_model = models + candidate_model_index;
        }
    }
    state.base_seed = center_footprint.chief_seed;
    state.base_rho = hypot(center_field.hx, center_field.hy);
    if (inner_model && inner_model->valid &&
        best_inner_rho > state.base_rho) {
        state.base_seed = inner_model->chief_seed;
        state.base_rho = best_inner_rho;
    }
    state.has_inner =
        inner_model && inner_model->valid &&
                best_inner_rho > hypot(center_field.hx, center_field.hy)
            ? 1
            : 0;
    state.target_abcd_seed = abcd_seed;
    state.target_rho = target_rho;
    state.center_radius = d_center_meridional_radius(
        surfaces, cfg, center_field, wavelength_um);
    state.stop_radius = rs;
    state.require_convergence = abcd_seed_unusable ? 1 : 0;
    state.status = kCooperativeBridgeReady;
    if (stats) {
        atomicAdd(&stats->cooperative_eligible, 1ull);
    }
    (void)model_index;
}
} // namespace
} // namespace optiland
