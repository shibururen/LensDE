#pragma once
#include "../prelude.cuh"
#include "../aiming/chromatic_anchor.cuh"
#include "../aiming/model_builder.cuh"
#include "../aiming/ordered_chief_tasks.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/calibration_tasks.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"

namespace optiland {
namespace {
template <int kEdgeCount>
__global__ void replay_calibrated_edges_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    DeviceConfig cfg,
    int population_size,
    int center_field_index,
    const DeviceCalibratedModel* source_models,
    DeviceCalibratedModel* replay_models) {
    const int item = blockIdx.x * blockDim.x + threadIdx.x;
    const int models_per_row = cfg.field_count * cfg.wavelength_count;
    const int item_count = population_size * models_per_row;
    if (item >= item_count) {
        return;
    }
    const int row = item / models_per_row;
    fields = d_candidate_row_fields(fields, cfg, row);
    const int local = item - row * models_per_row;
    const int field_index = local / cfg.wavelength_count;
    const int wave_index = local - field_index * cfg.wavelength_count;
    const DeviceCalibratedModel& source = source_models[item];
    if (waves[wave_index].weight <= 0.0 ||
        !source.valid || !source.chief_seed.valid) {
        replay_models[item] = DeviceCalibratedModel{};
        return;
    }
    const DeviceSurface* surfaces =
        candidate_surfaces + static_cast<long long>(row) * cfg.surface_count;
    const long long center_model_index =
        (static_cast<long long>(row) * cfg.field_count + center_field_index) *
            cfg.wavelength_count +
        wave_index;
    DeviceCalibratedCenterFootprint center_footprint;
    const DeviceCalibratedCenterFootprint* center_footprint_ptr = nullptr;
    if (field_index != center_field_index) {
        center_footprint =
            d_make_center_footprint(source_models[center_model_index]);
        if (center_footprint.valid) {
            center_footprint_ptr = &center_footprint;
        }
    }
    const DeviceCalibratedModel* same_field_wave_model = nullptr;
    if (wave_index > 0 && source_models[item - 1].valid) {
        same_field_wave_model = source_models + item - 1;
    }
    d_build_calibrated_model<true, kEdgeCount>(
        surfaces,
        cfg,
        fields[field_index],
        waves[wave_index].value_um,
        replay_models + item,
        fields + center_field_index,
        center_footprint_ptr,
        nullptr,
        nullptr,
        field_index,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        same_field_wave_model,
        &source.chief_seed,
        source.chief_stop_x,
        source.chief_stop_y,
        0.0,
        false);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void prepare_ordered_chief_tasks_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    DeviceConfig cfg,
    int population_size,
    int center_field_index,
    int field_index,
    int wave_index,
    const DeviceCalibratedModel* models,
    int seed_capacity,
    bool preserve_solved,
    DeviceOrderedChiefTask* tasks,
    DeviceOrderedChiefStats* stats) {
    const int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= population_size || field_index < 0 ||
        field_index >= cfg.field_count || wave_index < 0 ||
        wave_index >= cfg.wavelength_count) {
        return;
    }
    fields = d_candidate_row_fields(fields, cfg, row);
    if (waves[wave_index].weight <= 0.0) {
        tasks[row].chief_seed = DeviceSeed{};
        tasks[row].stop_x = 0.0;
        tasks[row].stop_y = 0.0;
        tasks[row].residual = INFINITY;
        tasks[row].trial_count = 0;
        tasks[row].require_convergence = 0;
        tasks[row].chief_valid = 0;
        return;
    }
    if (preserve_solved && tasks[row].chief_valid) {
        return;
    }
    const DeviceSurface* surfaces =
        candidate_surfaces + static_cast<long long>(row) * cfg.surface_count;
    const long long model_index =
        (static_cast<long long>(row) * cfg.field_count + field_index) *
            cfg.wavelength_count +
        wave_index;
    const long long center_model_index =
        (static_cast<long long>(row) * cfg.field_count + center_field_index) *
            cfg.wavelength_count +
        wave_index;
    DeviceCalibratedCenterFootprint center_footprint;
    const DeviceCalibratedCenterFootprint* center_footprint_ptr = nullptr;
    if (field_index != center_field_index) {
        center_footprint = d_make_center_footprint(models[center_model_index]);
        if (center_footprint.valid) {
            center_footprint_ptr = &center_footprint;
        }
    }
    const DeviceCalibratedModel* inner_model = nullptr;
    const DeviceField* inner_field = nullptr;
    const DeviceCalibratedModel* previous_model = nullptr;
    const DeviceField* previous_field = nullptr;
    const DeviceCalibratedModel* previous2_model = nullptr;
    const DeviceField* previous2_field = nullptr;
    const DeviceCalibratedModel* same_field_wave_model =
        d_primary_anchored_same_field_wave_model(
            models, model_index, cfg, wave_index);
    if (field_index != center_field_index && field_index > 0) {
        const long long previous_index =
            (static_cast<long long>(row) * cfg.field_count + field_index - 1) *
                cfg.wavelength_count +
            wave_index;
        previous_model = models + previous_index;
        previous_field = fields + field_index - 1;
    }
    if (field_index != center_field_index && field_index > 1) {
        const long long previous2_index =
            (static_cast<long long>(row) * cfg.field_count + field_index - 2) *
                cfg.wavelength_count +
            wave_index;
        previous2_model = models + previous2_index;
        previous2_field = fields + field_index - 2;
    }
    double best_inner_rho = -1.0;
    if (field_index != center_field_index) {
        const double target_rho =
            hypot(fields[field_index].hx, fields[field_index].hy);
        for (int candidate = 0; candidate < field_index; ++candidate) {
            if (!d_same_field_direction(
                    fields[candidate], fields[field_index])) {
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
            inner_field = fields + candidate;
        }
    }
    d_prepare_ordered_chief_task(
        surfaces,
        cfg,
        fields[field_index],
        waves[wave_index].value_um,
        center_footprint_ptr ? &center_footprint_ptr->chief_seed : nullptr,
        fields + center_field_index,
        inner_model && inner_model->valid ? &inner_model->chief_seed : nullptr,
        inner_field ? hypot(inner_field->hx, inner_field->hy) : 0.0,
        field_index,
        previous_model && previous_model->valid
            ? &previous_model->chief_seed
            : nullptr,
        previous_field,
        previous2_model && previous2_model->valid
            ? &previous2_model->chief_seed
            : nullptr,
        previous2_field,
        same_field_wave_model && same_field_wave_model->valid
            ? &same_field_wave_model->chief_seed
            : nullptr,
        seed_capacity,
        tasks + row);
    if (stats) {
        atomicAdd(&stats->prepared, 1ull);
        if (tasks[row].chief_valid) {
            atomicAdd(&stats->paraxial_accepted, 1ull);
        } else if (tasks[row].trial_count > 0) {
            atomicAdd(&stats->trial_tasks, 1ull);
            atomicAdd(
                &stats->trial_seed_sum,
                static_cast<unsigned long long>(tasks[row].trial_count));
        }
    }
}
} // namespace
} // namespace optiland
