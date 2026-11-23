#pragma once
#include "../prelude.cuh"
#include "../aiming/chromatic_anchor.cuh"
#include "../aiming/edge_scale_solver.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../contracts/spot_wavefront_records.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/material_and_sag.cuh"
#include "../host/certificate_context.cuh"
#include "spot_trace.cuh"
#include "../merit/first_order_constraints.cuh"
#include "../merit/geometry_constraints.cuh"
#include "../merit/residual_primitives.cuh"
#include "../merit/supported_ray_generation.cuh"
#include "../merit/unified_score.cuh"
#include "../ray/paraxial_generation.cuh"
#include "../sampling/symmetry_mapping.cuh"

namespace optiland {
namespace {
// The complete selected-field RMS terms are fixed before edge guard updates.
// Unseen image terms are nonnegative; all possible active term weights form
// a denominator upper bound even when unusable fields omit some terms.
__device__ double evaluation_image_weight_upper(const DeviceField* fields, const DeviceConfig& cfg) {
    double fw=0.0;
    for (int fi=0;fi<cfg.field_count;++fi) {
        const double w=d_image_field_weight(cfg,fields[fi]);
        if (!d_finite(w) || w<0.0) return NAN;
        fw+=w;
    }
    double weight=fw*(
        (cfg.operand_rms_spot?fabs(cfg.unified_image_mean_spot_weight):0.0)+
        (cfg.operand_single_ray_spot_guard?fabs(cfg.unified_image_mean_spot_weight):0.0)+
        (cfg.operand_single_ray_spot_tail?fabs(cfg.single_ray_spot_tail_weight):0.0)+
        (cfg.operand_rms_wavefront?fabs(cfg.unified_image_wavefront_weight):0.0)+
        (cfg.operand_single_ray_wavefront_guard?fabs(cfg.unified_image_wavefront_weight):0.0));
    weight+=(cfg.operand_max_spot?fabs(cfg.unified_image_max_spot_weight):0.0);
    weight+=(cfg.operand_max_distortion?fabs(cfg.unified_image_distortion_weight):0.0);
    weight+=(cfg.operand_max_chief_ray_angle?fabs(cfg.unified_image_cra_weight):0.0);
    weight+=d_chromatic_family_budget(cfg)*d_chromatic_family_count(cfg);
    return d_finite(weight) && weight>0.0 ? weight*(1.0+1.0e-10) : NAN;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void evaluation_certify_outer_rms_kernel(
    const DeviceField* fields,const DeviceWave* waves,
    const DeviceChiefRaySample* chiefs,const DeviceFieldSpotStats* stats,
    DeviceConfig cfg,int population_size,double epsilon) {
    const int row=blockIdx.x*blockDim.x+threadIdx.x;
    if (row>=population_size || !g_evaluation_certificate) return;
    auto& proof=g_evaluation_certificate[row];
    if (!proof.merit_eligible || proof.reason>0) return;
    fields=d_candidate_row_fields(fields,cfg,row);
    const double denominator=evaluation_image_weight_upper(fields,cfg);
    if (!d_finite(denominator) || denominator<=0.0) return;
    double min_chief=1.0;
    if (cfg.require_chief_ray) {
        for (int fi=0;fi<cfg.field_count;++fi) {
            double sum=0.0,weight=0.0;
            for (int wi=0;wi<cfg.wavelength_count;++wi) {
                if (waves[wi].weight<=0.0) continue;
                const auto& chief=chiefs[(static_cast<long long>(row)*cfg.field_count+fi)*cfg.wavelength_count+wi];
                if (!d_finite(chief.weighted_progress) || !d_finite(chief.wave_weight) || chief.wave_weight<0.0) return;
                sum+=chief.weighted_progress;weight+=chief.wave_weight;
            }
            if (!(weight>0.0) || !d_finite(sum)) return;
            min_chief=fmin(min_chief,sum/weight);
        }
    }
    const double trace_gate=
        d_smoothstep(cfg.min_system_coverage-epsilon-1.0e-10,cfg.unified_gate_system_coverage_lo,1.0)*
        d_smoothstep(cfg.min_field_coverage-epsilon-1.0e-10,cfg.unified_gate_min_field_lo,1.0)*
        d_smoothstep(min_chief-1.0e-10,cfg.unified_gate_chief_progress_lo,1.0);
    double rms_sum=0.0;
    for (int fi=cfg.field_count-3;fi<cfg.field_count;++fi) {
        const auto& field_stats=stats[static_cast<long long>(row)*cfg.field_count+fi];
        if (!d_finite(field_stats.rms_radius) || field_stats.rms_radius<0.0) continue;
        const double term=d_image_field_weight(cfg,fields[fi])*fabs(cfg.unified_image_mean_spot_weight)*
            d_pseudo_huber(field_stats.rms_radius/d_image_tolerance(cfg,0),cfg.unified_loss_delta);
        if (!d_finite(term) || term<0.0) return;
        rms_sum+=term;
    }
    const double lower=fmin(cfg.fail_merit,
        fabs(cfg.group_weight_image)*trace_gate*trace_gate*rms_sum/denominator)*(1.0-1.0e-8);
    if (!d_finite(lower) || !d_finite(proof.parent_adjusted_merit)) return;
    proof.prefix_fields=3;
    proof.image_denominator_upper=denominator;
    proof.image_gate_lower=trace_gate*trace_gate;
    proof.rms_sum_lower=rms_sum*(1.0-1.0e-8);
    proof.merit_lower=lower;
    const double margin=1.0e-8*fmax(1.0,fabs(proof.parent_adjusted_merit));
    if (lower>proof.parent_adjusted_merit+margin) proof.reason=3;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void apply_single_ray_spot_guard_fields_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    DeviceConfig cfg,
    int population_size,
    const DeviceSpotRaySample* guard_samples,
    const DeviceChiefRaySample* chief_samples,
    const DeviceSpotRaySample* extra_edge_samples,
    DeviceFieldSpotStats* field_stats) {
    constexpr int kGuardCount = 4;
    const int item = blockIdx.x * blockDim.x + threadIdx.x;
    const int item_count = population_size * cfg.field_count;
    if (item >= item_count) {
        return;
    }
    DeviceFieldSpotStats stats = field_stats[item];
    const long long field_wave_base =
        static_cast<long long>(item) * cfg.wavelength_count;
    const int row = item / cfg.field_count;
    fields = d_candidate_row_fields(fields, cfg, row);
    const int field_index = item - row * cfg.field_count;
    const DeviceSurface* surfaces =
        candidate_surfaces + static_cast<long long>(row) * cfg.surface_count;
    if (cfg.operand_working_f_number && guard_samples && chief_samples &&
        field_index == d_center_field_index(fields, cfg)) {
        for (int wi = 0; wi < cfg.wavelength_count; ++wi) {
            if (waves[wi].weight <= 0.0 ||
                fabs(waves[wi].value_um - cfg.primary_wavelength) > 1.0e-10) {
                continue;
            }
            const DeviceChiefRaySample chief =
                chief_samples[field_wave_base + wi];
            double image_n = NAN;
            for (int si = 1; si < cfg.surface_count; ++si) {
                if (surfaces[si].kind == kKindImage) {
                    image_n = d_surface_refractive_index(
                        surfaces[si - 1], cfg, waves[wi].value_um);
                    break;
                }
            }
            const double chief_norm = sqrt(
                chief.image_l * chief.image_l + chief.image_m * chief.image_m +
                chief.image_n * chief.image_n);
            if (!chief.valid || !(chief_norm > kDeviceEps) ||
                !d_finite(image_n) || !(image_n > kDeviceEps)) {
                break;
            }
            const long long sample_base =
                (field_wave_base + wi) * kGuardCount;
            for (int gi = 0; gi < kGuardCount; ++gi) {
                const DeviceSpotRaySample marginal =
                    guard_samples[sample_base + gi];
                const double marginal_norm = sqrt(
                    marginal.l * marginal.l + marginal.m * marginal.m +
                    marginal.n * marginal.n);
                if (!marginal.usable || !(marginal_norm > kDeviceEps)) {
                    continue;
                }
                const double cosine = fmax(
                    -1.0,
                    fmin(
                        1.0,
                        (chief.image_l * marginal.l +
                         chief.image_m * marginal.m +
                         chief.image_n * marginal.n) /
                            (chief_norm * marginal_norm)));
                stats.working_na_squared_sum +=
                    image_n * image_n * fmax(0.0, 1.0 - cosine * cosine);
                ++stats.working_valid_marginal_rays;
            }
            break;
        }
    }
    if (stats.valid_rays <= 0 || !d_finite(stats.centroid_x) ||
        !d_finite(stats.centroid_y) || !d_finite(stats.max_radius)) {
        field_stats[item] = stats;
        return;
    }
    double max_radius = stats.max_radius;
    // Plain and calibrated paraxial share the same finite failure contract,
    // gated by the actual physical-stop intercept.  A rough pupil mapping that
    // lands outside the stop is omitted from this guard operand.
    const bool soft_paraxial_edge_guard =
        cfg.aiming_mode == 1 || cfg.aiming_mode == 5;
    const bool strict_edge_guard =
        cfg.aiming_mode != 0 && !soft_paraxial_edge_guard;
    bool edge_guard_failed = false;
    for (int wi = 0; wi < cfg.wavelength_count; ++wi) {
        if (waves[wi].weight <= 0.0) {
            continue;
        }
        const long long sample_base =
            (field_wave_base + wi) * kGuardCount;
        for (int gi = 0; gi < kGuardCount; ++gi) {
            const DeviceSpotRaySample sample = guard_samples[sample_base + gi];
            if (sample.normal_aperture_blockage) continue;
            if (!sample.usable) {
                const bool guard_inside_physical_stop =
                    d_guard_ray_reached_inside_physical_stop(
                        surfaces,
                        cfg,
                        waves[wi].value_um,
                        sample.stop_reached,
                        sample.stop_x,
                        sample.stop_y);
                if (guard_inside_physical_stop) {
                    stats.single_ray_guard_surface_order_edge_deficit = fmax(
                        stats.single_ray_guard_surface_order_edge_deficit,
                        d_guard_surface_order_edge_deficit(
                            surfaces,
                            cfg,
                            sample));
                }
                const bool penalize_soft_failure =
                    soft_paraxial_edge_guard &&
                    guard_inside_physical_stop;
                if (penalize_soft_failure) {
                    const double progress = sample.weight > 0.0
                                                ? fmin(
                                                      1.0,
                                                      fmax(
                                                          0.0,
                                                          sample.weighted_progress /
                                                              sample.weight))
                                                : 0.0;
                    const double failure_strength =
                        1.0 + fmax(0.0, 1.0 - progress);
                    stats.single_ray_spot_guard_failure_squared +=
                        failure_strength * failure_strength;
                } else if (strict_edge_guard) {
                    edge_guard_failed = true;
                }
                continue;
            }
            stats.single_ray_guard_surface_order_edge_deficit = fmax(
                stats.single_ray_guard_surface_order_edge_deficit,
                d_backward_surface_order_edge_deficit(sample, cfg));
            const double dx = sample.x - stats.centroid_x;
            const double dy = sample.y - stats.centroid_y;
            const double radius = sqrt(dx * dx + dy * dy);
            if (d_finite(radius)) {
                max_radius = fmax(max_radius, radius);
            } else if (strict_edge_guard) {
                edge_guard_failed = true;
            } else if (soft_paraxial_edge_guard &&
                       d_guard_ray_reached_inside_physical_stop(
                           surfaces,
                           cfg,
                           waves[wi].value_um,
                           sample.stop_reached,
                           sample.stop_x,
                           sample.stop_y)) {
                stats.single_ray_spot_guard_failure_squared += 1.0;
            }
        }
        if (extra_edge_samples &&
            d_uses_selective_spot_tail_edge_sampling(
                cfg, fields[field_index])) {
            const long long extra_sample_base =
                (field_wave_base + wi) * kSpotTailExtraEdgeCount;
            const int symmetry =
                d_field_pupil_symmetry(cfg, fields[field_index]);
            for (int logical_arm = 0;
                 logical_arm < kSpotTailExtraEdgeCount;
                 ++logical_arm) {
                const int representative_arm =
                    d_spot_tail_extra_canonical_arm(logical_arm, symmetry);
                const int transform_code =
                    d_spot_tail_extra_transform_code_between(
                        representative_arm, logical_arm, symmetry);
                DeviceSpotRaySample sample =
                    extra_edge_samples[
                        extra_sample_base + representative_arm];
                if (transform_code > 0) {
                    sample = d_transform_symmetric_spot_sample(
                        sample, transform_code);
                }
                if (sample.normal_aperture_blockage) continue;
                if (!sample.usable) {
                    const bool guard_inside_physical_stop =
                        d_guard_ray_reached_inside_physical_stop(
                            surfaces,
                            cfg,
                            waves[wi].value_um,
                            sample.stop_reached,
                            sample.stop_x,
                            sample.stop_y);
                    if (guard_inside_physical_stop) {
                        stats.single_ray_guard_surface_order_edge_deficit = fmax(
                            stats.single_ray_guard_surface_order_edge_deficit,
                            d_guard_surface_order_edge_deficit(
                                surfaces,
                                cfg,
                                sample));
                    }
                    const bool penalize_soft_failure =
                        soft_paraxial_edge_guard &&
                        guard_inside_physical_stop;
                    if (penalize_soft_failure) {
                        const double progress = sample.weight > 0.0
                                                    ? fmin(
                                                          1.0,
                                                          fmax(
                                                              0.0,
                                                              sample.weighted_progress /
                                                                  sample.weight))
                                                    : 0.0;
                        const double failure_strength =
                            1.0 + fmax(0.0, 1.0 - progress);
                        stats.single_ray_spot_guard_failure_squared +=
                            failure_strength * failure_strength;
                    } else if (strict_edge_guard) {
                        edge_guard_failed = true;
                    }
                    continue;
                }
                stats.single_ray_guard_surface_order_edge_deficit = fmax(
                    stats.single_ray_guard_surface_order_edge_deficit,
                    d_backward_surface_order_edge_deficit(sample, cfg));
                const double dx = sample.x - stats.centroid_x;
                const double dy = sample.y - stats.centroid_y;
                const double radius = sqrt(dx * dx + dy * dy);
                if (d_finite(radius)) {
                    max_radius = fmax(max_radius, radius);
                } else if (strict_edge_guard) {
                    edge_guard_failed = true;
                } else if (soft_paraxial_edge_guard &&
                           d_guard_ray_reached_inside_physical_stop(
                               surfaces,
                               cfg,
                               waves[wi].value_um,
                               sample.stop_reached,
                               sample.stop_x,
                               sample.stop_y)) {
                    stats.single_ray_spot_guard_failure_squared += 1.0;
                }
            }
        }
    }
    stats.max_radius = edge_guard_failed ? NAN : max_radius;
    field_stats[item] = stats;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void chief_metrics_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    const DeviceCalibratedModel* calibrated_models,
    const DeviceChiefRaySample* chief_samples,
    DeviceConfig cfg,
    int population_size,
    CudaScoreSummary* summaries) {
    const int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= population_size) {
        return;
    }
    fields = d_candidate_row_fields(fields, cfg, row);
    const DeviceSurface* surfaces =
        candidate_surfaces + static_cast<long long>(row) * cfg.surface_count;
    const DeviceCalibratedModel* row_models =
        calibrated_models
            ? calibrated_models +
                  static_cast<long long>(row) * cfg.field_count *
                      cfg.wavelength_count
            : nullptr;
    const DeviceChiefRaySample* row_chief_samples =
        chief_samples
            ? chief_samples +
                  static_cast<long long>(row) * cfg.field_count *
                      cfg.wavelength_count
            : nullptr;
    CudaScoreSummary summary = summaries[row];
    d_populate_chief_metrics(
        surfaces,
        fields,
        waves,
        row_models,
        row_chief_samples,
        cfg,
        &summary);
    summaries[row] = summary;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void finalize_merit_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceGlassProximity* glass_proximity,
    const DeviceGlassProximityPoint* glass_proximity_points,
    DeviceConfig cfg,
    int population_size,
    const DeviceMeritTerms* merit_terms,
    CudaScoreSummary* summaries) {
    const int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= population_size) {
        return;
    }
    const DeviceSurface* surfaces =
        candidate_surfaces + static_cast<long long>(row) * cfg.surface_count;
    CudaScoreSummary summary = summaries[row];
    const double glass_penalty = d_glass_proximity_penalty(
        surfaces,
        cfg,
        glass_proximity,
        glass_proximity_points);
    d_apply_unified_merit(
        surfaces,
        cfg,
        merit_terms[row],
        glass_penalty,
        &summary);
    summaries[row] = summary;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
void accumulate_timing(
    CudaEvaluationTiming* timing,
    const CudaEvaluationTiming& delta) {
    if (!timing) {
        return;
    }
    timing->calls += delta.calls;
    timing->candidates += delta.candidates;
    timing->context_seconds += delta.context_seconds;
    timing->host_setup_seconds += delta.host_setup_seconds;
    timing->host_to_device_seconds += delta.host_to_device_seconds;
    timing->kernel_seconds += delta.kernel_seconds;
    timing->device_to_host_seconds += delta.device_to_host_seconds;
    timing->cleanup_seconds += delta.cleanup_seconds;
    timing->total_seconds += delta.total_seconds;
}
} // namespace
} // namespace optiland
