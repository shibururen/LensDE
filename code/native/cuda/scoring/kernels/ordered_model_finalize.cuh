#pragma once
#include "../prelude.cuh"
#include "../aiming/budget_chief.cuh"
#include "../aiming/chromatic_anchor.cuh"
#include "../aiming/model_builder.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/calibration_tasks.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../geometry/pupil_paraxial.cuh"

namespace optiland {
namespace {
template <bool kFallback, bool kChiefOnlyFallback = false>
__global__ void finalize_ordered_calibrated_models_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    DeviceConfig cfg,
    int population_size,
    int center_field_index,
    int field_index,
    int wave_index,
    int processed_trial_limit,
    bool resume_serial_tail,
    bool skip_valid_models,
    DeviceCalibratedModel* models,
    DeviceOrderedChiefTask* tasks,
    DeviceOrderedChiefStats* stats) {
    const int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= population_size) {
        return;
    }
    fields = d_candidate_row_fields(fields, cfg, row);
    const long long model_index =
        (static_cast<long long>(row) * cfg.field_count + field_index) *
            cfg.wavelength_count +
        wave_index;
    if (waves[wave_index].weight <= 0.0) {
        if constexpr (!kFallback) {
            models[model_index] = DeviceCalibratedModel{};
        }
        return;
    }
    DeviceOrderedChiefTask& task = tasks[row];
    if constexpr (kFallback) {
        if (task.chief_valid) {
            return;
        }
        if (task.cooperative_exhausted) {
            models[model_index] = DeviceCalibratedModel{};
            return;
        }
        if (stats) {
            atomicAdd(&stats->serial_fallback, 1ull);
        }
    } else {
        if (!task.chief_valid ||
            (skip_valid_models && models[model_index].valid)) {
            return;
        }
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
    if constexpr (kFallback && kChiefOnlyFallback) {
        models[model_index] = DeviceCalibratedModel{};
        DeviceSeed solved;
        double sx = 0.0;
        double sy = 0.0;
        double residual = INFINITY;
        const bool chief_ok = d_try_calibrated_budget_chief(
            surfaces,
            cfg,
            fields[field_index],
            waves[wave_index].value_um,
            d_aperture_stop_radius(
                surfaces, cfg, waves[wave_index].value_um),
            center_footprint_ptr ? &center_footprint_ptr->chief_seed : nullptr,
            fields + center_field_index,
            inner_model && inner_model->valid
                ? &inner_model->chief_seed
                : nullptr,
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
            resume_serial_tail
                ? min(task.trial_count, processed_trial_limit)
                : (task.trial_count <= processed_trial_limit
                       ? task.trial_count
                       : 0),
            &solved,
            &sx,
            &sy,
            &residual);
        if (chief_ok && solved.valid) {
            task.chief_seed = solved;
            task.stop_x = sx;
            task.stop_y = sy;
            task.residual = residual;
            task.chief_valid = 1;
            if (stats) {
                atomicAdd(&stats->serial_fallback_solved, 1ull);
            }
        } else if (stats) {
            atomicAdd(&stats->serial_fallback_failed, 1ull);
        }
    } else if constexpr (kFallback) {
        d_build_calibrated_model<false>(
            surfaces,
            cfg,
            fields[field_index],
            waves[wave_index].value_um,
            models + model_index,
            fields + center_field_index,
            center_footprint_ptr,
            inner_model,
            inner_field,
            field_index,
            previous_model,
            previous_field,
            previous2_model,
            previous2_field,
            same_field_wave_model,
            nullptr,
            NAN,
            NAN,
            INFINITY,
            resume_serial_tail
                ? min(task.trial_count, processed_trial_limit)
                : (task.trial_count <= processed_trial_limit
                       ? task.trial_count
                       : 0));
        if (stats) {
            if (models[model_index].valid &&
                models[model_index].chief_seed.valid) {
                atomicAdd(&stats->serial_fallback_solved, 1ull);
            } else {
                atomicAdd(&stats->serial_fallback_failed, 1ull);
            }
        }
    } else {
        d_build_calibrated_model<true>(
            surfaces,
            cfg,
            fields[field_index],
            waves[wave_index].value_um,
            models + model_index,
            fields + center_field_index,
            center_footprint_ptr,
            inner_model,
            inner_field,
            field_index,
            previous_model,
            previous_field,
            previous2_model,
            previous2_field,
            same_field_wave_model,
            &task.chief_seed,
            task.stop_x,
            task.stop_y,
            task.residual,
            false);
    }
}
} // namespace
} // namespace optiland
