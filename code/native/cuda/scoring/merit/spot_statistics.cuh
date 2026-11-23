#pragma once
#include "../prelude.cuh"
#include "../aiming/chromatic_anchor.cuh"
#include "../aiming/model_builder.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/calibration_tasks.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../contracts/ray_record.cuh"
#include "../contracts/spot_wavefront_records.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/material_and_sag.cuh"
#include "residual_primitives.cuh"
#include "supported_ray_generation.cuh"
#include "../ray/calibrated_generation.cuh"
#include "../ray/paraxial_generation.cuh"
#include "../ray/sequential_trace.cuh"
#include "../sampling/pupil_nodes.cuh"
#include "../sampling/symmetry_mapping.cuh"

namespace optiland {
namespace {
struct DeviceIlluminationFit {
    double normal[3][3]{};
    double rhs_l[3]{};
    double rhs_m[3]{};
    double total_weight = 0.0;
    double valid_intensity = 0.0;
    int fit_count = 0;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_solve_illumination_3x3(
    double matrix[3][3],
    const double rhs[3],
    double solution[3]) {
    double augmented[3][4];
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            augmented[row][column] = matrix[row][column];
        }
        augmented[row][3] = rhs[row];
    }
    for (int column = 0; column < 3; ++column) {
        int pivot_row = column;
        double pivot_magnitude = fabs(augmented[pivot_row][column]);
        for (int row = column + 1; row < 3; ++row) {
            const double magnitude = fabs(augmented[row][column]);
            if (magnitude > pivot_magnitude) {
                pivot_magnitude = magnitude;
                pivot_row = row;
            }
        }
        if (!(pivot_magnitude > 1.0e-18) || !d_finite(pivot_magnitude)) {
            return false;
        }
        if (pivot_row != column) {
            for (int entry = column; entry < 4; ++entry) {
                const double swap = augmented[column][entry];
                augmented[column][entry] = augmented[pivot_row][entry];
                augmented[pivot_row][entry] = swap;
            }
        }
        const double pivot = augmented[column][column];
        for (int entry = column; entry < 4; ++entry) {
            augmented[column][entry] /= pivot;
        }
        for (int row = 0; row < 3; ++row) {
            if (row == column) {
                continue;
            }
            const double factor = augmented[row][column];
            for (int entry = column; entry < 4; ++entry) {
                augmented[row][entry] -=
                    factor * augmented[column][entry];
            }
        }
    }
    for (int row = 0; row < 3; ++row) {
        solution[row] = augmented[row][3];
        if (!d_finite(solution[row])) {
            return false;
        }
    }
    return true;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ void d_accumulate_illumination_fit(
    DeviceIlluminationFit* fit,
    const DeviceField& field,
    double pupil_x,
    double pupil_y,
    double nominal_weight,
    double intensity,
    double direction_l,
    double direction_m,
    int trace_valid) {
    if (!fit) {
        return;
    }
    if (d_finite(nominal_weight) && nominal_weight > 0.0) {
        fit->total_weight += nominal_weight;
    }
    if (!trace_valid || !(nominal_weight > 0.0) ||
        !d_finite(direction_l) || !d_finite(direction_m)) {
        return;
    }
    const double qx =
        field.fvcx + (1.0 - field.fvdx) * pupil_x;
    const double qy =
        field.fvcy + (1.0 - field.fvdy) * pupil_y;
    if (!d_finite(qx) || !d_finite(qy)) {
        return;
    }
    const double features[3] = {1.0, qx, qy};
    for (int row = 0; row < 3; ++row) {
        fit->rhs_l[row] +=
            nominal_weight * features[row] * direction_l;
        fit->rhs_m[row] +=
            nominal_weight * features[row] * direction_m;
        for (int column = 0; column < 3; ++column) {
            fit->normal[row][column] +=
                nominal_weight * features[row] * features[column];
        }
    }
    if (d_finite(intensity) && intensity > 0.0) {
        fit->valid_intensity += intensity;
    }
    ++fit->fit_count;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ double d_finalize_illumination_fit(
    DeviceIlluminationFit fit,
    const DeviceField& field) {
    if (!(fit.total_weight > 0.0) || fit.fit_count < 3) {
        return NAN;
    }
    double diagonal_scale = 0.0;
    for (int i = 0; i < 3; ++i) {
        diagonal_scale = fmax(diagonal_scale, fabs(fit.normal[i][i]));
    }
    const double ridge = fmax(1.0e-15, diagonal_scale * 1.0e-12);
    for (int i = 0; i < 3; ++i) {
        fit.normal[i][i] += ridge;
    }
    double fit_l[3];
    double fit_m[3];
    double normal_l[3][3];
    double normal_m[3][3];
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            normal_l[row][column] = fit.normal[row][column];
            normal_m[row][column] = fit.normal[row][column];
        }
    }
    if (!d_solve_illumination_3x3(normal_l, fit.rhs_l, fit_l) ||
        !d_solve_illumination_3x3(normal_m, fit.rhs_m, fit_m)) {
        return NAN;
    }
    const double direction_jacobian =
        fabs(fit_l[1] * fit_m[2] - fit_l[2] * fit_m[1]);
    const double pupil_jacobian = fmax(
        0.0,
        (1.0 - field.fvdx) * (1.0 - field.fvdy));
    const double throughput = fit.valid_intensity / fit.total_weight;
    const double area =
        pupil_jacobian * direction_jacobian * throughput;
    return d_finite(area) && area >= 0.0 ? area : NAN;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __noinline__ DeviceFieldSpotStats d_trace_field_spot_stats(
    const DeviceSurface* surfaces,
    const DeviceField* fields,
    int field_index,
    const DeviceField& field,
    const DeviceWave* waves,
    const DeviceConfig& cfg) {
    DeviceFieldSpotStats stats;
    const int pupil_count = d_field_pupil_count(cfg, field);
    double chief_progress_sum = 0.0;
    double chief_weight_sum = 0.0;
    double sx = 0.0;
    double sy = 0.0;
    double progress_sum = 0.0;
    DeviceIlluminationFit illumination_fit{};
    const bool collect_illumination_proxy =
        d_finite(cfg.minimum_relative_illumination) &&
        cfg.minimum_relative_illumination > 0.0 &&
        cfg.minimum_relative_illumination <= 1.0;
    double sample_x[kMaxDeviceSpotSamples];
    double sample_y[kMaxDeviceSpotSamples];
    double sample_weight[kMaxDeviceSpotSamples];
    int sample_wave[kMaxDeviceSpotSamples];
    int sample_count = 0;
    constexpr int kMaxSpotGuardCount = 4 + kSpotTailExtraEdgeCount;
    double guard_x[kMaxDeviceWaves * kMaxSpotGuardCount];
    double guard_y[kMaxDeviceWaves * kMaxSpotGuardCount];
    int guard_count = 0;
    for (int wi = 0; wi < cfg.wavelength_count; ++wi) {
        const DeviceWave wave = waves[wi];
        if (wave.weight <= 0.0) {
            continue;
        }
        DeviceCalibratedModel calibrated_model;
        const DeviceCalibratedModel* calibrated_model_ptr = nullptr;
        if (cfg.aiming_mode == 5) {
            const int center_index = d_center_field_index(fields, cfg);
            DeviceCalibratedModel center_model;
            DeviceCalibratedCenterFootprint center_footprint;
            const DeviceField* center_field =
                (fields && center_index >= 0 && center_index < cfg.field_count)
                    ? &fields[center_index]
                    : &field;
            const DeviceCalibratedCenterFootprint* center_footprint_ptr = nullptr;
            if (fields && field_index != center_index) {
                d_build_calibrated_model(
                    surfaces,
                    cfg,
                    *center_field,
                    wave.value_um,
                    &center_model,
                    center_field,
                    nullptr);
                center_footprint = d_make_center_footprint(center_model);
                if (center_footprint.valid) {
                    center_footprint_ptr = &center_footprint;
                }
            }
            d_build_calibrated_model(
                surfaces,
                cfg,
                field,
                wave.value_um,
                &calibrated_model,
                center_field,
                center_footprint_ptr);
            calibrated_model_ptr = &calibrated_model;
        }
        if (cfg.operand_working_f_number &&
            field_index == d_center_field_index(fields, cfg) &&
            fabs(wave.value_um - cfg.primary_wavelength) <= 1.0e-10) {
            DeviceRay chief = d_generate_supported_ray(
                surfaces,
                cfg,
                field,
                0.0,
                0.0,
                1.0,
                wave.value_um,
                calibrated_model_ptr);
            d_trace_ray(surfaces, cfg, &chief);
            double image_n = NAN;
            for (int si = 1; si < cfg.surface_count; ++si) {
                if (surfaces[si].kind == kKindImage) {
                    image_n = d_surface_refractive_index(
                        surfaces[si - 1], cfg, wave.value_um);
                    break;
                }
            }
            if (d_ray_usable_for_spot(chief) && d_finite(image_n) &&
                image_n > kDeviceEps) {
                const double chief_norm =
                    sqrt(chief.l * chief.l + chief.m * chief.m + chief.n * chief.n);
                for (int gi = 0; gi < 4; ++gi) {
                    const double px = gi == 0 ? 1.0 : gi == 1 ? -1.0 : 0.0;
                    const double py = gi == 2 ? 1.0 : gi == 3 ? -1.0 : 0.0;
                    DeviceRay marginal = d_generate_supported_ray(
                        surfaces,
                        cfg,
                        field,
                        px,
                        py,
                        1.0,
                        wave.value_um,
                        calibrated_model_ptr);
                    d_trace_ray(surfaces, cfg, &marginal);
                    const double marginal_norm = sqrt(
                        marginal.l * marginal.l + marginal.m * marginal.m +
                        marginal.n * marginal.n);
                    if (!d_ray_usable_for_spot(marginal) ||
                        !(chief_norm > kDeviceEps) ||
                        !(marginal_norm > kDeviceEps)) {
                        continue;
                    }
                    const double cosine = fmax(
                        -1.0,
                        fmin(
                            1.0,
                            (chief.l * marginal.l + chief.m * marginal.m +
                             chief.n * marginal.n) /
                                (chief_norm * marginal_norm)));
                    stats.working_na_squared_sum +=
                        image_n * image_n * fmax(0.0, 1.0 - cosine * cosine);
                    ++stats.working_valid_marginal_rays;
                }
            }
        }
        if (cfg.require_chief_ray) {
            const DeviceField& chief_field = field;
            DeviceRay chief = cfg.aiming_mode == 5
                                  ? d_generate_calibrated_chief_ray(
                                        surfaces, cfg, chief_field, 1.0, wave.value_um,
                                        calibrated_model_ptr)
                                  : d_generate_supported_ray(
                                        surfaces,
                                        cfg,
                                        chief_field,
                                        0.0,
                                        0.0,
                                        1.0,
                                        wave.value_um,
                                        nullptr);
            d_trace_ray(surfaces, cfg, &chief);
            chief.nominal_weight *= wave.weight;
            chief.intensity *= wave.weight;
            chief_progress_sum += wave.weight * d_ray_progress(chief, cfg.surface_count);
            chief_weight_sum += wave.weight;
            if (!d_ray_usable_for_spot(chief) && !d_normal_secondary_blockage(chief)) {
                stats.chief_ok = 0;
            }
        }
        for (int pi = 0; pi < pupil_count; ++pi) {
            double px = 0.0;
            double py = 0.0;
            double pw = 1.0;
            d_field_pupil_at(cfg, field, pi, &px, &py, &pw);
            DeviceRay ray = d_generate_supported_ray(
                surfaces,
                cfg,
                field,
                px,
                py,
                pw,
                wave.value_um,
                calibrated_model_ptr);
            d_trace_ray(surfaces, cfg, &ray);
            ray.nominal_weight *= wave.weight;
            ray.intensity *= wave.weight;
            const double weight = d_ray_nominal_weight(ray);
            if (d_calibrated_paraxial_stop_sampling_rejection(
                    ray, surfaces, cfg)) {
                stats.sampling_rejected_weight += weight;
                ++stats.sampling_rejected_rays;
                continue;
            }
            if (collect_illumination_proxy &&
                wi == cfg.primary_wave_index) {
                d_accumulate_illumination_fit(
                    &illumination_fit,
                    field,
                    px,
                    py,
                    weight,
                    ray.intensity,
                    ray.l,
                    ray.m,
                    ray.valid ? 1 : 0);
            }
            stats.single_ray_guard_surface_order_edge_deficit = fmax(
                stats.single_ray_guard_surface_order_edge_deficit,
                d_backward_surface_order_edge_deficit(ray, cfg));
            stats.total_weight += weight;
            progress_sum += weight * d_ray_progress(ray, cfg.surface_count);
            ++stats.attempted_rays;
            if (!d_ray_aim_usable_for_spot(ray)) {
                stats.aim_penalty += d_ray_failure_penalty(ray, cfg);
            }
            if (ray.backward_propagation) {
                stats.backward_weight += weight;
            }
            if (!d_ray_usable_for_spot(ray)) {
                stats.failed_weight += weight;
                stats.trace_penalty += d_ray_failure_penalty(ray, cfg);
                ++stats.failed_rays;
                continue;
            }
            sx += weight * ray.x;
            sy += weight * ray.y;
            stats.valid_weight += weight;
            ++stats.valid_rays;
            if (sample_count < kMaxDeviceSpotSamples) {
                sample_x[sample_count] = ray.x;
                sample_y[sample_count] = ray.y;
                sample_weight[sample_count] = weight;
                sample_wave[sample_count] = wi;
                ++sample_count;
            }
        }
        if (cfg.operand_single_ray_spot_guard || cfg.operand_single_ray_spot_tail) {
            const bool selective_extra =
                d_uses_selective_spot_tail_edge_sampling(cfg, field);
            const int guard_limit =
                4 + (selective_extra ? kSpotTailExtraEdgeCount : 0);
            for (int gi = 0; gi < guard_limit; ++gi) {
                double px = gi == 0 ? 1.0 : gi == 1 ? -1.0 : 0.0;
                double py = gi == 2 ? 1.0 : gi == 3 ? -1.0 : 0.0;
                if (gi >= 4) {
                    d_spot_tail_extra_edge_pupil_at(gi - 4, &px, &py);
                }
                DeviceRay guard = d_generate_supported_ray(
                    surfaces,
                    cfg,
                    field,
                    px,
                    py,
                    1.0,
                    wave.value_um,
                    calibrated_model_ptr, true);
                d_trace_ray(surfaces, cfg, &guard);
                guard.nominal_weight *= wave.weight;
                guard.intensity *= wave.weight;
                if (d_normal_secondary_blockage(guard)) continue;
                stats.single_ray_guard_surface_order_edge_deficit = fmax(
                    stats.single_ray_guard_surface_order_edge_deficit,
                    d_backward_surface_order_edge_deficit(guard, cfg));
                if (!d_ray_usable_for_spot(guard)) {
                    const bool soft_paraxial_edge_guard =
                        cfg.aiming_mode == 1 || cfg.aiming_mode == 5;
                    const bool stop_inside =
                        d_guard_ray_reached_inside_physical_stop(
                            surfaces,
                            cfg,
                            wave.value_um,
                            guard.stop_reached,
                            guard.stop_x,
                            guard.stop_y);
                    if (soft_paraxial_edge_guard && stop_inside) {
                        const double progress =
                            d_ray_progress(guard, cfg.surface_count);
                        const double failure_strength =
                            1.0 + fmax(0.0, 1.0 - progress);
                        stats.single_ray_spot_guard_failure_squared +=
                            failure_strength * failure_strength;
                    }
                } else if (
                    guard_count < kMaxDeviceWaves * kMaxSpotGuardCount) {
                    guard_x[guard_count] = guard.x;
                    guard_y[guard_count] = guard.y;
                    ++guard_count;
                }
            }
        }
    }
    stats.chief_progress =
        chief_weight_sum > 0.0 ? chief_progress_sum / chief_weight_sum : 1.0;
    stats.trace_progress =
        stats.total_weight > 0.0
            ? progress_sum / stats.total_weight
            : (stats.sampling_rejected_rays > 0 ? 1.0 : 0.0);
    if (collect_illumination_proxy) {
        stats.relative_illumination_proxy_area =
            d_finalize_illumination_fit(illumination_fit, field);
    }
    if (stats.valid_weight <= 0.0 || stats.valid_rays == 0) {
        stats.rms_radius = NAN;
        stats.max_radius = NAN;
        return stats;
    }
    const double cx = sx / stats.valid_weight;
    const double cy = sy / stats.valid_weight;
    stats.centroid_x = cx;
    stats.centroid_y = cy;
    double sum_r2 = 0.0;
    double max_radius = 0.0;
    for (int i = 0; i < sample_count; ++i) {
        const double dx = sample_x[i] - cx;
        const double dy = sample_y[i] - cy;
        const double r = sqrt(dx * dx + dy * dy);
        sum_r2 += sample_weight[i] * r * r;
        max_radius = fmax(max_radius, r);
    }
    for (int i = 0; i < guard_count; ++i) {
        const double dx = guard_x[i] - cx;
        const double dy = guard_y[i] - cy;
        max_radius = fmax(max_radius, sqrt(dx * dx + dy * dy));
    }
    stats.rms_radius = sqrt(sum_r2 / stats.valid_weight);
    stats.max_radius = max_radius;
    double best_wave_rms = INFINITY;
    double worst_wave_rms = 0.0;
    int active_wave_count = 0;
    int valid_wave_count = 0;
    for (int wi = 0; wi < cfg.wavelength_count; ++wi) {
        if (waves[wi].weight <= 0.0) {
            continue;
        }
        ++active_wave_count;
        double wave_weight = 0.0;
        double wave_sx = 0.0;
        double wave_sy = 0.0;
        for (int i = 0; i < sample_count; ++i) {
            if (sample_wave[i] != wi) {
                continue;
            }
            wave_weight += sample_weight[i];
            wave_sx += sample_weight[i] * sample_x[i];
            wave_sy += sample_weight[i] * sample_y[i];
        }
        if (wave_weight <= 0.0) {
            continue;
        }
        const double wave_cx = wave_sx / wave_weight;
        const double wave_cy = wave_sy / wave_weight;
        double wave_sum_r2 = 0.0;
        for (int i = 0; i < sample_count; ++i) {
            if (sample_wave[i] != wi) {
                continue;
            }
            const double dx = sample_x[i] - wave_cx;
            const double dy = sample_y[i] - wave_cy;
            wave_sum_r2 += sample_weight[i] * (dx * dx + dy * dy);
        }
        const double wave_rms = sqrt(wave_sum_r2 / wave_weight);
        best_wave_rms = fmin(best_wave_rms, wave_rms);
        worst_wave_rms = fmax(worst_wave_rms, wave_rms);
        ++valid_wave_count;
    }
    if (active_wave_count > 0 && valid_wave_count == active_wave_count) {
        stats.worst_wavelength_rms_radius = worst_wave_rms;
        stats.spectral_rms_spread = fmax(0.0, worst_wave_rms - best_wave_rms);
    }
    return stats;
}
} // namespace
} // namespace optiland
