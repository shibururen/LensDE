#pragma once
#include "../prelude.cuh"
#include "../aiming/chromatic_anchor.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../contracts/spot_wavefront_records.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/first_order_metrics.cuh"
#include "../geometry/mechanical_constraints.cuh"
#include "../merit/first_order_constraints.cuh"
#include "../merit/residual_primitives.cuh"
#include "../merit/supported_ray_generation.cuh"
#include "../merit/trace_spot_summary.cuh"

namespace optiland {
namespace {
__device__ void d_accumulate_vignetting_boundary_support(
    const DeviceSurface* surfaces,
    const DeviceField* fields,
    const DeviceWave* waves,
    const DeviceConfig& cfg,
    int row,
    const double* meridional_boundary_heights,
    DeviceMeritTerms* terms) {
    if (!surfaces || !fields || !waves || !meridional_boundary_heights ||
        !terms || !d_finite(cfg.minimum_relative_illumination) ||
        cfg.minimum_relative_illumination <= 0.0 ||
        cfg.minimum_relative_illumination > 1.0 ||
        cfg.vignetting_boundary_support_weight <= 0.0) {
        return;
    }
    const double aperture_margin = fmax(1.0, cfg.clear_aperture_margin);
    const double tolerance = fmax(
        fmax(1.0e-9, cfg.vignetting_boundary_support_tolerance),
        1.0 - 1.0 / aperture_margin);
    const double activation_floor =
        fmax(0.0, cfg.vignetting_boundary_support_activation);
    const double transition =
        fmax(1.0e-9, cfg.vignetting_boundary_support_transition);
    const double selectivity_slope =
        fmax(0.0, cfg.vignetting_boundary_support_selectivity_slope);
    for (int fi = 0; fi < cfg.field_count; ++fi) {
        const double field_radius = hypot(fields[fi].hx, fields[fi].hy);
        if (field_radius <= kDeviceEps) {
            continue;
        }
        const double cuts[kMeridionalBoundaryCount] = {
            fmax(0.0, fields[fi].fvdy + fields[fi].fvcy),
            fmax(0.0, fields[fi].fvdy - fields[fi].fvcy)};
        for (int side = 0; side < kMeridionalBoundaryCount; ++side) {
            const double activation =
                cfg.vignetting_physical_cut_match_enabled != 0
                    ? (cuts[side] > 1.0e-12 ? 1.0 : 0.0)
                    : fmin(
                          1.0,
                          fmax(
                              0.0,
                              (cuts[side] - activation_floor) / transition));
            if (activation <= 0.0) {
                continue;
            }
            double best_deficit = INFINITY;
            const int wi = max(
                0,
                min(cfg.wavelength_count - 1, cfg.primary_wave_index));
            if (cfg.wavelength_count > 0 && waves[wi].weight > 0.0) {
                const long long boundary_base =
                    ((((static_cast<long long>(row) * cfg.field_count + fi) *
                           cfg.wavelength_count +
                       wi) *
                          kMeridionalSupportTraceCount +
                      side) *
                     cfg.surface_count);
                const long long opposite_base =
                    ((((static_cast<long long>(row) * cfg.field_count + fi) *
                           cfg.wavelength_count +
                       wi) *
                          kMeridionalSupportTraceCount +
                      (1 - side)) *
                     cfg.surface_count);
                const long long chief_base =
                    ((((static_cast<long long>(row) * cfg.field_count + fi) *
                           cfg.wavelength_count +
                       wi) *
                          kMeridionalSupportTraceCount +
                      kMeridionalChiefTrace) *
                     cfg.surface_count);
                const long long signed_boundary_base =
                    ((((static_cast<long long>(row) * cfg.field_count + fi) *
                           cfg.wavelength_count +
                       wi) *
                          kMeridionalSupportTraceCount +
                      kMeridionalSignedTraceOffset + side) *
                     cfg.surface_count);
                const long long signed_opposite_base =
                    ((((static_cast<long long>(row) * cfg.field_count + fi) *
                           cfg.wavelength_count +
                       wi) *
                          kMeridionalSupportTraceCount +
                      kMeridionalSignedTraceOffset + (1 - side)) *
                     cfg.surface_count);
                const long long signed_chief_base =
                    ((((static_cast<long long>(row) * cfg.field_count + fi) *
                           cfg.wavelength_count +
                       wi) *
                          kMeridionalSupportTraceCount +
                      kMeridionalSignedTraceOffset +
                      kMeridionalChiefTrace) *
                     cfg.surface_count);
                for (int si = 0; si < cfg.surface_count; ++si) {
                    const DeviceSurface& surface = surfaces[si];
                    if (surface.kind == kKindObject ||
                        surface.kind == kKindImage || surface.is_stop ||
                        surface.vignetting_aperture_excluded ||
                        !d_finite(surface.semi_aperture) ||
                        surface.semi_aperture <= kDeviceEps) {
                        continue;
                    }
                    const double height =
                        meridional_boundary_heights[boundary_base + si];
                    const double opposite_height =
                        meridional_boundary_heights[opposite_base + si];
                    const double chief_height =
                        meridional_boundary_heights[chief_base + si];
                    const double edge_y =
                        meridional_boundary_heights[
                            signed_boundary_base + si];
                    const double chief_y =
                        meridional_boundary_heights[signed_chief_base + si];
                    if (!d_finite(height) || height <= 0.0 ||
                        !d_finite(chief_height) || chief_height < 0.0 ||
                        (cfg.vignetting_boundary_support_contract != 1 &&
                         (!d_finite(opposite_height) ||
                          opposite_height <= 0.0))) {
                        continue;
                    }
                    double evidence_deficit = INFINITY;
                    if (cfg.vignetting_boundary_support_contract == 1) {
                        // With minimum relative illumination enabled the
                        // effective aperture margin is one, so semi_aperture
                        // is the common retained envelope for all fields,
                        // waves and sampled pupil points in this candidate.
                        const double keep_height = surface.semi_aperture;
                        const double owner_gap = fmax(
                            0.0, (keep_height - height) / keep_height);
                        const double retained_span =
                            fmax(1.0e-9, 1.0 - fields[fi].fvdy);
                        const double edge_outward =
                            side == 0 ? -edge_y : edge_y;
                        const double signed_outward_slope =
                            side == 0
                                ? (chief_y - edge_y) / retained_span
                                : (edge_y - chief_y) / retained_span;
                        const double outward_slope =
                            fmax(0.0, signed_outward_slope);
                        const double predicted_outside_height =
                            edge_outward + outward_slope * cuts[side];
                        const double available_trim_window =
                            (predicted_outside_height - keep_height) /
                            keep_height;
                        const double required_trim_window =
                            cfg.vignetting_physical_cut_match_enabled != 0
                                ? 0.01
                                : 0.01 + selectivity_slope * cuts[side];
                        const double ownership_excess =
                            cfg.vignetting_physical_cut_match_enabled != 0
                                ? fmin(
                                      cuts[side],
                                      outward_slope > 1.0e-12
                                          ? fmax(
                                                0.0,
                                                (keep_height - edge_outward) /
                                                    outward_slope)
                                          : cuts[side])
                                : fmax(0.0, owner_gap - tolerance);
                        const double window_excess = fmax(
                            0.0,
                            required_trim_window - available_trim_window);
                        const double physical_side_excess =
                            fmax(0.0, -edge_outward / keep_height);
                        double cumulative_rank_excess = 0.0;
                        for (int sj = cfg.first_optical_surface;
                             sj <= si;
                             ++sj) {
                            const double history_edge_y =
                                meridional_boundary_heights[
                                    signed_boundary_base + sj];
                            const double history_opposite_y =
                                meridional_boundary_heights[
                                    signed_opposite_base + sj];
                            const double history_chief_y =
                                meridional_boundary_heights[
                                    signed_chief_base + sj];
                            if (!d_finite(history_edge_y) ||
                                !d_finite(history_opposite_y) ||
                                !d_finite(history_chief_y)) {
                                cumulative_rank_excess = INFINITY;
                                break;
                            }
                            const double competing_y =
                                side == 0
                                    ? fmin(
                                          history_opposite_y,
                                          history_chief_y)
                                    : fmax(
                                          history_opposite_y,
                                          history_chief_y);
                            const double rank_gap =
                                side == 0
                                    ? history_edge_y - competing_y
                                    : competing_y - history_edge_y;
                            const double history_scale = fmax(
                                1.0e-9,
                                surfaces[sj].semi_aperture);
                            constexpr double kOrderNumericalSlack = 1.0e-6;
                            cumulative_rank_excess = fmax(
                                cumulative_rank_excess,
                                fmax(
                                    0.0,
                                    rank_gap / history_scale -
                                        kOrderNumericalSlack));
                        }
                        const double raw_evidence_deficit = sqrt(
                            ownership_excess * ownership_excess +
                            window_excess * window_excess +
                            physical_side_excess * physical_side_excess +
                            cumulative_rank_excess * cumulative_rank_excess);
                        // Unsupported physical cut is limited to the requested
                        // pupil interval; fixed radial slack must vanish from
                        // the error when that interval tends to zero.
                        evidence_deficit = cfg.vignetting_physical_cut_match_enabled != 0 &&
                                cfg.primary_wavelength_apertures
                            ? fmin(cuts[side], raw_evidence_deficit)
                            : raw_evidence_deficit;
                    } else {
                        const double competing_height =
                            fmax(opposite_height, chief_height);
                        const double edge_gap =
                            fabs(surface.semi_aperture - height) /
                            surface.semi_aperture;
                        const double competing_clearance =
                            (surface.semi_aperture - competing_height) /
                            surface.semi_aperture;
                        const double required_clearance =
                            tolerance + selectivity_slope * cuts[side];
                        const double contact_excess =
                            fmax(0.0, edge_gap - tolerance);
                        const double selectivity_excess = fmax(
                            0.0,
                            required_clearance - competing_clearance);
                        evidence_deficit = hypot(
                            contact_excess, selectivity_excess);
                    }
                    if (d_finite(evidence_deficit)) {
                        best_deficit = fmin(
                            best_deficit, evidence_deficit);
                    }
                }
            }
            if (!d_finite(best_deficit)) {
                best_deficit = 1.0;
            }
            const double weighted_excess = activation * best_deficit;
            const double residual = d_equivalent_spot_residual(
                cfg, weighted_excess, tolerance);
            terms->vignetting_boundary_support_constraint_squared +=
                fmax(0.0, cfg.vignetting_boundary_support_weight) *
                residual * residual;
        }
    }
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void trace_spot_kernel(
    const DeviceSurface* candidate_surfaces,
    const DeviceFieldSpotStats* field_stats,
    const DeviceField* fields,
    const DeviceWave* waves,
    const double* meridional_boundary_heights,
    DeviceConfig cfg,
    int population_size,
    CudaScoreSummary* summaries,
    DeviceMeritTerms* merit_terms) {
    const int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= population_size) {
        return;
    }
    fields = d_candidate_row_fields(fields, cfg, row);
    const DeviceSurface* surfaces =
        candidate_surfaces + static_cast<long long>(row) * cfg.surface_count;
    CudaScoreSummary summary{};
    summary.mean_wavefront_rms = NAN;
    summary.max_wavefront_rms = NAN;
    summary.max_single_ray_wavefront = NAN;
    summary.max_single_ray_wavefront_ratio = NAN;
    summary.working_f_number = NAN;
    summary.image_space_numerical_aperture = NAN;
    summary.working_f_number_valid_marginal_rays = 0;
    summary.efl = d_efl(surfaces, cfg);
    summary.paraxial_axial_color_focus_shift_mm =
        d_paraxial_axial_color_focus_shift(surfaces, waves, cfg);
    summary.ttl = d_total_track(surfaces, cfg);
    summary.bfl = d_bfl(surfaces, cfg);
    summary.min_edge = d_min_edge_thickness(surfaces, cfg);
    DeviceMeritTerms terms{};
    d_populate_trace_spot_summary(
        field_stats + static_cast<long long>(row) * cfg.field_count,
        fields,
        cfg,
        &summary,
        &terms);
    d_accumulate_vignetting_boundary_support(
        surfaces,
        fields,
        waves,
        cfg,
        row,
        meridional_boundary_heights,
        &terms);
    summaries[row] = summary;
    merit_terms[row] = terms;
}
} // namespace
} // namespace optiland
