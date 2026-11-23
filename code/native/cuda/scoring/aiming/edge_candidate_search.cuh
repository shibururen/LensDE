#pragma once
#include "../prelude.cuh"
#include "chief_geometry.cuh"
#include "edge_scale_solver.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/calibration_tasks.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../diagnostics/asphere_profile_records.cuh"
#include "../diagnostics/builder_instrumentation.cuh"
#include "../geometry/asphere_slope.cuh"

namespace optiland {
namespace {
template <
    bool kUsePrecomputedChief,
    int kEdgeCount,
    int kSafeguardedRecoveryMode,
    bool kRepairBuild,
    bool kUsePrecomputedFirstEdge,
    class Capture_remember_scale_hint
>
struct DProbeEdgeCandidateFamilyInputs {
    const DeviceSurface*& surfaces;
    const DeviceConfig& cfg;
    const DeviceField& field;
    double& wavelength_um;
    DeviceCalibratedModel& model;
    DeviceBuilderDiagnostic*& builder_diag;
    const double& rs;
    const double& first_surface_edge_radius;
    const bool& enforce_edge_branch_continuity;
    const double& residual_limit;
    double& max_residual;
    double (&axis_scale_hints)[2];
    double& common_scale_hint;
    Capture_remember_scale_hint& remember_scale_hint;
    int& i;
    const int& axis_index;
    DeviceCalibratedEdgeAnchor& anchor;
    DeviceSeed (&full_candidates)[10];
    int& candidate_count;
    DeviceEdgeStopSample& best_sample;
    DeviceEdgeStopSample& previous_sample;
    DeviceSeed& best_full_seed;
    const bool& use_transverse_center_radius;
    DeviceSeed& canonical_full_seed;
    DeviceAsphereUC5Profile*& uc5_profile;
    const int& uc5_cell;
    bool& cached_seed_traceable;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
template <
    bool kUsePrecomputedChief,
    int kEdgeCount,
    int kSafeguardedRecoveryMode,
    bool kRepairBuild,
    bool kUsePrecomputedFirstEdge,
    class Inputs
>
__device__ __forceinline__ native_detail::PhaseOutcome d_probe_edge_candidate_family(Inputs& inputs) {
    auto& surfaces = inputs.surfaces;
    auto& cfg = inputs.cfg;
    auto& field = inputs.field;
    auto& wavelength_um = inputs.wavelength_um;
    auto& model = inputs.model;
    auto& builder_diag = inputs.builder_diag;
    auto& rs = inputs.rs;
    auto& first_surface_edge_radius = inputs.first_surface_edge_radius;
    auto& enforce_edge_branch_continuity = inputs.enforce_edge_branch_continuity;
    auto& residual_limit = inputs.residual_limit;
    auto& max_residual = inputs.max_residual;
    auto& axis_scale_hints = inputs.axis_scale_hints;
    auto& common_scale_hint = inputs.common_scale_hint;
    auto& remember_scale_hint = inputs.remember_scale_hint;
    auto& i = inputs.i;
    auto& axis_index = inputs.axis_index;
    auto& anchor = inputs.anchor;
    auto& full_candidates = inputs.full_candidates;
    auto& candidate_count = inputs.candidate_count;
    auto& best_sample = inputs.best_sample;
    auto& previous_sample = inputs.previous_sample;
    auto& best_full_seed = inputs.best_full_seed;
    auto& use_transverse_center_radius = inputs.use_transverse_center_radius;
    auto& canonical_full_seed = inputs.canonical_full_seed;
    auto& uc5_profile = inputs.uc5_profile;
    auto& uc5_cell = inputs.uc5_cell;
    auto& cached_seed_traceable = inputs.cached_seed_traceable;
    for (int c = 0; !anchor.valid && !cached_seed_traceable &&
                    c < candidate_count; ++c) {
        DeviceEdgeStopSample candidate_best_sample;
        DeviceEdgeStopSample candidate_previous_sample;
        auto remember_candidate_sample =
            [&](const DeviceEdgeStopSample& sample) {
                if (!sample.valid) {
                    return;
                }
                if (!candidate_best_sample.valid ||
                    sample.residual < candidate_best_sample.residual) {
                    if (candidate_best_sample.valid &&
                        hypot(candidate_best_sample.seed.x - sample.seed.x,
                              candidate_best_sample.seed.y - sample.seed.y) >
                            1.0e-12) {
                        candidate_previous_sample = candidate_best_sample;
                    }
                    candidate_best_sample = sample;
                } else if (
                    candidate_best_sample.valid &&
                    hypot(candidate_best_sample.seed.x - sample.seed.x,
                          candidate_best_sample.seed.y - sample.seed.y) >
                        1.0e-12 &&
                    (!candidate_previous_sample.valid ||
                     sample.residual < candidate_previous_sample.residual)) {
                    candidate_previous_sample = sample;
                }
            };
        const bool use_hint_only =
            !use_transverse_center_radius &&
            (d_finite(axis_scale_hints[axis_index]) ||
             d_finite(common_scale_hint));
        DeviceSeed last_predicted_seed;
        DeviceEdgeStopSample last_predicted_sample;
        bool have_last_prediction = false;
        bool candidate_traceable = false;
        const int pass_count = use_hint_only ? 2 : 1;
        for (int pass = 0; pass < pass_count && !anchor.valid; ++pass) {
            double scales[8];
            int scale_count = 0;
            if (use_transverse_center_radius) {
                d_push_unique_scale(scales, &scale_count, 8, 1.0);
            } else if (use_hint_only && pass == 0) {
                d_push_unique_scale(
                    scales,
                    &scale_count,
                    8,
                    axis_scale_hints[axis_index]);
                d_push_unique_scale(
                    scales,
                    &scale_count,
                    8,
                    0.99 * axis_scale_hints[axis_index]);
                d_push_unique_scale(
                    scales,
                    &scale_count,
                    8,
                    0.98 * axis_scale_hints[axis_index]);
                d_push_unique_scale(
                    scales,
                    &scale_count,
                    8,
                    common_scale_hint);
                d_push_unique_scale(
                    scales,
                    &scale_count,
                    8,
                    0.99 * common_scale_hint);
                d_push_unique_scale(
                    scales,
                    &scale_count,
                    8,
                    0.98 * common_scale_hint);
            } else {
                d_push_unique_scale(scales, &scale_count, 8, 0.25);
                d_push_unique_scale(scales, &scale_count, 8, 0.5);
                d_push_unique_scale(scales, &scale_count, 8, 1.0);
            }
            for (int s = 0; s < scale_count; ++s) {
                if (uc5_profile) {
                    atomicAdd(
                        &uc5_profile->base_probes[uc5_cell], 1ULL);
                }
                DeviceSeed seed = d_scaled_edge_seed(
                    model.chief_seed,
                    full_candidates[c],
                    scales[s]);
                DeviceEdgeStopSample sample = d_probe_calibrated_edge_seed(
                    surfaces,
                    cfg,
                    field,
                    wavelength_um,
                    seed,
                    anchor.px,
                    anchor.py,
                    rs,
                    true, builder_diag && g_builder_diagnostic_level > 1 ? &builder_diag->edge_work[i] : nullptr);
                if (!sample.valid) {
                    continue;
                }
                candidate_traceable = true;
                if (d_accept_calibrated_edge_sample(
                        surfaces,
                        cfg,
                        wavelength_um,
                        &anchor,
                        field,
                        sample.seed,
                        model.chief_seed,
                        first_surface_edge_radius,
                        anchor.px,
                        anchor.py,
                        enforce_edge_branch_continuity,
                        rs,
                        sample.sx,
                        sample.sy,
                        residual_limit,
                        &max_residual)) {
                    remember_scale_hint(
                        axis_index,
                        canonical_full_seed,
                        anchor.seed);
                    break;
                }
                remember_candidate_sample(sample);
                const double target_dx = ((g_edge_path_mode & 4) ? (field.fvcx + anchor.px * (1.0 - field.fvdx)) * rs - model.chief_stop_x : anchor.px * (1.0 - field.fvdx) * rs);
                const double target_dy = ((g_edge_path_mode & 4) ? (field.fvcy + anchor.py * (1.0 - field.fvdy)) * rs - model.chief_stop_y : anchor.py * (1.0 - field.fvdy) * rs);
                const double target_dr = hypot(target_dx, target_dy);
                const double stop_dx = sample.sx - model.chief_stop_x;
                const double stop_dy = sample.sy - model.chief_stop_y;
                const double projected =
                    target_dr > 1.0e-12
                        ? (stop_dx * target_dx + stop_dy * target_dy) / target_dr
                        : NAN;
                if (d_finite(projected) && projected > 1.0e-12) {
                    const double predicted_scale =
                        d_clamp(scales[s] * target_dr / projected, 1.0e-3, 1.25);
                    if (fabs(predicted_scale - scales[s]) <=
                        1.0e-7 * fmax(1.0, fabs(predicted_scale))) {
                        continue;
                    }
                    if (uc5_profile) {
                        atomicAdd(
                            &uc5_profile
                                 ->predicted_probes[uc5_cell],
                            1ULL);
                    }
                    DeviceSeed predicted_seed = d_scaled_edge_seed(
                        model.chief_seed,
                        full_candidates[c],
                        predicted_scale);
                    const bool repeated_prediction = have_last_prediction &&
                        predicted_seed.valid == last_predicted_seed.valid &&
                        predicted_seed.x == last_predicted_seed.x &&
                        predicted_seed.y == last_predicted_seed.y;
                    if (builder_diag && repeated_prediction) ++builder_diag->repeated_prediction[i];
                    DeviceEdgeStopSample predicted;
                    if ((g_edge_path_mode & 2) && repeated_prediction) {
                        predicted = last_predicted_sample;
                    } else {
                    predicted = d_probe_calibrated_edge_seed(
                        surfaces,
                        cfg,
                        field,
                        wavelength_um,
                        predicted_seed,
                        anchor.px,
                        anchor.py,
                        rs,
                        true, builder_diag && g_builder_diagnostic_level > 1 ? &builder_diag->edge_work[i] : nullptr);
                    }
                    last_predicted_seed = predicted_seed;
                    last_predicted_sample = predicted;
                    have_last_prediction = true;
                    if (predicted.valid &&
                        d_accept_calibrated_edge_sample(
                            surfaces,
                            cfg,
                            wavelength_um,
                            &anchor,
                            field,
                            predicted.seed,
                            model.chief_seed,
                            first_surface_edge_radius,
                            anchor.px,
                            anchor.py,
                            enforce_edge_branch_continuity,
                            rs,
                            predicted.sx,
                            predicted.sy,
                            residual_limit,
                            &max_residual)) {
                        remember_scale_hint(
                            axis_index,
                            canonical_full_seed,
                            anchor.seed);
                        break;
                    }
                    if (predicted.valid) {
                        candidate_traceable = true;
                    }
                    remember_candidate_sample(predicted);
                }
            }
        }
        if (anchor.valid) {
            break;
        }
        if (candidate_best_sample.valid &&
            (!best_sample.valid ||
             candidate_best_sample.residual < best_sample.residual)) {
            if (!candidate_previous_sample.valid && best_sample.valid &&
                hypot(best_sample.seed.x - candidate_best_sample.seed.x,
                      best_sample.seed.y - candidate_best_sample.seed.y) >
                    1.0e-12) {
                candidate_previous_sample = best_sample;
            }
            best_sample = candidate_best_sample;
            previous_sample = candidate_previous_sample;
            best_full_seed = full_candidates[c];
        } else if (
            candidate_best_sample.valid && best_sample.valid &&
            hypot(best_sample.seed.x - candidate_best_sample.seed.x,
                  best_sample.seed.y - candidate_best_sample.seed.y) >
                1.0e-12 &&
            (!previous_sample.valid ||
             candidate_best_sample.residual < previous_sample.residual)) {
            previous_sample = candidate_best_sample;
        }
    }
    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace
} // namespace optiland
