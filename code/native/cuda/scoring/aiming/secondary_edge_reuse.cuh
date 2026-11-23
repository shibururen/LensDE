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
    class Capture_remember_scale_hint,
    class Capture_remember_sample
>
struct DReuseSameWaveEdgeAnchorInputs {
    const DeviceCalibratedModel*& same_field_wave_model;
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
    Capture_remember_scale_hint& remember_scale_hint;
    int& i;
    const int& axis_index;
    DeviceCalibratedEdgeAnchor& anchor;
    Capture_remember_sample& remember_sample;
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
__device__ __forceinline__ native_detail::PhaseOutcome d_reuse_same_wave_edge_anchor(Inputs& inputs) {
    auto& same_field_wave_model = inputs.same_field_wave_model;
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
    auto& remember_scale_hint = inputs.remember_scale_hint;
    auto& i = inputs.i;
    auto& axis_index = inputs.axis_index;
    auto& anchor = inputs.anchor;
    auto& remember_sample = inputs.remember_sample;
    auto& canonical_full_seed = inputs.canonical_full_seed;
    auto& uc5_profile = inputs.uc5_profile;
    auto& uc5_cell = inputs.uc5_cell;
    auto& cached_seed_traceable = inputs.cached_seed_traceable;
    if (same_field_wave_model && same_field_wave_model->valid &&
        same_field_wave_model->chief_seed.valid &&
        same_field_wave_model->edge_anchors[i].valid &&
        same_field_wave_model->edge_anchors[i].seed.valid) {
        DeviceSeed cached_full_seed;
        cached_full_seed.x =
            model.chief_seed.x +
            (same_field_wave_model->edge_anchors[i].seed.x -
             same_field_wave_model->chief_seed.x);
        cached_full_seed.y =
            model.chief_seed.y +
            (same_field_wave_model->edge_anchors[i].seed.y -
             same_field_wave_model->chief_seed.y);
        cached_full_seed.valid = 1;
        DeviceEdgeStopSample cached_sample = d_probe_calibrated_edge_seed(
            surfaces,
            cfg,
            field,
            wavelength_um,
            cached_full_seed,
            anchor.px,
            anchor.py,
            rs,
            true, builder_diag && g_builder_diagnostic_level > 1 ? &builder_diag->edge_work[i] : nullptr);
        if (cached_sample.valid) {
            cached_seed_traceable = true;
            if (uc5_profile) {
                atomicAdd(
                    &uc5_profile->cached_traceable[uc5_cell], 1ULL);
            }
            remember_sample(cached_sample, cached_full_seed);
            d_accept_calibrated_edge_sample(
                surfaces,
                cfg,
                wavelength_um,
                &anchor,
                field,
                cached_sample.seed,
                model.chief_seed,
                first_surface_edge_radius,
                anchor.px,
                anchor.py,
                enforce_edge_branch_continuity,
                rs,
                cached_sample.sx,
                cached_sample.sy,
                residual_limit,
                &max_residual);
            if (anchor.valid) {
                if (uc5_profile) {
                    atomicAdd(
                        &uc5_profile->cached_accepted[uc5_cell], 1ULL);
                }
                remember_scale_hint(
                    axis_index,
                    canonical_full_seed,
                    anchor.seed);
            }
            if (!anchor.valid) {
                const double target_dx =
                    ((g_edge_path_mode & 4) ? (field.fvcx + anchor.px * (1.0 - field.fvdx)) * rs - model.chief_stop_x : anchor.px * (1.0 - field.fvdx) * rs);
                const double target_dy =
                    ((g_edge_path_mode & 4) ? (field.fvcy + anchor.py * (1.0 - field.fvdy)) * rs - model.chief_stop_y : anchor.py * (1.0 - field.fvdy) * rs);
                const double target_dr = hypot(target_dx, target_dy);
                const double stop_dx = cached_sample.sx - model.chief_stop_x;
                const double stop_dy = cached_sample.sy - model.chief_stop_y;
                const double projected =
                    target_dr > 1.0e-12
                        ? (stop_dx * target_dx + stop_dy * target_dy) /
                              target_dr
                        : NAN;
                if (d_finite(projected) && projected > 1.0e-12) {
                    const double predicted_scale = d_clamp(
                        target_dr / projected,
                        1.0e-3,
                        1.25);
                    if (fabs(predicted_scale - 1.0) >
                        1.0e-7 * fmax(1.0, fabs(predicted_scale))) {
                        if (uc5_profile) {
                            atomicAdd(
                                &uc5_profile
                                     ->predicted_probes[uc5_cell],
                                1ULL);
                        }
                        const DeviceSeed predicted_seed = d_scaled_edge_seed(
                            model.chief_seed,
                            cached_full_seed,
                            predicted_scale);
                        DeviceEdgeStopSample predicted =
                            d_probe_calibrated_edge_seed(
                                surfaces,
                                cfg,
                                field,
                                wavelength_um,
                                predicted_seed,
                                anchor.px,
                                anchor.py,
                                rs,
                                true, builder_diag && g_builder_diagnostic_level > 1 ? &builder_diag->edge_work[i] : nullptr);
                        if (predicted.valid) {
                            cached_seed_traceable = true;
                            remember_sample(predicted, cached_full_seed);
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
                                &max_residual);
                            if (anchor.valid) {
                                if (uc5_profile) {
                                    atomicAdd(
                                        &uc5_profile
                                             ->cached_accepted[uc5_cell],
                                        1ULL);
                                }
                                remember_scale_hint(
                                    axis_index,
                                    canonical_full_seed,
                                    anchor.seed);
                            }
                        }
                    }
                }
            }
        }
    }
    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace
} // namespace optiland
