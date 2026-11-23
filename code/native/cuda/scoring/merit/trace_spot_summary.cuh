#pragma once
#include "../prelude.cuh"
#include "../aiming/chromatic_anchor.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../contracts/spot_wavefront_records.cuh"
#include "../geometry/asphere_slope.cuh"
#include "residual_primitives.cuh"
#include "supported_ray_generation.cuh"

namespace optiland {
namespace {
__device__ __noinline__ void d_populate_trace_spot_summary(
    const DeviceFieldSpotStats* field_stats,
    const DeviceField* fields,
    const DeviceConfig& cfg,
    CudaScoreSummary* summary,
    DeviceMeritTerms* terms) {
    if (!summary || !field_stats || !fields || cfg.field_count <= 0) {
        return;
    }
    double spot_sum = 0.0;
    double spot_count = 0.0;
    double max_spot = 0.0;
    double max_single_ray_spot = 0.0;
    double max_single_ray_spot_ratio = 0.0;
    bool have_single_ray_spot = false;
    double coverage_total = 0.0;
    double coverage_valid = 0.0;
    double progress_total = 0.0;
    double progress_weight = 0.0;
    double min_field_coverage = INFINITY;
    double min_trace_progress = INFINITY;
    double min_chief_progress = INFINITY;
    int chief_failed = 0;
    int zero_coverage_off_axis = 0;
    int spot_failed = 0;
    summary->min_relative_illumination_proxy = NAN;
    summary->min_pupil_order_jacobian = NAN;
    summary->pupil_order_worst_surface_progress = NAN;
    const bool relative_illumination_enabled =
        d_finite(cfg.minimum_relative_illumination) &&
        cfg.minimum_relative_illumination > 0.0 &&
        cfg.minimum_relative_illumination <= 1.0;
    const int center_field_index =
        d_center_field_index(fields, cfg);
    const double reference_illumination_area =
        relative_illumination_enabled && center_field_index >= 0
            ? field_stats[center_field_index]
                  .relative_illumination_proxy_area
            : NAN;
    bool have_off_axis_relative_illumination = false;
    bool invalid_off_axis_relative_illumination = false;
    double supervised_minimum_relative_illumination = NAN;
    const double chromatic_family_budget = d_chromatic_family_budget(cfg);
    const double total_field_weight =
        fmax(1.0e-12, d_total_image_field_weight(fields, cfg));
    for (int fi = 0; fi < cfg.field_count; ++fi) {
        const DeviceField field = fields[fi];
        const double field_weight = d_image_field_weight(cfg, field);
        const DeviceFieldSpotStats stats = field_stats[fi];
        if (relative_illumination_enabled) {
            const double relative_illumination =
                d_finite(reference_illumination_area) &&
                        reference_illumination_area > 0.0 &&
                        d_finite(stats.relative_illumination_proxy_area) &&
                        stats.relative_illumination_proxy_area >= 0.0
                    ? stats.relative_illumination_proxy_area /
                          reference_illumination_area
                    : NAN;
            if (d_finite(relative_illumination)) {
                summary->min_relative_illumination_proxy =
                    d_finite(summary->min_relative_illumination_proxy)
                        ? fmin(
                              summary->min_relative_illumination_proxy,
                              relative_illumination)
                        : relative_illumination;
            }
            if (d_field_is_off_axis(field) && cfg.relative_illumination_field_selected[fi]) {
                have_off_axis_relative_illumination = true;
                if (!d_finite(relative_illumination)) {
                    invalid_off_axis_relative_illumination = true;
                } else {
                    supervised_minimum_relative_illumination =
                        d_finite(supervised_minimum_relative_illumination)
                            ? fmin(supervised_minimum_relative_illumination, relative_illumination)
                            : relative_illumination;
                }
            }
            if (terms &&
                cfg.relative_illumination_operand_weight[fi] > 0.0) {
                const double delta = d_finite(relative_illumination)
                                         ? fmax(
                                               0.0,
                                               cfg.relative_illumination_operand_lower[fi] -
                                                   relative_illumination)
                                         : 1.0e6;
                const double residual = d_equivalent_spot_residual(
                    cfg,
                    delta,
                    fmax(1.0e-9, cfg.quality_relative_illumination_tol));
                terms->relative_illumination_constraint_squared +=
                    cfg.relative_illumination_operand_weight[fi] *
                    residual * residual;
            }
        }
        if (cfg.operand_working_f_number &&
            fi == d_center_field_index(fields, cfg)) {
            summary->working_f_number_valid_marginal_rays =
                stats.working_valid_marginal_rays;
            if (stats.working_valid_marginal_rays == 4 &&
                d_finite(stats.working_na_squared_sum) &&
                stats.working_na_squared_sum > 0.0) {
                summary->image_space_numerical_aperture =
                    sqrt(stats.working_na_squared_sum / 4.0);
                summary->working_f_number =
                    1.0 / (2.0 * summary->image_space_numerical_aperture);
            }
        }
        coverage_total += stats.total_weight;
        coverage_valid += stats.valid_weight;
        progress_total += stats.trace_progress * stats.total_weight;
        progress_weight += stats.total_weight;
        const double coverage =
            stats.total_weight > 0.0
                ? stats.valid_weight / stats.total_weight
                : (stats.sampling_rejected_rays > 0 ? 1.0 : 0.0);
        min_field_coverage = fmin(min_field_coverage, coverage);
        min_trace_progress = fmin(min_trace_progress, stats.trace_progress);
        min_chief_progress = fmin(min_chief_progress, stats.chief_progress);
        summary->failed_ray_weight += stats.failed_weight;
        if (terms) {
            if (stats.backward_weight > 0.0) {
                terms->trace_failure_type_severity += stats.backward_weight;
            }
            const double guard_edge_deficit =
                fmax(0.0, stats.single_ray_guard_surface_order_edge_deficit);
            terms->guard_surface_order_edge_deficit_squared +=
                guard_edge_deficit * guard_edge_deficit;
            const double order_violation =
                fmax(0.0, stats.pupil_order_prior_violation);
            const double order_penalty =
                fmax(0.0, cfg.pupil_order_prior_weight) *
                order_violation * order_violation;
            terms->pupil_order_soft_prior_penalty += order_penalty;
            summary->pupil_order_prior_penalty += order_penalty;
        }
        if (!stats.chief_ok) {
            ++chief_failed;
        }
        if (d_field_is_off_axis(field) && stats.valid_rays == 0) {
            ++zero_coverage_off_axis;
        }
        summary->max_axis_cross_count =
            max(summary->max_axis_cross_count, stats.axis_cross_count);
        if (stats.axis_cross_excess > 0) {
            ++summary->ray_path_axis_cross_fields;
        }
        if (stats.center_early_focus != 0) {
            ++summary->center_early_focus_fields;
        }
        if (stats.chief_entry_side_failed != 0) {
            ++summary->chief_entry_side_failed_fields;
        }
        if (stats.structural_failure != 0) {
            ++summary->ray_path_structural_failure_fields;
        }
        if (d_finite(stats.min_pupil_order_jacobian)) {
            if (!d_finite(summary->min_pupil_order_jacobian) ||
                stats.min_pupil_order_jacobian <
                    summary->min_pupil_order_jacobian) {
                summary->min_pupil_order_jacobian =
                    stats.min_pupil_order_jacobian;
                summary->pupil_order_worst_surface_progress =
                    stats.pupil_order_worst_surface_progress;
            }
        }
        if (stats.pupil_order_fold != 0) {
            ++summary->pupil_order_fold_fields;
        }
        if (stats.pupil_order_hard_fold != 0) {
            ++summary->pupil_order_hard_fold_fields;
        }
        const bool spot_usable =
            d_finite(stats.rms_radius) &&
            coverage >= cfg.min_coverage_for_image_merit;
        if (spot_usable) {
            spot_sum += stats.rms_radius;
            spot_count += 1.0;
            max_spot = fmax(max_spot, stats.rms_radius);
            if (d_finite(stats.max_radius)) {
                const double ratio =
                    stats.max_radius / fmax(1.0e-12, fabs(stats.rms_radius));
                double guard_ratio = ratio;
                if (stats.single_ray_spot_guard_failure_squared > 0.0) {
                    const double upper =
                        d_finite(cfg.single_ray_spot_guard_upper)
                            ? fmax(0.0, cfg.single_ray_spot_guard_upper)
                            : 10.0;
                    const double measured_excess = fmax(0.0, ratio - upper);
                    const double combined_excess = hypot(
                        measured_excess,
                        sqrt(stats.single_ray_spot_guard_failure_squared));
                    guard_ratio = upper + combined_excess;
                }
                max_single_ray_spot = fmax(max_single_ray_spot, stats.max_radius);
                max_single_ray_spot_ratio = fmax(max_single_ray_spot_ratio, ratio);
                have_single_ray_spot = true;
                if (terms && cfg.operand_single_ray_spot_guard) {
                    const double base =
                        field_weight * fabs(cfg.unified_image_mean_spot_weight);
                    if (base > 0.0) {
                        const double delta = d_upper_delta(
                            guard_ratio,
                            cfg.single_ray_spot_guard_upper);
                        terms->image_sum +=
                            base * d_pseudo_huber(delta, cfg.unified_loss_delta);
                        terms->image_weight += base;
                    }
                }
                if (terms && cfg.operand_single_ray_spot_tail) {
                    const double base =
                        field_weight * fabs(cfg.single_ray_spot_tail_weight);
                    if (base > 0.0) {
                        const double tol = d_image_tolerance(cfg, 0);
                        terms->image_sum +=
                            base * d_pseudo_huber(
                                       stats.max_radius / tol,
                                       cfg.unified_loss_delta);
                        terms->image_weight += base;
                    }
                }
            } else if (cfg.operand_single_ray_spot_guard ||
                       cfg.operand_single_ray_spot_tail) {
                // A non-finite maximum means at least one exact pupil-edge
                // guard ray was unusable.  It is a guard failure, not a sample
                // to omit from the merit.  Match the CPU unified-merit
                // non-finite residual (1e6); using fail_merit as a finite
                // operand value made d_upper_delta produce a 1e12 residual.
                max_single_ray_spot = fmax(max_single_ray_spot, cfg.fail_merit);
                max_single_ray_spot_ratio =
                    fmax(max_single_ray_spot_ratio, cfg.fail_merit);
                have_single_ray_spot = true;
                if (terms) {
                    const double guard_base =
                        cfg.operand_single_ray_spot_guard
                            ? field_weight * fabs(cfg.unified_image_mean_spot_weight)
                            : 0.0;
                    if (guard_base > 0.0) {
                        const double delta = d_upper_delta(
                            NAN,
                            cfg.single_ray_spot_guard_upper);
                        terms->image_sum +=
                            guard_base * d_pseudo_huber(delta, cfg.unified_loss_delta);
                        terms->image_weight += guard_base;
                    }
                    const double tail_base =
                        cfg.operand_single_ray_spot_tail
                            ? field_weight * fabs(cfg.single_ray_spot_tail_weight)
                            : 0.0;
                    if (tail_base > 0.0) {
                        terms->image_sum +=
                            tail_base * d_pseudo_huber(1.0e6, cfg.unified_loss_delta);
                        terms->image_weight += tail_base;
                    }
                }
            }
            if (terms && cfg.operand_rms_spot) {
                const double base =
                    field_weight * fabs(cfg.unified_image_mean_spot_weight);
                if (base > 0.0) {
                    const double tol = d_image_tolerance(cfg, 0);
                    terms->image_sum +=
                        base * d_pseudo_huber(stats.rms_radius / tol, cfg.unified_loss_delta);
                    terms->image_weight += base;
                }
            }
            if (terms && cfg.operand_worst_wavelength_spot) {
                const double base =
                    chromatic_family_budget * field_weight / total_field_weight;
                if (base > 0.0) {
                    const double delta = d_upper_delta(
                        stats.worst_wavelength_rms_radius,
                        cfg.worst_wavelength_spot_upper);
                    terms->image_sum += base * d_pseudo_huber(
                        delta / d_image_tolerance(cfg, 0),
                        cfg.unified_loss_delta);
                    terms->image_weight += base;
                }
            }
            if (terms && cfg.operand_spectral_spot_spread) {
                const double base =
                    chromatic_family_budget * field_weight / total_field_weight;
                if (base > 0.0) {
                    const double delta = d_upper_delta(
                        stats.spectral_rms_spread,
                        cfg.spectral_spot_spread_upper);
                    terms->image_sum += base * d_pseudo_huber(
                        delta / d_image_tolerance(cfg, 5),
                        cfg.unified_loss_delta);
                    terms->image_weight += base;
                }
            }
        } else {
            spot_failed = 1;
        }
    }
    if (terms && relative_illumination_enabled &&
        have_off_axis_relative_illumination) {
        const double delta =
            !invalid_off_axis_relative_illumination &&
                    d_finite(supervised_minimum_relative_illumination)
                ? fmax(
                      0.0,
                      cfg.minimum_relative_illumination -
                          supervised_minimum_relative_illumination)
                : 1.0e6;
        const double residual = d_equivalent_spot_residual(
            cfg,
            delta,
            fmax(1.0e-9, cfg.quality_relative_illumination_tol));
        terms->relative_illumination_constraint_squared +=
            25.0 * residual * residual;
    }
    if (terms && relative_illumination_enabled) {
        double maximum_field_radius = 0.0;
        for (int fi = 0; fi < cfg.field_count; ++fi) {
            maximum_field_radius = fmax(
                maximum_field_radius,
                hypot(fields[fi].hx, fields[fi].hy));
        }
        const double edge_budget = fmin(
            fmin(1.0, fmax(0.0, cfg.vignetting_edge_fvdy_cap)),
            fmin(1.0, fmax(0.0, cfg.vignetting_loss_budget_fraction)) *
                (1.0 - cfg.minimum_relative_illumination));
        for (int fi = 0; fi < cfg.field_count; ++fi) {
            const double radius = hypot(fields[fi].hx, fields[fi].hy);
            if (radius <= kDeviceEps || maximum_field_radius <= kDeviceEps) {
                continue;
            }
            const double magnitude = fmax(0.0, fields[fi].fvdy);
            const double geometry_excess =
                fmax(0.0, fabs(fields[fi].fvcy) - magnitude);
            const double geometry_residual = d_equivalent_spot_residual(
                cfg,
                geometry_excess,
                fmax(1.0e-9, cfg.quality_relative_illumination_tol));
            terms->vignetting_geometry_constraint_squared +=
                fmax(0.0, cfg.vignetting_meridional_excess_weight) *
                geometry_residual * geometry_residual;

            const double rho = fmin(1.0, fmax(0.0, radius / maximum_field_radius));
            const double soft_upper = edge_budget * pow(
                rho,
                fmax(0.0, cfg.vignetting_soft_field_power));
            const double soft_excess = fmax(0.0, magnitude - soft_upper);
            terms->vignetting_soft_prior_penalty +=
                fmax(0.0, cfg.vignetting_soft_prior_weight) *
                soft_excess * soft_excess;
        }
    }
    if (terms && cfg.operand_max_spot) {
        const double base = fabs(cfg.unified_image_max_spot_weight);
        if (base > 0.0) {
            const double tol = d_image_tolerance(cfg, 1);
            terms->image_sum +=
                base * d_pseudo_huber(max_spot / tol, cfg.unified_loss_delta);
            terms->image_weight += base;
        }
    }
    summary->trace_coverage =
        coverage_total > 0.0 ? coverage_valid / coverage_total : 0.0;
    summary->min_field_coverage =
        d_finite(min_field_coverage) ? min_field_coverage : 0.0;
    summary->trace_progress =
        progress_weight > 0.0 ? progress_total / progress_weight : 0.0;
    summary->min_trace_progress =
        d_finite(min_trace_progress) ? min_trace_progress : 0.0;
    summary->min_chief_progress =
        d_finite(min_chief_progress) ? min_chief_progress : 1.0;
    summary->mean_spot = spot_count > 0.0 ? spot_sum / spot_count : NAN;
    summary->max_spot = max_spot;
    summary->max_single_ray_spot =
        have_single_ray_spot ? max_single_ray_spot : NAN;
    summary->max_single_ray_spot_ratio =
        have_single_ray_spot ? max_single_ray_spot_ratio : NAN;
    summary->chief_ray_ok = chief_failed == 0 ? 1 : 0;
    summary->chief_ray_failed_fields = chief_failed;
    summary->zero_coverage_off_axis_fields = zero_coverage_off_axis;
    if (terms) {
        terms->trace_failure_type_severity += summary->failed_ray_weight;
        if (cfg.require_chief_ray && chief_failed > 0) {
            terms->trace_failure_type_severity += static_cast<double>(chief_failed);
        }
    }
    summary->trace_ok =
        (!spot_failed &&
         d_finite(summary->mean_spot) &&
         (!cfg.require_chief_ray || chief_failed == 0) &&
         (!cfg.require_off_axis_field_coverage || zero_coverage_off_axis == 0))
            ? 1
            : 0;
}
} // namespace
} // namespace optiland
