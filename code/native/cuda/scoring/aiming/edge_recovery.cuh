#pragma once
#include "../prelude.cuh"
#include "chief_secant_solver.cuh"
#include "edge_scale_solver.cuh"
#include "seed_candidates.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/calibration_tasks.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../diagnostics/asphere_profile_records.cuh"
#include "../diagnostics/builder_instrumentation.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/mechanical_constraints.cuh"
#include "../geometry/pupil_paraxial.cuh"

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
struct DRecoverBestEdgeSampleInputs {
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
    unsigned int& safeguarded_edge_mask;
    Capture_remember_scale_hint& remember_scale_hint;
    int& i;
    const int& axis_index;
    DeviceCalibratedEdgeAnchor& anchor;
    DeviceEdgeStopSample& best_sample;
    DeviceEdgeStopSample& previous_sample;
    DeviceSeed& best_full_seed;
    DeviceSeed& canonical_full_seed;
    DeviceAsphereUC5Profile*& uc5_profile;
    const int& uc5_cell;
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
__device__ __forceinline__ native_detail::PhaseOutcome d_recover_best_edge_sample(Inputs& inputs) {
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
    auto& safeguarded_edge_mask = inputs.safeguarded_edge_mask;
    auto& remember_scale_hint = inputs.remember_scale_hint;
    auto& i = inputs.i;
    auto& axis_index = inputs.axis_index;
    auto& anchor = inputs.anchor;
    auto& best_sample = inputs.best_sample;
    auto& previous_sample = inputs.previous_sample;
    auto& best_full_seed = inputs.best_full_seed;
    auto& canonical_full_seed = inputs.canonical_full_seed;
    auto& uc5_profile = inputs.uc5_profile;
    auto& uc5_cell = inputs.uc5_cell;
    if (!anchor.valid && best_sample.valid) {
        const double target_x =
            (field.fvcx + anchor.px * (1.0 - field.fvdx)) * rs;
        const double target_y =
            (field.fvcy + anchor.py * (1.0 - field.fvdy)) * rs;
        const double target_dx = anchor.px * (1.0 - field.fvdx) * rs;
        const double target_dy = anchor.py * (1.0 - field.fvdy) * rs;
        const double target_dr = fmax(1.0e-9, hypot(target_dx, target_dy));
        const double seed_dr = fmax(
            1.0e-9,
            hypot(best_sample.seed.x - model.chief_seed.x,
                  best_sample.seed.y - model.chief_seed.y));
        const double initial_scale = seed_dr / target_dr;
        if (builder_diag) {
            double* v = builder_diag->initial_data[i];
            v[0]=best_sample.seed.x; v[1]=best_sample.seed.y;
            v[2]=best_sample.sx; v[3]=best_sample.sy;
            v[4]=best_sample.residual; v[5]=initial_scale;
            if (best_sample.residual <= residual_limit) ++builder_diag->residual_ok_window_rejected[i];
        }
        DeviceSeed solved;
        double edge_sx = 0.0;
        double edge_sy = 0.0;
        double residual = INFINITY;
        if (uc5_profile) {
            atomicAdd(&uc5_profile->secant_calls[uc5_cell], 1ULL);
        }
        bool accepted = d_solve_seed_to_stop_secant<false>(
            surfaces,
            cfg,
            field,
            wavelength_um,
            best_sample.seed,
            target_x,
            target_y,
            residual_limit,
            cfg.calibrated_edge_iterations,
            initial_scale,
            0.0,
            0.0,
            initial_scale,
            true,
            &solved,
            &edge_sx,
            &edge_sy,
            &residual,
            nullptr,
            &best_sample,
            previous_sample.valid ? &previous_sample : nullptr,
            true,
            2,
            d_uc5_encode_edge_role(uc5_cell), INFINITY,
            builder_diag && g_builder_diagnostic_level > 1 ? &builder_diag->edge_work[i] : nullptr);
        if (!accepted && previous_sample.valid) {
            if (uc5_profile) {
                atomicAdd(&uc5_profile->secant_calls[uc5_cell], 1ULL);
            }
            accepted = d_solve_seed_to_stop_secant<false>(
                surfaces,
                cfg,
                field,
                wavelength_um,
                best_sample.seed,
                target_x,
                target_y,
                residual_limit,
                cfg.calibrated_edge_iterations,
                initial_scale,
                0.0,
                0.0,
                initial_scale,
                true,
                &solved,
                &edge_sx,
                &edge_sy,
                &residual,
                nullptr,
                &best_sample,
                nullptr,
                true,
                2,
                d_uc5_encode_edge_role(uc5_cell), INFINITY,
            builder_diag && g_builder_diagnostic_level > 1 ? &builder_diag->edge_work[i] : nullptr);
        }
        // Match the CPU primary-wavelength recovery contract. A
        // finite-difference inverse Jacobian costs two probes and is
        // attempted only after both cheap scalar-inverse secant calls
        // fail. This replaces the former CPU-only damped solve without
        // introducing a pupil scan into the population hot path.
        const bool primary_vignetting_edge =
            cfg.primary_wavelength_apertures &&
            fabs(wavelength_um - cfg.primary_wavelength) <= 1.0e-10;
        if (!accepted && primary_vignetting_edge &&
            best_sample.valid && best_sample.seed.valid) {
            DeviceBuilderCycleScope e3_scope(builder_diag ? &builder_diag->e3_cycles[i] : nullptr);
            if (builder_diag) ++builder_diag->e3_calls[i];
            const double delta = 1.0e-4 * fmax(
                1.0,
                d_entrance_pupil_radius_from_cfg(
                    surfaces, cfg, wavelength_um));
            DeviceSeed seed_x = best_sample.seed;
            DeviceSeed seed_y = best_sample.seed;
            seed_x.x += delta;
            seed_y.y += delta;
            const DeviceEdgeStopSample sample_x =
                d_probe_calibrated_edge_seed(
                    surfaces,
                    cfg,
                    field,
                    wavelength_um,
                    seed_x,
                    anchor.px,
                    anchor.py,
                    rs,
                    true, builder_diag && g_builder_diagnostic_level > 1 ? &builder_diag->e3_work[i] : nullptr);
            const DeviceEdgeStopSample sample_y =
                d_probe_calibrated_edge_seed(
                    surfaces,
                    cfg,
                    field,
                    wavelength_um,
                    seed_y,
                    anchor.px,
                    anchor.py,
                    rs,
                    true, builder_diag && g_builder_diagnostic_level > 1 ? &builder_diag->e3_work[i] : nullptr);
            if (sample_x.valid && sample_y.valid) {
                const double j11 =
                    (sample_x.sx - best_sample.sx) / delta;
                const double j21 =
                    (sample_x.sy - best_sample.sy) / delta;
                const double j12 =
                    (sample_y.sx - best_sample.sx) / delta;
                const double j22 =
                    (sample_y.sy - best_sample.sy) / delta;
                const double det = j11 * j22 - j12 * j21;
                if (d_finite(det) && fabs(det) > 1.0e-14) {
                    const double track = d_finite(d_total_track(surfaces, cfg))
                                             ? fabs(d_total_track(surfaces, cfg))
                                             : 0.0;
                    const double theta = d_finite(cfg.max_field_deg)
                                             ? fabs(cfg.max_field_deg) *
                                                   3.14159265358979323846 / 180.0
                                             : 0.0;
                    const double field_slope = d_finite(theta)
                                                   ? fmin(64.0, fabs(tan(theta)))
                                                   : 0.0;
                    const double position_step_cap = fmax(
                        1.0,
                        fmax(fabs(rs), 0.10 * track * field_slope));
                    if (uc5_profile) {
                        atomicAdd(
                            &uc5_profile->secant_calls[uc5_cell], 1ULL);
                    }
                    accepted = d_solve_seed_to_stop_secant<false>(
                        surfaces,
                        cfg,
                        field,
                        wavelength_um,
                        best_sample.seed,
                        target_x,
                        target_y,
                        residual_limit,
                        cfg.calibrated_edge_iterations,
                        j22 / det,
                        -j12 / det,
                        -j21 / det,
                        j11 / det,
                        true,
                        &solved,
                        &edge_sx,
                        &edge_sy,
                        &residual,
                        nullptr,
                        &best_sample,
                        nullptr,
                        true,
                        4,
                        d_uc5_encode_edge_role(uc5_cell),
                        position_step_cap,
                        builder_diag && g_builder_diagnostic_level > 1 ? &builder_diag->e3_work[i] : nullptr);
                    if (builder_diag && accepted) ++builder_diag->e3_ok[i];
                }
            }
        }
        if constexpr (kSafeguardedRecoveryMode == 0) {
            if (accepted &&
                d_seed_in_first_surface_bounds(
                    surfaces, cfg, wavelength_um, solved)) {
                // Strict Round17: a stop-valid secant seed is accepted
                // immediately after the first-surface bounds check.
                anchor.seed = solved;
                anchor.stop_x = edge_sx;
                anchor.stop_y = edge_sy;
                anchor.valid = 1;
                max_residual = fmax(max_residual, residual);
                remember_scale_hint(
                    axis_index, canonical_full_seed, anchor.seed);
            }
        } else {
            DeviceSeed deferred_stop_valid_seed;
            double deferred_stop_valid_residual = INFINITY;
            if (accepted &&
                d_seed_in_first_surface_bounds(
                    surfaces, cfg, wavelength_um, solved)) {
                deferred_stop_valid_seed = solved;
                deferred_stop_valid_residual = residual;
                d_accept_calibrated_edge_sample(
                    surfaces,
                    cfg,
                    wavelength_um,
                    &anchor,
                    field,
                    solved,
                    model.chief_seed,
                    first_surface_edge_radius,
                    anchor.px,
                    anchor.py,
                    enforce_edge_branch_continuity,
                    rs,
                    edge_sx,
                    edge_sy,
                    residual_limit,
                    &max_residual);
            }
            if (!anchor.valid) {
                if constexpr (kSafeguardedRecoveryMode == 2) {
                    d_solve_calibrated_edge_by_safeguarded_scale(
                        surfaces,
                        cfg,
                        field,
                        wavelength_um,
                        model,
                        first_surface_edge_radius,
                        enforce_edge_branch_continuity,
                        rs,
                        residual_limit,
                        &anchor,
                        anchor.px,
                        anchor.py,
                        best_full_seed,
                        &max_residual);
                } else if (deferred_stop_valid_seed.valid &&
                           d_finite(deferred_stop_valid_residual) &&
                           deferred_stop_valid_residual <= residual_limit) {
                    static_assert(kSafeguardedRecoveryMode == 1);
                    // Recovery is reserved for a concrete Round17
                    // stop-valid solution that fails only the local
                    // branch-continuity contract.  If the fast solve
                    // never produced a stop-valid edge, preserve the
                    // strict Round17 invalid-model result instead of
                    // spending a 160-sample scan trying to rescue an
                    // immature DE candidate.
                    safeguarded_edge_mask |= (1u << i);
                }
            }
            if (!anchor.valid && deferred_stop_valid_seed.valid &&
                d_finite(deferred_stop_valid_residual) &&
                deferred_stop_valid_residual <= residual_limit) {
                // Continuity is a branch preference. Retain the
                // stop-valid Round17 seed only after recovery has been
                // requested or exhausted.
                anchor.seed = deferred_stop_valid_seed;
                anchor.stop_x = edge_sx;
                anchor.stop_y = edge_sy;
                anchor.correction_x = 0.0;
                anchor.correction_y = 0.0;
                anchor.valid = 1;
                max_residual =
                    fmax(max_residual, deferred_stop_valid_residual);
            }
            if (anchor.valid && anchor.seed.valid) {
                remember_scale_hint(
                    axis_index, canonical_full_seed, anchor.seed);
            }
        }
    }
    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace
} // namespace optiland
