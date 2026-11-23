#pragma once
#include "../prelude.cuh"
#include "../aiming/stop_samples.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../geometry/domain_materialization.cuh"
#include "../geometry/material_and_sag.cuh"
#include "../geometry/pupil_paraxial.cuh"
#include "geometry_constraints.cuh"
#include "residual_primitives.cuh"
#include "supported_ray_generation.cuh"
#include "unified_residuals.cuh"

namespace optiland {
namespace {
__device__ void d_apply_unified_merit(
    const DeviceSurface* surfaces,
    const DeviceConfig& cfg,
    const DeviceMeritTerms& terms,
    double glass_proximity_penalty,
    CudaScoreSummary* summary) {
    if (!summary) {
        return;
    }
    if (summary->distortion_sign_flip_fields > 0) {
        const double failed =
            static_cast<double>(summary->distortion_sign_flip_fields);
        summary->trace_ok = 0;
        summary->trace_group_error = fmax(summary->trace_group_error, failed);
        summary->image_group_error = fmax(summary->image_group_error, cfg.fail_merit);
        summary->trace_penalty = cfg.fail_merit;
        summary->uncapped_merit = cfg.fail_merit;
        summary->merit_sum = cfg.fail_merit * cfg.fail_merit;
        summary->merit_weight_sum = 1.0;
        summary->merit = cfg.fail_merit;
        return;
    }
    if (cfg.reject_ray_path_structural_failures &&
        summary->ray_path_structural_failure_fields > 0) {
        const double failed =
            static_cast<double>(summary->ray_path_structural_failure_fields);
        summary->trace_ok = 0;
        summary->trace_group_error = fmax(summary->trace_group_error, failed);
        summary->image_group_error = fmax(summary->image_group_error, cfg.fail_merit);
        summary->trace_penalty = cfg.fail_merit;
        summary->uncapped_merit = cfg.fail_merit;
        summary->merit_sum = cfg.fail_merit * cfg.fail_merit;
        summary->merit_weight_sum = 1.0;
        summary->merit = cfg.fail_merit;
        return;
    }

    double trace_sum = 0.0;
    double trace_weight = 0.0;
    d_add_pseudo_group_term(
        &trace_sum,
        &trace_weight,
        cfg.unified_trace_system_weight,
        d_lower_delta(summary->trace_coverage, 1.0),
        cfg.unified_coverage_tol,
        cfg);
    d_add_pseudo_group_term(
        &trace_sum,
        &trace_weight,
        cfg.unified_trace_min_field_weight,
        d_lower_delta(summary->min_field_coverage, 1.0),
        cfg.unified_coverage_tol,
        cfg);
    d_add_pseudo_group_term(
        &trace_sum,
        &trace_weight,
        cfg.unified_trace_progress_weight,
        d_lower_delta(summary->min_trace_progress, 1.0),
        cfg.unified_progress_tol,
        cfg);
    d_add_pseudo_group_term(
        &trace_sum,
        &trace_weight,
        cfg.unified_trace_chief_progress_weight,
        d_lower_delta(summary->min_chief_progress, 1.0),
        cfg.unified_progress_tol,
        cfg);
    d_add_pseudo_group_term(
        &trace_sum,
        &trace_weight,
        cfg.unified_trace_failed_weight,
        d_finite(summary->failed_ray_weight) ? fmax(0.0, summary->failed_ray_weight) : 1.0e6,
        cfg.unified_failed_weight_tol,
        cfg);
    d_add_pseudo_group_term(
        &trace_sum,
        &trace_weight,
        cfg.unified_trace_failure_type_weight,
        d_finite(terms.trace_failure_type_severity)
            ? fmax(0.0, terms.trace_failure_type_severity)
            : 1.0e6,
        cfg.unified_failed_weight_tol,
        cfg);

    summary->trace_group_error =
        trace_weight > 0.0 ? trace_sum / fmax(1.0e-12, trace_weight) : 0.0;
    summary->constraint_group_error = d_constraint_group_error(
        surfaces,
        cfg,
        summary->min_edge,
        terms.guard_surface_order_edge_deficit_squared) +
        terms.relative_illumination_constraint_squared +
        terms.vignetting_geometry_constraint_squared +
        terms.vignetting_boundary_support_constraint_squared;

    double first_order_sum = 0.0;
    double first_order_weight = 0.0;
    if (cfg.operand_efl) {
        const double delta = d_finite(summary->efl) && d_finite(cfg.target_efl)
                                 ? summary->efl - cfg.target_efl
                                 : 1.0e6;
        d_add_pseudo_group_term(
            &first_order_sum,
            &first_order_weight,
            cfg.unified_first_order_efl_weight,
            delta,
            d_first_order_tolerance(
                cfg,
                0,
                summary->efl,
                cfg.target_efl,
                NAN,
                NAN),
            cfg);
    }
    if (cfg.operand_ttl) {
        d_add_pseudo_group_term(
            &first_order_sum,
            &first_order_weight,
            cfg.unified_first_order_ttl_weight,
            d_upper_delta(summary->ttl, cfg.ttl_upper),
            d_first_order_tolerance(
                cfg,
                1,
                summary->ttl,
                NAN,
                NAN,
                cfg.ttl_upper),
            cfg);
    }
    if (cfg.operand_bfl) {
        d_add_pseudo_group_term(
            &first_order_sum,
            &first_order_weight,
            cfg.unified_first_order_bfl_weight,
            d_lower_delta(summary->bfl, cfg.bfl_lower),
            d_first_order_tolerance(
                cfg,
                2,
                summary->bfl,
                NAN,
                cfg.bfl_lower,
                NAN),
            cfg);
    }
    if (cfg.operand_working_f_number) {
        // Match contribution_delta_for_unified on CPU: Target penalizes
        // deviations on both sides; an upper bound penalizes only excess.
        const double delta = cfg.working_f_number_target_mode
            ? (!d_finite(summary->working_f_number)
                   ? 1.0e6
                   : (d_finite(cfg.working_f_number_target)
                          ? summary->working_f_number - cfg.working_f_number_target
                          : 0.0))
            : d_upper_delta(summary->working_f_number, cfg.working_f_number_upper);
        d_add_pseudo_group_term(
            &first_order_sum,
            &first_order_weight,
            cfg.unified_first_order_working_f_number_weight,
            delta,
            d_first_order_tolerance(
                cfg,
                3,
                summary->working_f_number,
                cfg.working_f_number_target_mode ? cfg.working_f_number_target : NAN,
                NAN,
                cfg.working_f_number_upper),
            cfg);
    }
    summary->first_order_group_error =
        first_order_weight > 0.0
            ? first_order_sum / fmax(1.0e-12, first_order_weight)
            : 0.0;

    double image_sum = terms.image_sum;
    double image_weight = terms.image_weight;
    const double chromatic_family_budget = d_chromatic_family_budget(cfg);
    if (cfg.operand_max_distortion) {
        d_add_pseudo_group_term(
            &image_sum,
            &image_weight,
            cfg.unified_image_distortion_weight,
            d_upper_delta(summary->max_distortion, cfg.max_distortion_upper),
            d_image_tolerance(cfg, 2),
            cfg);
    }
    if (cfg.operand_max_chief_ray_angle) {
        d_add_pseudo_group_term(
            &image_sum,
            &image_weight,
            cfg.unified_image_cra_weight,
            d_upper_delta(summary->max_chief_ray_angle, cfg.max_chief_ray_angle_upper),
            d_image_tolerance(cfg, 3),
            cfg);
    }
    if (cfg.operand_lateral_color) {
        d_add_pseudo_group_term(
            &image_sum,
            &image_weight,
            chromatic_family_budget,
            d_upper_delta(summary->lateral_color, cfg.lateral_color_upper),
            d_image_tolerance(cfg, 4),
            cfg);
    }
    if (cfg.operand_axial_color) {
        d_add_pseudo_group_term(
            &image_sum,
            &image_weight,
            chromatic_family_budget,
            d_upper_delta(
                summary->paraxial_axial_color_focus_shift_mm,
                cfg.axial_color_upper),
            d_image_tolerance(cfg, 6),
            cfg);
    }
    summary->image_group_error =
        image_weight > 0.0 ? image_sum / fmax(1.0e-12, image_weight) : 0.0;

    const double system_coverage =
        d_finite(summary->trace_coverage) ? summary->trace_coverage : 0.0;
    const double min_field =
        d_finite(summary->min_field_coverage) ? summary->min_field_coverage : 0.0;
    const double chief_progress =
        d_finite(summary->min_chief_progress) ? summary->min_chief_progress : 0.0;
    summary->trace_gate =
        d_smoothstep(system_coverage, cfg.unified_gate_system_coverage_lo, 1.0) *
        d_smoothstep(min_field, cfg.unified_gate_min_field_lo, 1.0) *
        d_smoothstep(chief_progress, cfg.unified_gate_chief_progress_lo, 1.0);
    summary->constraint_gate =
        1.0 / (1.0 + fmax(0.0, summary->constraint_group_error));
    const double min_first_order_gate = d_clamp01(cfg.unified_min_first_order_gate);
    summary->first_order_gate =
        min_first_order_gate + (1.0 - min_first_order_gate) * summary->trace_gate;
    summary->image_gate = summary->trace_gate * summary->trace_gate;

    summary->trace_priority_metric = d_trace_priority_metric(*summary, cfg);
    summary->trace_priority_penalty =
        cfg.trace_priority_weight > 0.0
            ? fabs(cfg.trace_priority_weight) * summary->trace_priority_metric
            : 0.0;
    summary->trace_penalty =
        fabs(cfg.group_weight_trace) * summary->trace_group_error;
    summary->constraint_penalty =
        fabs(cfg.group_weight_constraint) * summary->constraint_group_error;
    summary->first_order_penalty =
        summary->first_order_gate *
        fabs(cfg.group_weight_first_order) *
        summary->first_order_group_error;
    summary->image_penalty =
        summary->image_gate *
        fabs(cfg.group_weight_image) *
        summary->image_group_error;
    summary->other_penalty =
        d_finite(glass_proximity_penalty)
            ? fmax(0.0, glass_proximity_penalty) +
                  fmax(0.0, terms.vignetting_soft_prior_penalty) +
                  fmax(0.0, terms.pupil_order_soft_prior_penalty) +
                  fmax(0.0, d_same_sign_tight_triplet_prior(surfaces, cfg))
            : cfg.fail_merit;

    const double total_penalty =
        summary->trace_penalty +
        summary->trace_priority_penalty +
        summary->constraint_penalty +
        summary->first_order_penalty +
        summary->image_penalty +
        summary->other_penalty;
    summary->merit_sum = total_penalty;
    summary->merit_weight_sum = 1.0;
    summary->uncapped_merit = total_penalty;
    if (!d_finite(summary->uncapped_merit)) {
        summary->trace_ok = 0;
        summary->merit = cfg.fail_merit;
    } else {
        summary->merit = fmin(summary->uncapped_merit, cfg.fail_merit);
    }
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__global__ void prepare_geometry_kernel(
    const DeviceSurface* base_surfaces,
    const DeviceVariable* variables,
    const DeviceGlassCandidate* glass_candidates,
    const DeviceField* fields,
    const DeviceWave* waves,
    DeviceConfig cfg,
    const double* flat_population,
    int population_size,
    int dim,
    DeviceSurface* candidate_surfaces,
    DeviceField* candidate_fields,
    int phase) {
    const int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= population_size) {
        return;
    }
    const int surface_count = cfg.surface_count;
    DeviceSurface* surfaces =
        candidate_surfaces + static_cast<long long>(row) * surface_count;
    DeviceField* row_fields =
        candidate_fields
            ? candidate_fields +
                  static_cast<long long>(row) * cfg.field_count
            : nullptr;
    if (phase != 2) {
        for (int i = 0; i < surface_count; ++i) {
            surfaces[i] = base_surfaces[i];
        }
        if (row_fields) {
            for (int i = 0; i < cfg.field_count; ++i) {
                row_fields[i] = fields[i];
            }
        }
        const double* values = flat_population + static_cast<long long>(row) * dim;
        for (int j = 0; j < dim && j < cfg.variable_count; ++j) {
        const DeviceVariable var = variables[j];
        if (row_fields &&
            (var.kind == 4 || var.kind == 5 || var.kind == 14 ||
             var.kind == 15 || var.kind == 16 || var.kind == 17 ||
             var.kind == 18 || var.kind == 19)) {
            if (var.index >= 0 && var.index < cfg.field_count) {
                if (var.kind == 4) {
                    row_fields[var.index].fvdx = values[j];
                } else if (var.kind == 5 || var.kind == 15) {
                    row_fields[var.index].fvdy = values[j];
                } else if (var.kind == 17) {
                    row_fields[var.index].hx = values[j];
                } else if (var.kind == 18) {
                    row_fields[var.index].hy = values[j];
                } else if (var.kind == 19) {
                    row_fields[var.index].fvcx = values[j];
                } else {
                    row_fields[var.index].fvcy = values[j];
                }
            }
            continue;
        }
        if (var.kind == 9) {
            continue;
        }
        if (var.index < 0 || var.index >= surface_count) {
            continue;
        }
        double value = values[j];
        if (var.kind == 1) {
            value = fmax(0.0, value);
        }
        DeviceSurface& surface = surfaces[var.index];
        if (var.kind == 0) {
            surface.radius = value;
            if (surface.kind == kKindPlane && !surface.is_stop &&
                d_finite(value) && fabs(value) >= kDeviceEps) {
                surface.kind = kKindStandard;
            }
        } else if (var.kind == 8) {
            surface.radius = d_radius_from_curvature(value);
            if (surface.kind == kKindPlane && !surface.is_stop &&
                d_finite(surface.radius) && fabs(surface.radius) >= kDeviceEps &&
                fabs(surface.radius) < 0.5 * kDeviceInf) {
                surface.kind = kKindStandard;
            }
        } else if (var.kind == 1) {
            surface.thickness = value;
        } else if (var.kind == 2) {
            surface.nd = value;
            surface.material_index_offset = -1;
            if (!(d_finite(surface.vd) && surface.vd > 0.0)) {
                surface.vd = 50.0;
            }
        } else if (var.kind == 3) {
            surface.vd = value;
            surface.material_index_offset = -1;
            if (!(d_finite(surface.nd) && surface.nd > 1.0)) {
                surface.nd = 1.5;
            }
        } else if (var.kind == 6) {
            surface.conic = value;
        } else if (var.kind == 7 && var.term >= 0 &&
                   var.term < kMaxDeviceAsphereCoefficients) {
            surface.coeffs[var.term] = value;
            surface.coeff_count = max(surface.coeff_count, var.term + 1);
            if (surface.kind == kKindStandard ||
                (surface.kind == kKindPlane && !surface.is_stop)) {
                surface.kind = kKindEvenAsphere;
            }
        } else if (var.kind == 10) {
            surface.semi_aperture = value;
        } else if (var.kind == 11) {
            surface.mechanical_aperture = value;
        } else if (var.kind == 12) {
            surface.aperture_radius = value;
        } else if (var.kind == 13 && var.term >= 0 &&
                   var.term < kMaxDeviceMaterialIndices) {
            surface.evaluator_material_indices[var.term] = value;
            surface.evaluator_material_index_mask |= 1 << var.term;
        }
    }
        // Automatic meridional vignetting stores eta temporarily in FVCY and
        // reconstructs the physical shift only after every candidate
        // coordinate has been loaded.  This keeps the result independent of
        // variable order and guarantees |FVCY| <= FVDY on-device.
        if (row_fields) {
            for (int j = 0; j < dim && j < cfg.variable_count; ++j) {
                const DeviceVariable var = variables[j];
                if (var.kind != 16 || var.index < 0 || var.index >= cfg.field_count) {
                    continue;
                }
                DeviceField& field = row_fields[var.index];
                const double magnitude = fmin(1.0, fmax(0.0, field.fvdy));
                const double asymmetry = fmin(1.0, fmax(-1.0, field.fvcy));
                field.fvdy = magnitude;
                field.fvcy = asymmetry * magnitude;
            }
        }
        // Match apply_mixed_variables_row(): continuous material variables are
        // applied first, then a changed discrete choice replaces the material.
        // The initial choice intentionally preserves the base/continuous material.
        for (int j = 0; j < dim && j < cfg.variable_count; ++j) {
        const DeviceVariable var = variables[j];
        if (var.kind != 9 || !glass_candidates ||
            var.glass_candidate_count <= 0 ||
            var.glass_surface_index < 0 ||
            var.glass_surface_index >= surface_count) {
            continue;
        }
        int choice = static_cast<int>(floor(values[j] + 0.5));
        choice = max(0, min(choice, var.glass_candidate_count - 1));
        if (choice == var.glass_initial_choice) {
            continue;
        }
        const DeviceGlassCandidate candidate =
            glass_candidates[var.glass_candidate_offset + choice];
        DeviceSurface& surface = surfaces[var.glass_surface_index];
        surface.nd = candidate.nd;
        surface.vd = candidate.vd;
        surface.material_index_offset = -1;
        surface.evaluator_material_index_mask = 0;
        for (int wave_index = 0;
             wave_index < cfg.wavelength_count &&
             wave_index < kMaxDeviceMaterialIndices;
             ++wave_index) {
            surface.evaluator_material_indices[wave_index] =
                candidate.indices[wave_index];
            surface.evaluator_material_index_mask |= 1 << wave_index;
        }
        }
    }
    if (phase == 1) {
        return;
    }
    // CPU parity: for EPD/NA/F-number systems using real-stop aiming, size the
    // physical stop from the primary-wavelength EPD edge ray after all candidate
    // geometry/material variables have been applied and before aiming models
    // are built.  d_aperture_stop_radius() then reuses this stored radius.
    if (phase == 0) {
        d_update_asphere_slope_root_domains(surfaces, cfg);
    }
    d_initialize_real_stop_aperture(surfaces, cfg);
    if (cfg.dynamic_apertures) {
        if (cfg.aiming_mode != 1 && cfg.aiming_mode != 5) {
            d_update_clear_apertures_from_trace(
                surfaces, row_fields ? row_fields : fields, waves, cfg);
        } else if (cfg.aiming_mode == 5) {
            d_sync_stop_aperture_to_aiming_radius(surfaces, cfg);
        }
    } else {
        d_sync_stop_aperture_to_aiming_radius(surfaces, cfg);
        // Static-aperture CPU scoring preserves the serialized/materialized
        // mechanical apertures and only synchronizes the stop.  Rebuilding
        // element groups here changed edge-thickness manufacturing terms even
        // though the optical state and aiming mode were otherwise identical.
        // Dynamic-aperture paths still rebuild the groups after their traced
        // clear-aperture update.
    }
    if (phase == 0) {
        d_update_asphere_slope_root_domains(surfaces, cfg);
    }
}
} // namespace
} // namespace optiland
