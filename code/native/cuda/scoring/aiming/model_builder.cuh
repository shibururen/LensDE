#pragma once
#include "../prelude.cuh"
#include "chief_geometry.cuh"
#include "edge_calibration.cuh"
#include "edge_scale_solver.cuh"
#include "model_chief.cuh"
#include "seed_candidates.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/calibration_tasks.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../diagnostics/asphere_profile_records.cuh"
#include "../diagnostics/builder_instrumentation.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/pupil_paraxial.cuh"

namespace optiland {
namespace {
template <
    bool kUsePrecomputedChief = false,
    int kEdgeCount = 4,
    int kSafeguardedRecoveryMode = 0,
    bool kRepairBuild = false,
    bool kUsePrecomputedFirstEdge = false>
__device__ __noinline__ void d_build_calibrated_model(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceField& field,
    double wavelength_um,
    DeviceCalibratedModel* output,
    const DeviceField* center_field = nullptr,
    const DeviceCalibratedCenterFootprint* center_footprint = nullptr,
    const DeviceCalibratedModel* inner_model = nullptr,
    const DeviceField* inner_field = nullptr,
    int field_index = -1,
    const DeviceCalibratedModel* previous_model = nullptr,
    const DeviceField* previous_field = nullptr,
    const DeviceCalibratedModel* previous2_model = nullptr,
    const DeviceField* previous2_field = nullptr,
    const DeviceCalibratedModel* same_field_wave_model = nullptr,
    const DeviceSeed* precomputed_chief_seed = nullptr,
    double precomputed_stop_x = NAN,
    double precomputed_stop_y = NAN,
    double precomputed_residual = INFINITY,
    int precomputed_initial_trial_start = 0) {
    if (!output) {
        return;
    }
    static_assert(kEdgeCount >= 0 && kEdgeCount <= 4);
    DeviceCalibratedEdgeAnchor precomputed_first_edge;
    if constexpr (kUsePrecomputedFirstEdge) {
        precomputed_first_edge = output->edge_anchors[0];
    }
    *output = DeviceCalibratedModel{};
    DeviceCalibratedModel& model = *output;
    DeviceBuilderDiagnostic* builder_diag = nullptr;
    if (g_builder_diagnostic) {
        const auto address = reinterpret_cast<unsigned long long>(output);
        const auto base = reinterpret_cast<unsigned long long>(g_builder_model_base);
        if (address >= base && address < base + g_builder_model_count * sizeof(DeviceCalibratedModel)) {
            builder_diag = g_builder_diagnostic + 2 * ((address - base) / sizeof(DeviceCalibratedModel)) + (kRepairBuild ? 1 : 0);
            ++builder_diag->calls;
        }
    }
    DeviceBuilderCycleScope whole_scope(builder_diag ? &builder_diag->total_cycles : nullptr);
    DeviceBuilderCycleScope chief_scope(builder_diag ? &builder_diag->chief_cycles : nullptr);
    if (!cfg.calibrated_enabled) {
        return;
    }
    const double rs = d_aperture_stop_radius(surfaces, cfg, wavelength_um);
    const double stop_scale = fmax(1.0e-9, fabs(rs));
    const double chief_target_x = field.fvcx * rs;
    const double chief_target_y = field.fvcy * rs;
    bool chief_ok = false;
    if constexpr (kUsePrecomputedChief) {
        if (precomputed_chief_seed && precomputed_chief_seed->valid &&
            d_finite(precomputed_stop_x) && d_finite(precomputed_stop_y) &&
            d_finite(precomputed_residual)) {
            model.chief_seed = *precomputed_chief_seed;
            model.chief_stop_x = precomputed_stop_x;
            model.chief_stop_y = precomputed_stop_y;
            model.fit_rms = precomputed_residual;
            chief_ok = true;
        }
    }
    DPrepareParaxialChiefInputs<kUsePrecomputedChief, kEdgeCount, kSafeguardedRecoveryMode, kRepairBuild, kUsePrecomputedFirstEdge> d_prepare_paraxial_chief_inputs{
        center_field,
        center_footprint,
        inner_model,
        inner_field,
        field_index,
        previous_model,
        previous_field,
        previous2_model,
        previous2_field,
        same_field_wave_model,
        precomputed_initial_trial_start,
        surfaces,
        cfg,
        field,
        wavelength_um,
        model,
        builder_diag,
        rs,
        stop_scale,
        chief_target_x,
        chief_target_y,
        chief_ok
    };
    d_prepare_paraxial_chief<kUsePrecomputedChief, kEdgeCount, kSafeguardedRecoveryMode, kRepairBuild, kUsePrecomputedFirstEdge>(d_prepare_paraxial_chief_inputs);

    chief_scope.finish();
    if (builder_diag && chief_ok && model.chief_seed.valid) ++builder_diag->chief_ok;
    if (!chief_ok || !model.chief_seed.valid) {
        return;
    }
    if constexpr (kEdgeCount == 0) {
        model.valid = 1;
        model.calibrated_edges_all_valid = 0;
        return;
    }

    const double search_radius =
        d_first_surface_search_radius(surfaces, cfg, wavelength_um);
    const double edge_radius = d_clamp(
        fmax(1.0e-9, d_entrance_pupil_radius_from_cfg(surfaces, cfg, wavelength_um)),
        1.0e-9,
        fmax(1.0e-9, search_radius));
    const DeviceField& radius_center_field = center_field ? *center_field : field;
    const double center_beam_edge_radius =
        d_center_meridional_radius(surfaces, cfg, radius_center_field, wavelength_um);
    const double first_surface_edge_radius =
        (d_finite(center_beam_edge_radius) && center_beam_edge_radius > 1.0e-9)
            ? fmin(edge_radius, center_beam_edge_radius)
            : edge_radius;
    const int first_aiming_surface =
        d_first_ray_aiming_surface(surfaces, cfg, wavelength_um);
    const bool enforce_edge_branch_continuity =
        first_aiming_surface >= 0 && first_aiming_surface < cfg.surface_count &&
        surfaces[first_aiming_surface].kind == kKindEvenAsphere;
    const double residual_limit = fmax(
        fmax(0.0, cfg.ray_aiming_tolerance),
        fmax(0.0, cfg.calibrated_edge_residual_stop_fraction) *
            d_physical_stop_radius(surfaces, cfg, rs));
    const double edge_px[4] = {1.0, -1.0, 0.0, 0.0};
    const double edge_py[4] = {0.0, 0.0, 1.0, -1.0};
    double max_residual =
        d_finite(model.fit_rms) ? fmax(0.0, model.fit_rms) : 0.0;
    unsigned int safeguarded_edge_mask = 0u;
    const int uc5_symmetry = g_asphere_uc5_symmetry_enabled
        ? d_field_pupil_symmetry(cfg, field)
        : kPupilSymmetryNone;

    if (builder_diag) {
        double* c = builder_diag->center_data;
        c[0] = model.chief_seed.x; c[1] = model.chief_seed.y;
        c[2] = model.chief_stop_x; c[3] = model.chief_stop_y;
        c[4] = chief_target_x; c[5] = chief_target_y;
        c[6] = residual_limit;
        if (g_edge_path_shadow) {
            const double delta = 1.0e-4 * fmax(1.0, edge_radius);
            DeviceSeed dx = model.chief_seed, dy = model.chief_seed;
            dx.x += delta; dy.y += delta;
            const auto sx = d_probe_calibrated_edge_seed(surfaces,cfg,field,wavelength_um,dx,0,0,rs,true);
            const auto sy = d_probe_calibrated_edge_seed(surfaces,cfg,field,wavelength_um,dy,0,0,rs,true);
            c[7] = sx.valid && sy.valid;
            if (c[7]) {
                c[8] = (sx.sx-model.chief_stop_x)/delta;
                c[9] = (sy.sx-model.chief_stop_x)/delta;
                c[10] = (sx.sy-model.chief_stop_y)/delta;
                c[11] = (sy.sy-model.chief_stop_y)/delta;
            }
        }
    }
    DCalibrateModelEdgesInputs<kUsePrecomputedChief, kEdgeCount, kSafeguardedRecoveryMode, kRepairBuild, kUsePrecomputedFirstEdge> d_calibrate_model_edges_inputs{
        center_footprint,
        field_index,
        previous_model,
        previous_field,
        previous2_model,
        previous2_field,
        same_field_wave_model,
        surfaces,
        cfg,
        field,
        wavelength_um,
        precomputed_first_edge,
        model,
        builder_diag,
        rs,
        first_surface_edge_radius,
        enforce_edge_branch_continuity,
        residual_limit,
        edge_px,
        edge_py,
        max_residual,
        safeguarded_edge_mask,
        uc5_symmetry
    };
    const auto d_calibrate_model_edges_outcome = d_calibrate_model_edges<kUsePrecomputedChief, kEdgeCount, kSafeguardedRecoveryMode, kRepairBuild, kUsePrecomputedFirstEdge>(d_calibrate_model_edges_inputs);
    if (d_calibrate_model_edges_outcome.flow == native_detail::PhaseFlow::Return) { return; }


    if (fabs(wavelength_um - cfg.primary_wavelength) > 1.0e-10 &&
        same_field_wave_model && same_field_wave_model->chief_seed.valid) {
        constexpr double kSecondaryPrimarySeedDamping = 0.997;
        for (int i = 0; i < 4; ++i) {
            DeviceCalibratedEdgeAnchor& anchor = model.edge_anchors[i];
            const DeviceCalibratedEdgeAnchor& primary_anchor =
                same_field_wave_model->edge_anchors[i];
            if ((anchor.valid && anchor.seed.valid) ||
                !primary_anchor.valid || !primary_anchor.seed.valid) {
                continue;
            }
            DeviceSeed candidate;
            candidate.x = model.chief_seed.x + kSecondaryPrimarySeedDamping *
                (primary_anchor.seed.x - same_field_wave_model->chief_seed.x);
            candidate.y = model.chief_seed.y + kSecondaryPrimarySeedDamping *
                (primary_anchor.seed.y - same_field_wave_model->chief_seed.y);
            candidate.valid = 1;
            DeviceEdgeStopSample sample = d_probe_calibrated_edge_seed(
                surfaces,
                cfg,
                field,
                wavelength_um,
                candidate,
                edge_px[i],
                edge_py[i],
                rs,
                true, builder_diag && g_builder_diagnostic_level > 1 ? &builder_diag->edge_work[i] : nullptr);
            if (!sample.valid) {
                continue;
            }
            const bool x_axis = i < 2;
            const double axis_denominator =
                (x_axis ? (1.0 - field.fvdx) : (1.0 - field.fvdy)) * rs;
            const double actual_axis =
                fabs(axis_denominator) > 1.0e-12
                    ? (x_axis
                           ? (sample.sx - field.fvcx * rs) / axis_denominator
                           : (sample.sy - field.fvcy * rs) / axis_denominator)
                    : NAN;
            const double requested_sign = x_axis ? edge_px[i] : edge_py[i];
            const double cross_residual =
                x_axis
                    ? fabs(sample.sy - field.fvcy * rs)
                    : fabs(sample.sx - field.fvcx * rs);
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
                anchor.seed = sample.seed;
                anchor.stop_x = sample.sx;
                anchor.stop_y = sample.sy;
                anchor.correction_x = 0.0;
                anchor.correction_y = 0.0;
                anchor.valid = 1;
                max_residual = fmax(max_residual, cross_residual);
            }
        }
    }

