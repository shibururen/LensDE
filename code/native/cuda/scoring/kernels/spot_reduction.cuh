#pragma once
#include "../prelude.cuh"
#include "../aiming/chromatic_anchor.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../contracts/spot_wavefront_records.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/first_order_metrics.cuh"
#include "../geometry/material_and_sag.cuh"
#include "../host/certificate_context.cuh"
#include "../merit/first_order_constraints.cuh"
#include "../merit/residual_primitives.cuh"
#include "../merit/spot_statistics.cuh"
#include "../ray/structural_trace.cuh"
#include "../sampling/pupil_nodes.cuh"
#include "../sampling/symmetry_mapping.cuh"

namespace optiland {
namespace {
__global__ void reduce_paraxial_spot_fields_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    DeviceConfig cfg,
    int population_size,
    int pupil_count,
    const DeviceSpotRaySample* ray_samples,
    const DeviceChiefRaySample* chief_samples,
    const double* pupil_order_heights,
    const double* meridional_support_heights,
    DeviceFieldSpotStats* field_stats, int evaluation_phase=0) {
    const int item = blockIdx.x * blockDim.x + threadIdx.x;
    const int item_count = population_size * cfg.field_count;
    if (item >= item_count) {
        return;
    }
    const int row = item / cfg.field_count;
    fields = d_candidate_row_fields(fields, cfg, row);
    const int field_index = item - row * cfg.field_count;
    // Clear every field of a newly certified row. Downstream consumers either
    // skip it or read defined empty statistics; no uninitialized history reads.
    if (evaluation_skip_outer_row(row)) { field_stats[item]=DeviceFieldSpotStats{}; return; }
    if ((evaluation_phase==1 && field_index<cfg.field_count-3) ||
        (evaluation_phase==2 && field_index>=cfg.field_count-3)) return;
    const bool off_axis_field = d_field_is_off_axis(fields[field_index]);
    const int active_pupil_count =
        d_field_pupil_count(cfg, fields[field_index]);
    const DeviceSurface* row_surfaces =
        candidate_surfaces + static_cast<long long>(row) * cfg.surface_count;
    const int stop_surface_index = d_stop_index(row_surfaces, cfg);
    const long long field_wave_base =
        (static_cast<long long>(row) * cfg.field_count + field_index) *
        cfg.wavelength_count;
    DeviceFieldSpotStats stats{};
    double sx = 0.0;
    double sy = 0.0;
    double progress_sum = 0.0;
    double chief_progress_sum = 0.0;
    double chief_weight_sum = 0.0;
    if (d_finite(cfg.minimum_relative_illumination) &&
        cfg.minimum_relative_illumination > 0.0 &&
        cfg.minimum_relative_illumination <= 1.0 &&
        cfg.primary_wave_index >= 0 &&
        cfg.primary_wave_index < cfg.wavelength_count) {
        DeviceIlluminationFit illumination_fit{};
        const long long primary_sample_base =
            (field_wave_base + cfg.primary_wave_index) * pupil_count;
        for (int pi = 0; pi < active_pupil_count; ++pi) {
            const DeviceSpotRaySample sample =
                ray_samples[primary_sample_base + pi];
            if (d_calibrated_paraxial_stop_sampling_rejection(
                    sample, cfg, stop_surface_index)) {
                continue;
            }
            d_accumulate_illumination_fit(
                &illumination_fit,
                fields[field_index],
                sample.pupil_x,
                sample.pupil_y,
                sample.weight,
                sample.intensity,
                sample.l,
                sample.m,
                sample.trace_valid);
        }
        stats.relative_illumination_proxy_area =
            d_finalize_illumination_fit(
                illumination_fit,
                fields[field_index]);
    }
    for (int wi = 0; wi < cfg.wavelength_count; ++wi) {
        if (waves[wi].weight <= 0.0) {
            continue;
        }
        if (cfg.require_chief_ray) {
            const DeviceChiefRaySample chief =
                chief_samples[field_wave_base + wi];
            chief_progress_sum += chief.weighted_progress;
            chief_weight_sum += chief.wave_weight;
            if (!chief.usable) {
                stats.chief_ok = 0;
            }
        }
        if (cfg.reject_ray_path_structural_failures && off_axis_field) {
            const DeviceChiefRaySample chief =
                chief_samples[field_wave_base + wi];
            stats.axis_cross_count =
                max(stats.axis_cross_count, chief.axis_cross_count);
            stats.axis_cross_excess = max(
                stats.axis_cross_excess,
                chief.axis_cross_count - cfg.axis_cross_allow_count);
            if (chief.first_surface_valid) {
                const int height_sign = d_axis_sign(
                    chief.first_surface_axis,
                    fmax(0.0, cfg.axis_crossing_epsilon));
                const int direction_sign = d_axis_sign(
                    chief.input_axis,
                    fmax(0.0, cfg.axis_crossing_epsilon));
                if (height_sign != 0 && direction_sign != 0 &&
                    height_sign == direction_sign) {
                    stats.chief_entry_side_failed = 1;
                }
            }
        }
        const long long sample_base =
            (field_wave_base + wi) * pupil_count;
        for (int pi = 0; pi < active_pupil_count; ++pi) {
            const DeviceSpotRaySample sample = ray_samples[sample_base + pi];
            if (d_calibrated_paraxial_stop_sampling_rejection(
                    sample, cfg, stop_surface_index)) {
                stats.sampling_rejected_weight += sample.weight;
                ++stats.sampling_rejected_rays;
                continue;
            }
            stats.single_ray_guard_surface_order_edge_deficit = fmax(
                stats.single_ray_guard_surface_order_edge_deficit,
                d_backward_surface_order_edge_deficit(sample, cfg));
            stats.total_weight += sample.weight;
            progress_sum += sample.weighted_progress;
            ++stats.attempted_rays;
            if (sample.backward) {
                stats.backward_weight += sample.weight;
            }
            stats.aim_penalty += sample.aim_penalty;
            if (!sample.usable) {
                stats.failed_weight += sample.weight;
                stats.trace_penalty += sample.trace_penalty;
                ++stats.failed_rays;
                continue;
            }
            sx += sample.weight * sample.x;
            sy += sample.weight * sample.y;
            stats.valid_weight += sample.weight;
            ++stats.valid_rays;
        }
    }
    stats.trace_progress = stats.total_weight > 0.0
                               ? progress_sum / stats.total_weight
                               : (stats.sampling_rejected_rays > 0 ? 1.0 : 0.0);
    stats.chief_progress = chief_weight_sum > 0.0
                               ? chief_progress_sum / chief_weight_sum
                               : 1.0;
    if (cfg.reject_ray_path_structural_failures && !off_axis_field) {
        double min_abs_py = INFINITY;
        for (int wi = 0; wi < cfg.wavelength_count; ++wi) {
            if (waves[wi].weight <= 0.0) {
                continue;
            }
            const long long sample_base = (field_wave_base + wi) * pupil_count;
            for (int pi = 0; pi < active_pupil_count; ++pi) {
                const DeviceSpotRaySample sample = ray_samples[sample_base + pi];
                if (sample.usable) {
                    min_abs_py = fmin(min_abs_py, fabs(sample.pupil_y));
                }
            }
        }
        if (d_finite(min_abs_py)) {
            const double meridional_tol = min_abs_py + 1.0e-8;
            double max_abs_px = 0.0;
            for (int wi = 0; wi < cfg.wavelength_count; ++wi) {
                if (waves[wi].weight <= 0.0) {
                    continue;
                }
                const long long sample_base =
                    (field_wave_base + wi) * pupil_count;
                for (int pi = 0; pi < active_pupil_count; ++pi) {
                    const DeviceSpotRaySample sample = ray_samples[sample_base + pi];
                    if (sample.usable &&
                        fabs(sample.pupil_y) <= meridional_tol) {
                        max_abs_px = fmax(max_abs_px, fabs(sample.pupil_x));
                    }
                }
            }
            if (max_abs_px > 1.0e-12) {
                const double edge_threshold =
                    d_clamp(cfg.center_early_focus_edge_ratio, 0.0, 1.0) *
                    max_abs_px;
                const double image_z = d_surface_vertex_z(
                    row_surfaces,
                    cfg.surface_count,
                    cfg.surface_count - 1);
                double last_optical_z = NAN;
                for (int si = cfg.surface_count - 1;
                     si >= cfg.first_optical_surface;
                     --si) {
                    if (row_surfaces[si].kind != kKindImage &&
                        row_surfaces[si].kind != kKindObject) {
                        last_optical_z = d_surface_vertex_z(
                            row_surfaces,
                            cfg.surface_count,
                            si);
                        break;
                    }
                }
                const double guard =
                    fmax(0.0, cfg.center_early_focus_guard_mm);
                for (int wi = 0; wi < cfg.wavelength_count; ++wi) {
                    if (waves[wi].weight <= 0.0) {
                        continue;
                    }
                    const long long sample_base =
                        (field_wave_base + wi) * pupil_count;
                    for (int pi = 0; pi < active_pupil_count; ++pi) {
                        const DeviceSpotRaySample sample =
                            ray_samples[sample_base + pi];
                        if (!sample.usable ||
                            fabs(sample.pupil_y) > meridional_tol ||
                            fabs(sample.pupil_x) + 1.0e-12 < edge_threshold) {
                            continue;
                        }
                        stats.axis_cross_count = max(
                            stats.axis_cross_count,
                            sample.axis_cross_count);
                        const bool early_inside_lens_train =
                            sample.has_axis_cross && d_finite(last_optical_z) &&
                            sample.first_cross_z < last_optical_z - guard;
                        const bool early_without_lens_train_reference =
                            sample.has_axis_cross && !d_finite(last_optical_z) &&
                            d_finite(image_z) &&
                            sample.first_cross_z < image_z - guard;
                        if (early_inside_lens_train ||
                            early_without_lens_train_reference) {
                            stats.center_early_focus = 1;
                        }
                    }
                }
                stats.axis_cross_excess = max(
                    0,
                    stats.axis_cross_count - cfg.axis_cross_allow_count);
            }
        }
    }
    if (pupil_order_heights && cfg.pupil_order_prior_weight > 0.0) {
        const DeviceField order_field = fields[field_index];
        const double field_radius = hypot(order_field.hx, order_field.hy);
        if (field_radius > 1.0e-12 &&
            field_radius + 1.0e-12 >= cfg.pupil_order_min_field_radius) {
            constexpr int kMaxPupilOrderSamples = 32;
            int order_indices[kMaxPupilOrderSamples];
            int order_sources[kMaxPupilOrderSamples];
            long long order_support_bases[kMaxPupilOrderSamples];
            double entry_heights[kMaxPupilOrderSamples];
            int order_count = 0;
            const double ux = order_field.hx / field_radius;
            const double uy = order_field.hy / field_radius;
            const int last_surface =
                d_pupil_order_last_optical_surface(row_surfaces, cfg);
            const int physical_pupil_count =
                d_field_physical_pupil_count(cfg, order_field);
            const long long order_field_base =
                (static_cast<long long>(row) * cfg.field_count + field_index) *
                pupil_count * cfg.surface_count;
            for (int pi = 0;
                 pi < physical_pupil_count &&
                 order_count < kMaxPupilOrderSamples;
                 ++pi) {
                double px = 0.0;
                double py = 0.0;
                double pw = 0.0;
                d_field_physical_pupil_at(
                    cfg, order_field, pi, &px, &py, &pw);
                const double sagittal = -px * uy + py * ux;
                if (fabs(sagittal) > 1.0e-10) {
                    continue;
                }
                const long long ray_base =
                    order_field_base +
                    static_cast<long long>(pi) * cfg.surface_count;
                const double entry = pupil_order_heights[
                    ray_base + cfg.first_optical_surface];
                if (!d_finite(entry)) {
                    continue;
                }
                int insertion = order_count;
                while (insertion > 0 &&
                       entry_heights[insertion - 1] > entry) {
                    entry_heights[insertion] = entry_heights[insertion - 1];
                    order_indices[insertion] = order_indices[insertion - 1];
                    order_sources[insertion] = order_sources[insertion - 1];
                    order_support_bases[insertion] =
                        order_support_bases[insertion - 1];
                    --insertion;
                }
                entry_heights[insertion] = entry;
                order_indices[insertion] = pi;
                order_sources[insertion] = 0;
                order_support_bases[insertion] = 0;
                ++order_count;
            }
            // The aperture Guard kernel already traces exact Py=-1, the
            // retained pupil center, and Py=+1.  For y-meridional fields merge
            // those signed histories into the order sequence without adding
            // them to the RMS quadrature or launching duplicate device rays.
            if (meridional_support_heights && fabs(ux) <= 1.0e-10 &&
                fabs(uy) > 1.0e-12) {
                const int exact_traces[3] = {
                    0, kMeridionalChiefTrace, 1};
                for (int exact = 0;
                     exact < 3 && order_count < kMaxPupilOrderSamples;
                     ++exact) {
                    const long long support_base =
                        ((((static_cast<long long>(row) * cfg.field_count +
                            field_index) *
                               cfg.wavelength_count +
                           cfg.primary_wave_index) *
                              kMeridionalSupportTraceCount +
                          kMeridionalSignedTraceOffset +
                          exact_traces[exact]) *
                         cfg.surface_count);
                    const double entry =
                        meridional_support_heights[
                            support_base + cfg.first_optical_surface] *
                        uy;
                    if (!d_finite(entry)) {
                        continue;
                    }
                    int insertion = order_count;
                    while (insertion > 0 &&
                           entry_heights[insertion - 1] > entry) {
                        entry_heights[insertion] =
                            entry_heights[insertion - 1];
                        order_indices[insertion] =
                            order_indices[insertion - 1];
                        order_sources[insertion] =
                            order_sources[insertion - 1];
                        order_support_bases[insertion] =
                            order_support_bases[insertion - 1];
                        --insertion;
                    }
                    entry_heights[insertion] = entry;
                    order_indices[insertion] = -1;
                    order_sources[insertion] = 1;
                    order_support_bases[insertion] = support_base;
                    ++order_count;
                }
            }
            if (order_count >= 3 &&
                last_surface > cfg.first_optical_surface) {
                double min_jacobian = INFINITY;
                double worst_progress = NAN;
                double max_violation = 0.0;
                bool hard_fold = false;
                const double soft_tolerance =
                    fmax(1.0e-12, cfg.pupil_order_soft_tolerance);
                const int hard_required =
                    max(1, cfg.pupil_order_hard_consecutive_surfaces);
                for (int pair = 0; pair + 1 < order_count; ++pair) {
                    const double denominator =
                        entry_heights[pair + 1] - entry_heights[pair];
                    if (!(denominator > 1.0e-12) ||
                        !d_finite(denominator)) {
                        continue;
                    }
                    const long long lower_base =
                        order_field_base +
                        static_cast<long long>(order_indices[pair]) *
                            cfg.surface_count;
                    const long long upper_base =
                        order_field_base +
                        static_cast<long long>(order_indices[pair + 1]) *
                            cfg.surface_count;
                    int hard_streak = 0;
                    for (int si = cfg.first_optical_surface + 1;
                         si <= last_surface;
                         ++si) {
                        const double lower_height =
                            order_sources[pair] == 0
                                ? pupil_order_heights[lower_base + si]
                                : meridional_support_heights[
                                      order_support_bases[pair] + si] *
                                      uy;
                        const double upper_height =
                            order_sources[pair + 1] == 0
                                ? pupil_order_heights[upper_base + si]
                                : meridional_support_heights[
                                      order_support_bases[pair + 1] + si] *
                                      uy;
                        if (!d_finite(lower_height) ||
                            !d_finite(upper_height)) {
                            hard_streak = 0;
                            continue;
                        }
                        const double jacobian =
                            (upper_height - lower_height) / denominator;
                        const double progress =
                            static_cast<double>(
                                si - cfg.first_optical_surface) /
                            static_cast<double>(
                                last_surface - cfg.first_optical_surface);
                        if (jacobian < min_jacobian) {
                            min_jacobian = jacobian;
                            worst_progress = progress;
                        }
                        const double early_weight =
                            (1.0 - progress) * (1.0 - progress);
                        max_violation = fmax(
                            max_violation,
                            early_weight *
                                fmax(
                                    0.0,
                                    cfg.pupil_order_jacobian_floor - jacobian) /
                                soft_tolerance);
                        if (progress <=
                                cfg.pupil_order_hard_max_progress + 1.0e-12 &&
                            jacobian < cfg.pupil_order_hard_jacobian) {
                            ++hard_streak;
                            if (hard_streak >= hard_required) {
                                hard_fold = true;
                            }
                        } else {
                            hard_streak = 0;
                        }
                    }
                }
                if (d_finite(min_jacobian)) {
                    stats.min_pupil_order_jacobian = min_jacobian;
                    stats.pupil_order_worst_surface_progress = worst_progress;
                    stats.pupil_order_prior_violation = max_violation;
                    stats.pupil_order_fold =
                        min_jacobian < cfg.pupil_order_jacobian_floor ? 1 : 0;
                    stats.pupil_order_hard_fold = hard_fold ? 1 : 0;
                }
            }
        }
    }
    // Pupil-order fold flags are diagnostic-only; their soft residual is
    // accumulated in merit. Preserve the unrelated structural hard guards.
    // A vignetted chief's entry side is also diagnostic-only when vignetting
    // optimization is enabled; its pupil center can legitimately shift sides.
    stats.structural_failure =
        (stats.axis_cross_excess > 0 || stats.center_early_focus != 0 ||
         (cfg.reject_chief_entry_side && stats.chief_entry_side_failed != 0))
            ? 1
            : 0;
    if (stats.valid_weight <= 0.0 || stats.valid_rays == 0) {
        stats.rms_radius = NAN;
        stats.max_radius = NAN;
        field_stats[item] = stats;
        return;
    }
    const double cx = sx / stats.valid_weight;
    const double cy = sy / stats.valid_weight;
    stats.centroid_x = cx;
    stats.centroid_y = cy;
    double sum_r2 = 0.0;
    double max_radius = 0.0;
    for (int wi = 0; wi < cfg.wavelength_count; ++wi) {
        if (waves[wi].weight <= 0.0) {
            continue;
        }
        const long long sample_base =
            (field_wave_base + wi) * pupil_count;
        for (int pi = 0; pi < active_pupil_count; ++pi) {
            const DeviceSpotRaySample sample = ray_samples[sample_base + pi];
            if (!sample.usable) {
                continue;
            }
            const double dx = sample.x - cx;
            const double dy = sample.y - cy;
            const double radius = sqrt(dx * dx + dy * dy);
            sum_r2 += sample.weight * radius * radius;
            max_radius = fmax(max_radius, radius);
        }
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
        const long long sample_base = (field_wave_base + wi) * pupil_count;
        double wave_weight = 0.0;
        double wave_sx = 0.0;
        double wave_sy = 0.0;
        for (int pi = 0; pi < active_pupil_count; ++pi) {
            const DeviceSpotRaySample sample = ray_samples[sample_base + pi];
            if (!sample.usable) {
                continue;
            }
            wave_weight += sample.weight;
            wave_sx += sample.weight * sample.x;
            wave_sy += sample.weight * sample.y;
        }
        if (wave_weight <= 0.0) {
            continue;
        }
        const double wave_cx = wave_sx / wave_weight;
        const double wave_cy = wave_sy / wave_weight;
        double wave_sum_r2 = 0.0;
        for (int pi = 0; pi < active_pupil_count; ++pi) {
            const DeviceSpotRaySample sample = ray_samples[sample_base + pi];
            if (!sample.usable) {
                continue;
            }
            const double dx = sample.x - wave_cx;
            const double dy = sample.y - wave_cy;
            wave_sum_r2 += sample.weight * (dx * dx + dy * dy);
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
    field_stats[item] = stats;
}
} // namespace
} // namespace optiland
