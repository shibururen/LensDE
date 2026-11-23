#pragma once
#include "../prelude.cuh"
#include "chief_geometry.cuh"
#include "chief_secant_solver.cuh"
#include "edge_candidate_search.cuh"
#include "edge_recovery.cuh"
#include "edge_scale_solver.cuh"
#include "edge_symmetry.cuh"
#include "secondary_edge_reuse.cuh"
#include "seed_candidates.cuh"
#include "vignetted_continuation.cuh"
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
    bool kUsePrecomputedFirstEdge
>
struct DCalibrateModelEdgesInputs {
    const DeviceCalibratedCenterFootprint*& center_footprint;
    int& field_index;
    const DeviceCalibratedModel*& previous_model;
    const DeviceField*& previous_field;
    const DeviceCalibratedModel*& previous2_model;
    const DeviceField*& previous2_field;
    const DeviceCalibratedModel*& same_field_wave_model;
    const DeviceSurface*& surfaces;
    const DeviceConfig& cfg;
    const DeviceField& field;
    double& wavelength_um;
    DeviceCalibratedEdgeAnchor& precomputed_first_edge;
    DeviceCalibratedModel& model;
    DeviceBuilderDiagnostic*& builder_diag;
    const double& rs;
    const double& first_surface_edge_radius;
    const bool& enforce_edge_branch_continuity;
    const double& residual_limit;
    const double (&edge_px)[4];
    const double (&edge_py)[4];
    double& max_residual;
    unsigned int& safeguarded_edge_mask;
    const int& uc5_symmetry;
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
__device__ __forceinline__ native_detail::PhaseOutcome d_calibrate_model_edges(Inputs& inputs) {
    auto& center_footprint = inputs.center_footprint;
    auto& field_index = inputs.field_index;
    auto& previous_model = inputs.previous_model;
    auto& previous_field = inputs.previous_field;
    auto& previous2_model = inputs.previous2_model;
    auto& previous2_field = inputs.previous2_field;
    auto& same_field_wave_model = inputs.same_field_wave_model;
    auto& surfaces = inputs.surfaces;
    auto& cfg = inputs.cfg;
    auto& field = inputs.field;
    auto& wavelength_um = inputs.wavelength_um;
    auto& precomputed_first_edge = inputs.precomputed_first_edge;
    auto& model = inputs.model;
    auto& builder_diag = inputs.builder_diag;
    auto& rs = inputs.rs;
    auto& first_surface_edge_radius = inputs.first_surface_edge_radius;
    auto& enforce_edge_branch_continuity = inputs.enforce_edge_branch_continuity;
    auto& residual_limit = inputs.residual_limit;
    auto& edge_px = inputs.edge_px;
    auto& edge_py = inputs.edge_py;
    auto& max_residual = inputs.max_residual;
    auto& safeguarded_edge_mask = inputs.safeguarded_edge_mask;
    auto& uc5_symmetry = inputs.uc5_symmetry;
    if (cfg.calibrated_edge_iterations > 0) {
        double axis_scale_hints[2] = {NAN, NAN};
        double common_scale_hint = NAN;
        auto relative_edge_scale = [&](const DeviceSeed& full_seed,
                                       const DeviceSeed& accepted_seed) -> double {
            if (!full_seed.valid || !accepted_seed.valid) {
                return NAN;
            }
            const double full_dx = full_seed.x - model.chief_seed.x;
            const double full_dy = full_seed.y - model.chief_seed.y;
            const double denom = full_dx * full_dx + full_dy * full_dy;
            if (!d_finite(denom) || denom <= 1.0e-24) {
                return NAN;
            }
            const double seed_dx = accepted_seed.x - model.chief_seed.x;
            const double seed_dy = accepted_seed.y - model.chief_seed.y;
            const double scale =
                (seed_dx * full_dx + seed_dy * full_dy) / denom;
            return d_finite(scale) ? scale : NAN;
        };
        auto remember_scale_hint = [&](int axis,
                                       const DeviceSeed& full_seed,
                                       const DeviceSeed& accepted_seed) {
            const double scale = relative_edge_scale(full_seed, accepted_seed);
            if (!d_finite(scale) || scale <= 0.0) {
                return;
            }
            if (axis >= 0 && axis < 2) {
                axis_scale_hints[axis] = scale;
            }
            common_scale_hint = scale;
        };
        for (int i = 0; i < kEdgeCount; ++i) {
            DeviceBuilderCycleScope edge_scope(builder_diag ? &builder_diag->edge_cycles[i] : nullptr);
            const int axis_index = i < 2 ? 0 : 1;
            DeviceCalibratedEdgeAnchor anchor;
            anchor.px = edge_px[i];
            anchor.py = edge_py[i];
            DeviceSeed full_candidates[10];
            int candidate_count = 0;
            DeviceEdgeStopSample best_sample;
            DeviceEdgeStopSample previous_sample;
            DeviceSeed best_full_seed;
            auto remember_sample = [&](const DeviceEdgeStopSample& sample,
                                       const DeviceSeed& full_seed) {
                if (!sample.valid) {
                    return;
                }
                if (!best_sample.valid || sample.residual < best_sample.residual) {
                    if (best_sample.valid &&
                        hypot(best_sample.seed.x - sample.seed.x,
                              best_sample.seed.y - sample.seed.y) > 1.0e-12) {
                        previous_sample = best_sample;
                    }
                    best_sample = sample;
                    best_full_seed = full_seed;
                } else if (best_sample.valid &&
                           hypot(best_sample.seed.x - sample.seed.x,
                                 best_sample.seed.y - sample.seed.y) > 1.0e-12 &&
                           (!previous_sample.valid ||
                            sample.residual < previous_sample.residual)) {
                    previous_sample = sample;
                }
            };

            const bool use_transverse_center_radius =
                center_footprint &&
                center_footprint->valid &&
                center_footprint->edge_vectors[i].valid &&
                fabs(anchor.py) > fabs(anchor.px);
            DeviceSeed canonical_full_seed;
            if (use_transverse_center_radius) {
                const DeviceSeed& vector = center_footprint->edge_vectors[i];
                canonical_full_seed = d_shifted_chief_seed_by_transverse_radius(
                    surfaces,
                    cfg,
                    field,
                    wavelength_um,
                    model.chief_seed,
                    anchor.px,
                    anchor.py,
                    hypot(vector.x, vector.y));
            } else {
                canonical_full_seed.x =
                    model.chief_seed.x + first_surface_edge_radius * anchor.px;
                canonical_full_seed.y =
                    model.chief_seed.y + first_surface_edge_radius * anchor.py;
                canonical_full_seed.valid = 1;
            }
            if constexpr (kUsePrecomputedFirstEdge) {
                if (i == 0) {
                    anchor = precomputed_first_edge;
                    model.edge_anchors[i] = anchor;
                    if (anchor.valid && anchor.seed.valid) {
                        remember_scale_hint(
                            axis_index, canonical_full_seed, anchor.seed);
                    }
                    continue;
                }
            }
            // Experiment: only reuse STOP-validated anchors under exact
            // symmetry. A geometric completion (valid flag but NaN STOP)
            // is expressly ineligible. No target or aperture is changed.
            if ((g_edge_path_mode & 8) && (!(g_edge_path_mode & 256) || !kRepairBuild) && cfg.primary_wavelength_apertures &&
                kSafeguardedRecoveryMode == 0 &&
                (!(g_edge_path_mode & 16) ||
                 fabs(wavelength_um-cfg.primary_wavelength)<=1.0e-10)) {
                int symmetry = d_field_pupil_symmetry(cfg, field);
                // Approximate equality in the general symmetry helper is
                // insufficient for a zero-probe exact coordinate transform.
                if (field.hx != 0.0 || field.fvcx != 0.0 ||
                    model.chief_seed.x != 0.0) symmetry = kPupilSymmetryNone;
                if (!(g_edge_path_mode & 32)) {
                    if (symmetry != kPupilSymmetryNone) symmetry = kPupilSymmetryMirrorX;
                } else if (symmetry == kPupilSymmetryDihedral4 ||
                           symmetry == kPupilSymmetrySignXY) {
                    if (field.hy != 0.0 || field.fvcy != 0.0 ||
                        model.chief_seed.y != 0.0) symmetry = kPupilSymmetryMirrorX;
                    else if (field.fvdx != field.fvdy) symmetry = kPupilSymmetrySignXY;
                }
                int transform = 0;
                const int from = d_uc5_mirror_source_anchor(symmetry,i,&transform);
                if (from >= 0 && from < i) {
                    const auto& origin = model.edge_anchors[from];
                    DeviceCalibratedEdgeAnchor reflected = d_uc5_transform_anchor(origin,transform);
                    const double target_x=(field.fvcx+anchor.px*(1.0-field.fvdx))*rs;
                    const double target_y=(field.fvcy+anchor.py*(1.0-field.fvdy))*rs;
                    double residual=hypot(reflected.stop_x-target_x,reflected.stop_y-target_y);
                    bool reusable=origin.valid && origin.seed.valid &&
                        d_finite(origin.stop_x) && d_finite(origin.stop_y) &&
                        d_finite(residual) && residual<=residual_limit &&
                        reflected.px==anchor.px && reflected.py==anchor.py &&
                        d_seed_in_first_surface_bounds(surfaces,cfg,wavelength_um,reflected.seed);
                    if (reusable && (g_edge_path_mode & 64)) {
                        const auto checked=d_probe_calibrated_edge_seed(
                            surfaces,cfg,field,wavelength_um,reflected.seed,
                            anchor.px,anchor.py,rs,true,
                            builder_diag && g_builder_diagnostic_level>1 ? &builder_diag->edge_work[i] : nullptr);
                        reusable=checked.valid && checked.residual<=residual_limit;
                        if (reusable) {
                            reflected.stop_x=checked.sx;reflected.stop_y=checked.sy;
                            residual=checked.residual;
                        } else if (builder_diag) ++builder_diag->symmetry_probe_failed[i];
                    }
                    if (reusable) {
                        anchor=reflected;
                        model.edge_anchors[i]=anchor;
                        max_residual=fmax(max_residual,residual);
                        remember_scale_hint(axis_index,canonical_full_seed,anchor.seed);
                        if (builder_diag) ++builder_diag->symmetry_reused[i];
                        continue;
                    }
                }
            }
            int mirror_transform = 0;
            const int mirror_source = d_uc5_mirror_source_anchor(
                uc5_symmetry, i, &mirror_transform);
            if (mirror_source >= 0 && mirror_source < i &&
                model.edge_anchors[mirror_source].valid &&
                model.edge_anchors[mirror_source].seed.valid) {
                anchor = d_uc5_transform_anchor(
                    model.edge_anchors[mirror_source], mirror_transform);
                model.edge_anchors[i] = anchor;
                remember_scale_hint(
                    axis_index, canonical_full_seed, anchor.seed);
                DeviceAsphereUC5Profile* profile = g_asphere_uc5_profile;
                if (profile) {
                    const int cell = d_uc5_counter_cell(
                        cfg, wavelength_um, i, kRepairBuild);
                    atomicAdd(&profile->anchor_mirrored[cell], 1ULL);
                }
                continue;
            }
            DeviceAsphereUC5Profile* uc5_profile = g_asphere_uc5_profile;
            const int uc5_cell = d_uc5_counter_cell(
                cfg, wavelength_um, i, kRepairBuild);
            if (uc5_profile) {
                atomicAdd(&uc5_profile->anchor_entered[uc5_cell], 1ULL);
            }
            // Warm start only. A failure leaves all legacy search samples untouched.
            if constexpr (kSafeguardedRecoveryMode == 0 && !kRepairBuild) {
                const bool warm_enabled = (i == 0 && (g_edge_path_mode & 512)) ||
                    ((i == 2 || i == 3) && (g_edge_path_mode & 1024));
                if (warm_enabled && field_index >= 2 && cfg.primary_wavelength_apertures &&
                    fabs(wavelength_um-cfg.primary_wavelength) <= 1.0e-10) {
                    double scale = NAN;
                    const DeviceSeed warm = d_vignetted_past_field_edge_seed(
                        cfg,field,model,i,rs,residual_limit,previous_model,previous_field,
                        previous2_model,previous2_field,&scale);
                    if (warm.valid) {
                        if (builder_diag) ++builder_diag->warm_attempt[i];
                        DeviceBuilderWork* work = builder_diag && g_builder_diagnostic_level > 1
                            ? &builder_diag->edge_work[i] : nullptr;
                        const auto sample = d_probe_calibrated_edge_seed(
                            surfaces,cfg,field,wavelength_um,warm,anchor.px,anchor.py,rs,true,work);
                        if (builder_diag) {
                            auto* v = builder_diag->warm_initial[i];
                            v[0]=warm.x; v[1]=warm.y; v[2]=sample.residual; v[3]=scale;
                            if (sample.valid) ++builder_diag->warm_traceable[i];
                        }
                        if (sample.valid) {
                            DeviceSeed solved;
                            double sx=0.0,sy=0.0,residual=INFINITY;
                            const double tx=(field.fvcx+anchor.px*(1.0-field.fvdx))*rs;
                            const double ty=(field.fvcy+anchor.py*(1.0-field.fvdy))*rs;
                            if (uc5_profile) atomicAdd(&uc5_profile->secant_calls[uc5_cell],1ULL);
                            const bool accepted = d_solve_seed_to_stop_secant<false>(
                                surfaces,cfg,field,wavelength_um,warm,tx,ty,residual_limit,
                                cfg.calibrated_edge_iterations,scale,0.0,0.0,scale,true,
                                &solved,&sx,&sy,&residual,nullptr,&sample,nullptr,true,2,
                                d_uc5_encode_edge_role(uc5_cell),INFINITY,work);
                            // Identical to the existing mode-0 fast secant acceptance.
                            if (accepted && d_seed_in_first_surface_bounds(
                                    surfaces,cfg,wavelength_um,solved)) {
                                anchor.seed=solved; anchor.stop_x=sx; anchor.stop_y=sy;
                                anchor.valid=1; model.edge_anchors[i]=anchor;
                                max_residual=fmax(max_residual,residual);
                                remember_scale_hint(axis_index,canonical_full_seed,anchor.seed);
                                if (builder_diag) ++builder_diag->warm_solved[i];
                                continue;
                            }
                        }
                    }
                }
            }
            bool cached_seed_traceable = false;
            DReuseSameWaveEdgeAnchorInputs<kUsePrecomputedChief, kEdgeCount, kSafeguardedRecoveryMode, kRepairBuild, kUsePrecomputedFirstEdge, decltype(remember_scale_hint), decltype(remember_sample)> d_reuse_same_wave_edge_anchor_inputs{
                same_field_wave_model,
                surfaces,
                cfg,
                field,
                wavelength_um,
                model,
                builder_diag,
                rs,
                first_surface_edge_radius,
                enforce_edge_branch_continuity,
                residual_limit,
                max_residual,
                remember_scale_hint,
                i,
                axis_index,
                anchor,
                remember_sample,
                canonical_full_seed,
                uc5_profile,
                uc5_cell,
                cached_seed_traceable
            };
d_reuse_same_wave_edge_anchor<kUsePrecomputedChief, kEdgeCount, kSafeguardedRecoveryMode, kRepairBuild, kUsePrecomputedFirstEdge>(d_reuse_same_wave_edge_anchor_inputs);

            if (!anchor.valid && !cached_seed_traceable &&
                use_transverse_center_radius) {
                const DeviceSeed& vector = center_footprint->edge_vectors[i];
                const double center_beam_radius = hypot(vector.x, vector.y);
                double radius_scales[10];
                int radius_scale_count = 0;
                d_push_unique_scale(
                    radius_scales,
                    &radius_scale_count,
                    10,
                    axis_scale_hints[axis_index]);
                d_push_unique_scale(
                    radius_scales,
                    &radius_scale_count,
                    10,
                    0.99 * axis_scale_hints[axis_index]);
                d_push_unique_scale(
                    radius_scales,
                    &radius_scale_count,
                    10,
                    0.98 * axis_scale_hints[axis_index]);
                if (d_finite(axis_scale_hints[axis_index])) {
                    d_push_unique_scale(
                        radius_scales,
                        &radius_scale_count,
                        10,
                        common_scale_hint);
                    d_push_unique_scale(
                        radius_scales,
                        &radius_scale_count,
                        10,
                        0.99 * common_scale_hint);
                    d_push_unique_scale(
                        radius_scales,
                        &radius_scale_count,
                        10,
                        0.98 * common_scale_hint);
                }
                d_push_unique_scale(radius_scales, &radius_scale_count, 10, 1.0);
                d_push_unique_scale(radius_scales, &radius_scale_count, 10, 0.5);
                d_push_unique_scale(radius_scales, &radius_scale_count, 10, 0.25);
                d_push_unique_scale(radius_scales, &radius_scale_count, 10, 0.125);
                for (int s = 0; s < radius_scale_count; ++s) {
                    d_push_unique_seed(
                        full_candidates,
                        &candidate_count,
                        10,
                        d_shifted_chief_seed_by_transverse_radius(
                            surfaces,
                            cfg,
                            field,
                            wavelength_um,
                            model.chief_seed,
                            anchor.px,
                            anchor.py,
                            center_beam_radius * radius_scales[s]));
                }
            } else if (!anchor.valid && !cached_seed_traceable) {
                DeviceSeed simple;
                simple.x = model.chief_seed.x + first_surface_edge_radius * anchor.px;
                simple.y = model.chief_seed.y + first_surface_edge_radius * anchor.py;
                simple.valid = 1;
                d_push_unique_seed(full_candidates, &candidate_count, 10, simple);
            }

            DProbeEdgeCandidateFamilyInputs<kUsePrecomputedChief, kEdgeCount, kSafeguardedRecoveryMode, kRepairBuild, kUsePrecomputedFirstEdge, decltype(remember_scale_hint)> d_probe_edge_candidate_family_inputs{
                surfaces,
                cfg,
                field,
                wavelength_um,
                model,
                builder_diag,
                rs,
                first_surface_edge_radius,
                enforce_edge_branch_continuity,
                residual_limit,
                max_residual,
                axis_scale_hints,
                common_scale_hint,
                remember_scale_hint,
                i,
                axis_index,
                anchor,
                full_candidates,
                candidate_count,
                best_sample,
                previous_sample,
                best_full_seed,
                use_transverse_center_radius,
                canonical_full_seed,
                uc5_profile,
                uc5_cell,
                cached_seed_traceable
            };
d_probe_edge_candidate_family<kUsePrecomputedChief, kEdgeCount, kSafeguardedRecoveryMode, kRepairBuild, kUsePrecomputedFirstEdge>(d_probe_edge_candidate_family_inputs);


            DRecoverBestEdgeSampleInputs<kUsePrecomputedChief, kEdgeCount, kSafeguardedRecoveryMode, kRepairBuild, kUsePrecomputedFirstEdge, decltype(remember_scale_hint)> d_recover_best_edge_sample_inputs{
                surfaces,
                cfg,
                field,
                wavelength_um,
                model,
                builder_diag,
                rs,
                first_surface_edge_radius,
                enforce_edge_branch_continuity,
                residual_limit,
                max_residual,
                safeguarded_edge_mask,
                remember_scale_hint,
                i,
                axis_index,
                anchor,
                best_sample,
                previous_sample,
                best_full_seed,
                canonical_full_seed,
                uc5_profile,
                uc5_cell
            };
d_recover_best_edge_sample<kUsePrecomputedChief, kEdgeCount, kSafeguardedRecoveryMode, kRepairBuild, kUsePrecomputedFirstEdge>(d_recover_best_edge_sample_inputs);

            if (!anchor.valid && best_sample.valid && best_sample.seed.valid &&
                fabs(wavelength_um - cfg.primary_wavelength) > 1.0e-10) {
                // The vignetting coefficients are measured at the primary
                // wavelength.  If a secondary unit edge terminates at a hard
                // traceability boundary, reuse the closest already-sampled
                // physical point as that wavelength's domain edge.  This is
                // a zero-scan fallback and never relaxes the primary edge.
                const bool x_axis = axis_index == 0;
                const double axis_denominator =
                    (x_axis ? (1.0 - field.fvdx) : (1.0 - field.fvdy)) * rs;
                const double actual_axis =
                    fabs(axis_denominator) > 1.0e-12
                        ? (x_axis
                               ? (best_sample.sx - field.fvcx * rs) / axis_denominator
                               : (best_sample.sy - field.fvcy * rs) / axis_denominator)
                        : NAN;
                const double requested_sign = x_axis ? anchor.px : anchor.py;
                const double cross_residual =
                    x_axis
                        ? fabs(best_sample.sy - field.fvcy * rs)
                        : fabs(best_sample.sx - field.fvcx * rs);
                const double cross_limit = fmax(
                    residual_limit,
                    fmax(0.0, cfg.calibrated_validation_residual_stop_fraction) *
                        d_physical_stop_radius(surfaces, cfg, rs));
                if (d_finite(actual_axis) &&
                    actual_axis * requested_sign > 0.0 &&
                    fabs(actual_axis) >= 0.98 &&
                    fabs(actual_axis) <= 1.0 + 1.0e-8 &&
                    d_finite(cross_residual) && cross_residual <= cross_limit) {
                    anchor.px = x_axis ? actual_axis : 0.0;
                    anchor.py = x_axis ? 0.0 : actual_axis;
                    anchor.seed = best_sample.seed;
                    anchor.stop_x = best_sample.sx;
                    anchor.stop_y = best_sample.sy;
                    anchor.correction_x = 0.0;
                    anchor.correction_y = 0.0;
                    anchor.valid = 1;
                    max_residual = fmax(max_residual, cross_residual);
                    remember_scale_hint(
                        axis_index, canonical_full_seed, anchor.seed);
                }
            }
            model.edge_anchors[i] = anchor;
            if (!anchor.valid || !anchor.seed.valid) {
                if (uc5_profile) {
                    atomicAdd(
                        &uc5_profile->exhausted_failures[uc5_cell], 1ULL);
                }
                const bool primary_wave =
                    fabs(wavelength_um - cfg.primary_wavelength) <= 1.0e-10;
                // Initial primary models can still provide partial/fallback
                // anchors to the secondary-wavelength post-loop rescue.  The
                // repair pass runs after those secondary builds, so only that
                // pass is dependency-free and safe to abort early.
                if (g_asphere_uc5_primary_abort_enabled && primary_wave &&
                    kRepairBuild) {
                    if (uc5_profile) {
                        for (int skipped = i + 1; skipped < 4; ++skipped) {
                            const int skipped_cell = d_uc5_counter_cell(
                                cfg,
                                wavelength_um,
                                skipped,
                                kRepairBuild);
                            atomicAdd(
                                &uc5_profile
                                     ->anchor_skipped_after_failure[skipped_cell],
                                1ULL);
                        }
                    }
                    model.calibrated_edges_all_valid = 0;
                    model.fit_rms = max_residual;
                    model.valid = 0;
                    return {native_detail::PhaseFlow::Return, false};
                }
            }
        }
    }
    return {native_detail::PhaseFlow::Proceed, false};
}
} // namespace
} // namespace optiland
