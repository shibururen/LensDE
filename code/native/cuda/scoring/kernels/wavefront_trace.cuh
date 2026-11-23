#pragma once
#include "../prelude.cuh"
#include "../aiming/chromatic_anchor.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../contracts/spot_wavefront_records.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/first_order_metrics.cuh"
#include "../host/certificate_context.cuh"
#include "../merit/residual_primitives.cuh"
#include "../merit/supported_ray_generation.cuh"
#include "../sampling/pupil_nodes.cuh"
#include "../wavefront/reference_geometry.cuh"

namespace optiland {
namespace {
__device__ double d_wavefront_model_value(
    int wave_index,
    double dir_x,
    double dir_y,
    const int* piston_columns,
    int dir_x_column,
    int dir_y_column,
    const double* coefficients,
    int coefficient_count) {
    if (wave_index < 0 || wave_index >= kMaxDeviceWaves ||
        piston_columns[wave_index] < 0 ||
        piston_columns[wave_index] >= coefficient_count) {
        return NAN;
    }
    double value = coefficients[piston_columns[wave_index]];
    if (dir_x_column >= 0 && dir_x_column < coefficient_count) {
        value += coefficients[dir_x_column] * dir_x;
    }
    if (dir_y_column >= 0 && dir_y_column < coefficient_count) {
        value += coefficients[dir_y_column] * dir_y;
    }
    return value;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void wavefront_stats_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    DeviceConfig cfg,
    int population_size,
    int pupil_count,
    const DeviceSpotRaySample* spot_samples,
    const DeviceSpotRaySample* guard_samples,
    const DeviceChiefRaySample* chief_samples,
    DeviceFieldWavefrontStats* field_stats) {
    const int item = blockIdx.x * blockDim.x + threadIdx.x;
    const int item_count = population_size * cfg.field_count;
    if (item >= item_count) {
        return;
    }
    const int row = item / cfg.field_count;
    if (evaluation_skip_outer_row(row)) { field_stats[item]=DeviceFieldWavefrontStats{}; return; }
    fields = d_candidate_row_fields(fields, cfg, row);
    const int field_index = item - row * cfg.field_count;
    const int active_pupil_count =
        d_field_pupil_count(cfg, fields[field_index]);
    const DeviceSurface* row_surfaces =
        candidate_surfaces + static_cast<long long>(row) * cfg.surface_count;
    const int stop_surface_index = d_stop_index(row_surfaces, cfg);
    const DeviceSurface* surfaces = row_surfaces;
    const long long field_wave_base =
        (static_cast<long long>(row) * cfg.field_count + field_index) *
        cfg.wavelength_count;
    const DeviceChiefRaySample* chiefs = chief_samples + field_wave_base;
    const DeviceSpotRaySample* samples =
        spot_samples + field_wave_base * pupil_count;
    const DeviceSpotRaySample* guards = guard_samples + field_wave_base * 4;
    DeviceFieldWavefrontStats stats;
    const DeviceWavefrontReference reference =
        d_make_wavefront_reference(surfaces, cfg, chiefs);
    if (!reference.valid) {
        stats.chief_ok = 0;
        field_stats[item] = stats;
        return;
    }
    DeviceWavefrontObjectReference object_references[kMaxDeviceWaves] = {};
    for (int wi = 0; wi < cfg.wavelength_count; ++wi) {
        object_references[wi] = d_make_wavefront_object_reference(
            surfaces, cfg, chiefs[wi], waves[wi].value_um);
    }
    int piston_columns[kMaxDeviceWaves] = {};
    int dir_x_column = -1;
    int dir_y_column = -1;
    int coefficient_count = 0;
    double coefficients[kMaxDeviceWaves + 2] = {};
    bool fitted = false;
    if (cfg.wavefront_centroid_reference) {
        fitted = d_fit_wavefront_model(
            surfaces, waves, cfg, samples, pupil_count, active_pupil_count,
            chiefs, reference, object_references,
            true, true, piston_columns, &dir_x_column, &dir_y_column,
            coefficients, &coefficient_count);
        if (!fitted) {
            fitted = d_fit_wavefront_model(
                surfaces, waves, cfg, samples, pupil_count, active_pupil_count,
                chiefs, reference, object_references,
                true, false, piston_columns, &dir_x_column, &dir_y_column,
                coefficients, &coefficient_count);
        }
        if (!fitted) {
            fitted = d_fit_wavefront_model(
                surfaces, waves, cfg, samples, pupil_count, active_pupil_count,
                chiefs, reference, object_references,
                false, true, piston_columns, &dir_x_column, &dir_y_column,
                coefficients, &coefficient_count);
        }
    }
    if (!fitted) {
        fitted = d_fit_wavefront_model(
            surfaces, waves, cfg, samples, pupil_count, active_pupil_count,
            chiefs, reference, object_references,
            false, false, piston_columns, &dir_x_column, &dir_y_column,
            coefficients, &coefficient_count);
    }
    if (!fitted) {
        stats.chief_ok = 0;
        field_stats[item] = stats;
        return;
    }
    double total_weight = 0.0;
    double valid_weight = 0.0;
    double sum_weight = 0.0;
    double sum_opd2 = 0.0;
    double max_abs = 0.0;
    for (int wi = 0; wi < cfg.wavelength_count; ++wi) {
        if (!chiefs[wi].usable) {
            stats.chief_ok = 0;
        }
        for (int pi = 0; pi < active_pupil_count; ++pi) {
            const DeviceSpotRaySample& sample = samples[wi * pupil_count + pi];
            if (d_calibrated_paraxial_stop_sampling_rejection(
                    sample, cfg, stop_surface_index)) {
                continue;
            }
            const double sample_weight = d_wavefront_sample_weight(sample);
            total_weight += sample_weight;
            double opd_waves = NAN;
            if (sample_weight <= 0.0 || !d_wavefront_opd_waves(
                    surfaces, cfg, waves[wi], chiefs[wi], sample,
                    reference, object_references[wi], &opd_waves)) {
                continue;
            }
            const double model = d_wavefront_model_value(
                wi, sample.l, sample.m, piston_columns,
                dir_x_column, dir_y_column, coefficients, coefficient_count);
            const double residual = opd_waves - model;
            if (!d_finite(residual)) {
                continue;
            }
            valid_weight += sample_weight;
            sum_weight += sample_weight;
            sum_opd2 += sample_weight * residual * residual;
            max_abs = fmax(max_abs, fabs(residual));
        }
        for (int gi = 0; gi < 4; ++gi) {
            const DeviceSpotRaySample& sample = guards[wi * 4 + gi];
            const double sample_weight = d_wavefront_sample_weight(sample);
            double opd_waves = NAN;
            if (sample_weight <= 0.0 || !d_wavefront_opd_waves(
                    surfaces, cfg, waves[wi], chiefs[wi], sample,
                    reference, object_references[wi], &opd_waves)) {
                continue;
            }
            const double model = d_wavefront_model_value(
                wi, sample.l, sample.m, piston_columns,
                dir_x_column, dir_y_column, coefficients, coefficient_count);
            if (d_finite(model)) {
                max_abs = fmax(max_abs, fabs(opd_waves - model));
            }
        }
    }
    stats.coverage = total_weight > 0.0 ? valid_weight / total_weight : 0.0;
    if (sum_weight > 0.0) {
        stats.rms_waves = sqrt(sum_opd2 / sum_weight);
        stats.max_abs_waves = max_abs;
    }
    field_stats[item] = stats;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void reduce_wavefront_fields_kernel(
    const DeviceFieldWavefrontStats* field_stats,
    const DeviceField* fields,
    DeviceConfig cfg,
    int population_size,
    CudaScoreSummary* summaries,
    DeviceMeritTerms* merit_terms) {
    const int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= population_size) {
        return;
    }
    fields = d_candidate_row_fields(fields, cfg, row);
    CudaScoreSummary summary = summaries[row];
    DeviceMeritTerms terms = merit_terms[row];
    double rms_sum = 0.0;
    double rms_count = 0.0;
    double max_rms = 0.0;
    double max_abs = 0.0;
    double max_ratio = 0.0;
    bool failed = false;
    const DeviceFieldWavefrontStats* row_stats =
        field_stats + static_cast<long long>(row) * cfg.field_count;
    for (int fi = 0; fi < cfg.field_count; ++fi) {
        const DeviceFieldWavefrontStats stats = row_stats[fi];
        const double field_weight = d_image_field_weight(cfg, fields[fi]);
        const bool usable = stats.chief_ok && d_finite(stats.rms_waves) &&
                            stats.coverage >= cfg.min_coverage_for_image_merit;
        if (!usable) {
            failed = true;
            continue;
        }
        rms_sum += stats.rms_waves;
        rms_count += 1.0;
        max_rms = fmax(max_rms, stats.rms_waves);
        if (d_finite(stats.max_abs_waves)) {
            max_abs = fmax(max_abs, stats.max_abs_waves);
            const double ratio =
                stats.max_abs_waves / fmax(1.0e-12, fabs(stats.rms_waves));
            max_ratio = fmax(max_ratio, ratio);
            if (cfg.operand_single_ray_wavefront_guard) {
                const double base =
                    field_weight * fabs(cfg.unified_image_wavefront_weight);
                if (base > 0.0) {
                    const double delta = d_upper_delta(
                        ratio, cfg.single_ray_wavefront_guard_upper);
                    terms.image_sum +=
                        base * d_pseudo_huber(delta, cfg.unified_loss_delta);
                    terms.image_weight += base;
                }
            }
        }
        if (cfg.operand_rms_wavefront) {
            const double base =
                field_weight * fabs(cfg.unified_image_wavefront_weight);
            const double tol = fmax(1.0e-9, cfg.quality_wavefront_scale);
            if (base > 0.0) {
                terms.image_sum +=
                    base * d_pseudo_huber(stats.rms_waves / tol, cfg.unified_loss_delta);
                terms.image_weight += base;
            }
        }
    }
    summary.mean_wavefront_rms = rms_count > 0.0 ? rms_sum / rms_count : NAN;
    summary.max_wavefront_rms = max_rms;
    summary.max_single_ray_wavefront = rms_count > 0.0 ? max_abs : NAN;
    summary.max_single_ray_wavefront_ratio = rms_count > 0.0 ? max_ratio : NAN;
    if (failed || !d_finite(summary.mean_wavefront_rms)) {
        summary.trace_ok = 0;
    }
    summaries[row] = summary;
    merit_terms[row] = terms;
}
} // namespace
} // namespace optiland