    // With vignetting optimization enabled, primary-wavelength unit edges
    // define the physical aperture and must be actual STOP-calibrated
    // solutions.  Geometric completion is only a secondary-wave sampling aid.
    // Preserve the historical completion outside that contract.
    const bool allow_unvalidated_edge_completion =
        !cfg.primary_wavelength_apertures ||
        fabs(wavelength_um - cfg.primary_wavelength) > 1.0e-10;
    if (allow_unvalidated_edge_completion) {
        for (int i = 0; i < 4; ++i) {
            DeviceCalibratedEdgeAnchor& anchor = model.edge_anchors[i];
            if (anchor.valid && anchor.seed.valid) {
                continue;
            }
            anchor.px = edge_px[i];
            anchor.py = edge_py[i];
            DeviceSeed fallback;
            const int opposite = (i == 0) ? 1 : (i == 1) ? 0 : (i == 2) ? 3 : 2;
            if (opposite >= 0 && opposite < 4 &&
                model.edge_anchors[opposite].valid &&
                model.edge_anchors[opposite].seed.valid) {
                fallback.x = model.chief_seed.x -
                             (model.edge_anchors[opposite].seed.x - model.chief_seed.x);
                fallback.y = model.chief_seed.y -
                             (model.edge_anchors[opposite].seed.y - model.chief_seed.y);
                fallback.valid = 1;
                if (!d_seed_in_first_surface_bounds(surfaces, cfg, wavelength_um, fallback)) {
                    fallback.valid = 0;
                }
            }
            if (!fallback.valid) {
                const double transverse_radius =
                    (d_finite(center_beam_edge_radius) && center_beam_edge_radius > 1.0e-9)
                        ? center_beam_edge_radius
                        : first_surface_edge_radius;
                fallback = d_shifted_chief_seed_by_transverse_radius(
                    surfaces,
                    cfg,
                    field,
                    wavelength_um,
                    model.chief_seed,
                    anchor.px,
                    anchor.py,
                    transverse_radius);
            }
            if (!fallback.valid) {
                fallback.x = model.chief_seed.x + first_surface_edge_radius * anchor.px;
                fallback.y = model.chief_seed.y + first_surface_edge_radius * anchor.py;
                fallback.valid = 1;
                if (!d_seed_in_first_surface_bounds(surfaces, cfg, wavelength_um, fallback)) {
                    fallback.valid = 0;
                }
            }
            if (fallback.valid) {
                anchor.seed = fallback;
                anchor.correction_x = 0.0;
                anchor.correction_y = 0.0;
                anchor.valid = 1;
            }
        }
    }
    // Match the CPU calibrated model: any permitted fallback anchors
    // participate in the final validity gate. Primary anchors remain invalid
    // when their physical STOP calibration failed during vignetting optimization.
    model.calibrated_edges_all_valid = 1;
    for (int i = 0; i < 4; ++i) {
        if (!model.edge_anchors[i].valid ||
            !model.edge_anchors[i].seed.valid) {
            model.calibrated_edges_all_valid = 0;
            break;
        }
    }
    // Interior interpolation residuals are diagnostic-only. The CPU aiming
    // diagnostics path samples them; device scoring avoids the extra traces.
    model.fit_rms = max_residual;
    model.valid = model.chief_seed.valid && model.calibrated_edges_all_valid;
    if (builder_diag) {
        builder_diag->model_ok += model.valid != 0;
        for (int i = 0; i < 4; ++i) builder_diag->edge_ok[i] += model.edge_anchors[i].valid && model.edge_anchors[i].seed.valid;
        for (int i = 0; i < 4; ++i) {
            const auto& a = model.edge_anchors[i];
            double* v = builder_diag->anchor_data[i];
            v[0]=a.seed.x; v[1]=a.seed.y; v[2]=a.stop_x; v[3]=a.stop_y;
        }
    }
    if constexpr (kSafeguardedRecoveryMode == 1) {
        if (!model.calibrated_edges_all_valid) {
            // A partially missing fast model remains a strict Round17
            // rejection.  Recovering the other stop-valid edges cannot make
            // that model usable, but it dominated sparse-pass time for early
            // DE trials.  Deep recovery is therefore limited to complete
            // models whose sole defect is a detected remote edge branch.
            safeguarded_edge_mask = 0u;
        }
        // correction_x is currently not consumed by calibrated ray generation;
        // use it only between the adjacent mark/recovery kernels. Recovery
        // rebuilds the model and restores the field to its normal zero value.
        model.edge_anchors[0].correction_x =
            static_cast<double>(safeguarded_edge_mask);
    }
}
} // namespace
} // namespace optiland
