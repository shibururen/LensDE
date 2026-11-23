#pragma once
#include "optiland/core/variables.hpp"
#include "optiland/core/merit_operands.hpp"
#include "optiland/core/aiming.hpp"
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace optiland {
struct MeritConfig {
    std::string merit_model = "weighted_rms";
    double target_efl = 0.0;
    double target_ttl = 0.0;
    double target_bfl = 0.0;
    double spot_target = 0.0;
    double weight_efl = 1.0;
    double weight_ttl = 0.4;
    double weight_bfl = 0.5;
    double weight_spot = 5.0;
    double weight_edge = 0.5;
    double fail_merit = 1.0e12;
    double min_edge_thickness = 0.0;
    double glass_center_thickness_lower = 0.5;
    double glass_center_thickness_upper = 10.0;
    double air_center_thickness_lower = 0.1;
    double air_center_thickness_upper = 500.0;
    double glass_edge_thickness_lower = 0.5;
    double glass_edge_thickness_upper = 10.0;
    double air_edge_thickness_lower = 0.1;
    double air_edge_thickness_upper = 500.0;
    // Soft structural prior for three consecutive air-spaced glass elements.
    // A window is active only when both intervening air gaps are close to the
    // configured air center-thickness lower bound and all six boundary
    // curvatures have the same non-zero sign.  Overlapping windows accumulate.
    double structural_prior_tight_triplet_weight = 5.0;
    double structural_prior_gap_tolerance = 0.05;
    double structural_prior_curvature_epsilon = 1.0e-9;
    // Soft merit prior on primary-wave meridional ordering (including the
    // existing exact center/edge guards). The reference order is measured at
    // the first optical surface, so nonlinear pupil aiming itself is not a
    // violation. Image-space air auxiliaries lie outside the checked train.
    double pupil_order_prior_weight = 20.0;
    double pupil_order_jacobian_floor = 0.05;
    double pupil_order_soft_tolerance = 0.25;
    // Legacy "hard" names are retained for file/diagnostic compatibility.
    // These thresholds only count pronounced reversals; they never mark a
    // trace invalid or impose a hard stage-handoff gate.
    double pupil_order_hard_jacobian = -1.0e-4;
    double pupil_order_hard_max_progress = 1.0;
    int pupil_order_hard_consecutive_surfaces = 1;
    double pupil_order_min_field_radius = 0.75;
    // Default center/edge/ratio thickness constraints describe optimization
    // degrees of freedom.  When enabled, fixed mechanical intervals such as a
    // cover glass are excluded; their explicit optical operands (for example
    // aggregate BFL) remain active.
    bool thickness_constraints_variable_only = false;
    std::vector<std::size_t> thickness_variable_indices;
    double atc_lower = 0.1;
    double atc_upper = 0.0;
    double ate_lower = 0.1;
    double ate_upper = 0.0;
    double bl0_lower = 0.0;
    double bl0_upper = 0.9;
    // Minimum absolute angle between the meridional surface tangent and the
    // optical axis.  A plane is 90 deg; a hemispherical rim approaches 0 deg.
    // A non-positive lower bound disables the constraint.
    double surface_tangent_angle_lower_deg = 30.0;
    int surface_tangent_angle_samples = 32;
    double quality_spot_scale = 0.01;
    double quality_wavefront_scale = 0.50;
    double quality_efl_rel_tol = 0.01;
    double quality_ttl_rel_tol = 0.02;
    double quality_bfl_rel_tol = 0.02;
    // Absolute working-F/# transition width used by both weighted and unified
    // merit models.
    double quality_working_f_number_tol = 0.01;
    double quality_length_abs_tol = 0.1;
    double quality_edge_tol = 0.05;
    double quality_ratio_tol = 0.01;
    double quality_bound_tol = 0.05;
    // Optional user-facing vignetting requirement. A finite value in (0, 1]
    // enables one global minimum projected-relative-illumination constraint.
    // Field dependence is handled by automatically generated FVCY/FVDY bounds
    // rather than auxiliary inner-field illumination targets.
    double minimum_relative_illumination =
        std::numeric_limits<double>::quiet_NaN();
    // Optional RI supervision only; trace/image/physical matching stay full-field.
    // Empty selectors preserve the historical all-field constraint. Radii are
    // reference field radii mapped to the nearest actual field at evaluation.
    std::vector<std::size_t> minimum_relative_illumination_field_indices;
    std::vector<double> minimum_relative_illumination_field_radii;
    double quality_relative_illumination_tol = 0.01;
    // Automatic meridional-vignetting geometry and scheduling.  The hard
    // search-space parameterization guarantees |FVCY| <= FVDY; this merit
    // guard also protects explicit legacy variables and imported systems.
    double vignetting_meridional_excess_weight = 25.0;
    // Quartic field scheduling is a weak preference only.  The GUI uses a
    // quadratic hard bound so useful intermediate-field vignetting remains
    // reachable.
    double vignetting_soft_prior_weight = 0.25;
    double vignetting_soft_field_power = 4.0;
    double vignetting_loss_budget_fraction = 2.0 / 3.0;
    double vignetting_edge_fvdy_cap = 0.5;
    // The legacy selective-contact contract asks the opposite retained edge
    // and chief ray to stay inside the target edge.  GlobalTrimRealizability
    // instead asks the target edge to own the all-field retained envelope on
    // at least one surface and uses the existing chief/boundary pair to
    // predict a non-empty physical trim window.  Both checks reuse the fixed
    // chief/+/-X/+/-Y guard budget and trace no additional hot-path rays.
    VignettingBoundarySupportContract vignetting_boundary_support_contract =
        VignettingBoundarySupportContract::GlobalTrimRealizability;
    double vignetting_boundary_support_weight = 25.0;
    double vignetting_boundary_support_tolerance = 0.02;
    double vignetting_boundary_support_activation = 0.02;
    double vignetting_boundary_support_transition = 0.05;
    double vignetting_boundary_support_selectivity_slope = 0.05;
    // Physical-cut variant of GlobalTrimRealizability.  It converts the
    // clearance between a retained
    // meridional boundary ray and the adaptive circular clear aperture back to
    // pupil-cut units using the already traced chief/boundary slope.  The raw,
    // non-saturating residual is the requested cut that would still pass the
    // materialized aperture.  It reuses the ordinary 4x8 aperture/support trace,
    // adds no ray or kernel.  The GUI enables it for the strict Outer phase
    // and every downstream optimization stage; the loose Outer phase writes
    // an explicit false value to preserve early global freedom.
    bool vignetting_physical_cut_match_enabled = false;
    // Experimental aperture contract: size automatic clear apertures from the
    // same pupil distribution used by the merit trace.  CUDA can fuse this
    // envelope reduction into the existing spot-ray pass; CPU paths retain a
    // separate trace for parity while using the same ring count.
    bool vignetting_merit_aperture_envelope_experimental = false;
    double quality_surface_tangent_angle_tol_deg = 5.0;
    // Soft transition width outside the configured conic interval.  A unit
    // excursion therefore contributes one squared constraint residual.
    double quality_conic_coefficient_tol = 1.0;
    double quality_distortion_tol = 0.01;
    std::string distortion_model = "auto";
    double quality_chief_ray_angle_tol = 5.0;
    double quality_lateral_color_tol = 0.01;
    double quality_axial_color_tol = 0.025;
    // Allowed field-wise spread between the best and worst monochromatic
    // RMS spot radii.  This is intentionally tighter than quality_spot_scale:
    // it measures spectral consistency, not absolute image size.
    double quality_spectral_spot_spread_tol = 0.0015;
    int num_rays = 3;
    int pupil_arms = 6;
    // Optionally increase the pupil sample only for fields at or beyond this
    // normalized radial field coordinate.  A threshold above 1.0, or
    // non-positive elevated dimensions, disables the override.
    double elevated_field_sampling_min_radius = 2.0;
    int elevated_field_num_rays = 0;
    int elevated_field_pupil_arms = 0;
    std::string pupil_distribution = "gaussian_ring_arm";
    // CODE V-style half-cell rectangular pupil grid.  A non-positive interval
    // selects the configured profile: 0.385 for "spherical" (the Code V
    // profile default) and 0.22 for "aspherical".  "auto" remains accepted in JSON/CLI
    // for compatibility and inspects the actual surfaces.  The two exponents implement
    // w(x,y) = 1 / (x^2 + y^2)^A for the corresponding RMS calculation.
    std::string codev_sampling_profile = "spherical";
    double codev_ray_interval = 0.0;
    double codev_obscuration = 0.0;
    // Standard spot RMS uses uniform pupil-area weighting.  CODE V's
    // historical transverse-aberration default (A=0.5) remains configurable,
    // but A=0 is the project default because validation uses Gaussian/
    // Hexapolar spot diagrams.
    double codev_spot_aperture_weight = 0.0;
    double codev_wavefront_aperture_weight = 0.0;
    // Legacy name retained for project compatibility.  Symmetry reduction is
    // applied to every pupil distribution whose generated point set contains
    // the required reflected/rotated partners.
    bool codev_use_symmetry = true;
    // Relative image-quality weight for a field at normalized radial
    // coordinate r: 1 + (edge_weight - 1) * r^exponent.  "uniform" disables
    // the multiplier.  The unified merit normalizes by the total field
    // weight, so this changes field balance without changing the group scale.
    std::string image_field_weight_mode = "edge_power";
    double image_field_edge_weight = 2.0;
    double image_field_weight_exponent = 2.0;
    std::string wavefront_reference = "centroid_sphere";
    unsigned int sampling_seed = 1;
    bool dynamic_apertures = false;
    int clear_aperture_rings = 4;
    // Aperture sizing is independent of image/spot pupil sampling.
    int clear_aperture_arms = 8;
    double clear_aperture_margin = 1.02;
    // A positive relative-illumination requirement enables vignetting
    // optimization.  In that contract, every non-stop automatic clear
    // aperture is the traced vignetted-ray envelope itself; adding the
    // ordinary 2% engineering margin changes the physical pupil that the
    // vignetting coefficients describe.
    double effective_clear_aperture_margin() const {
        return vignetting_optimization_enabled()
                   ? 1.0
                   : clear_aperture_margin;
    }
    bool vignetting_optimization_enabled() const {
        return std::isfinite(minimum_relative_illumination) &&
               minimum_relative_illumination > 0.0 &&
               minimum_relative_illumination <= 1.0;
    }
    TraceFailurePolicy trace_policy = TraceFailurePolicy::Strict;
    double min_field_coverage = 1.0;
    double min_system_coverage = 1.0;
    double min_coverage_for_image_merit = 0.5;
    double trace_failure_weight = 1.0;
    double coverage_failure_weight = 10.0;
    double aim_failure_weight = 1.0;
    double tir_failure_weight = 10.0;
    double aperture_failure_weight = 2.0;
    double surface_miss_weight = 5.0;
    double backward_failure_weight = 10.0;
    bool require_chief_ray = false;
    bool require_off_axis_field_coverage = false;
    double hard_trace_gate_weight = 1000.0;
    double trace_progress_weight = 0.0;
    double chief_progress_weight = 0.0;
    double trace_priority_weight = 0.0;
    bool allow_soft_aim_rays_for_spot = false;
    double soft_aim_residual_for_spot = 1.0e-4;
    double unified_loss_delta = 2.0;
    double group_weight_trace = 50.0;
    double group_weight_constraint = 1.0;
    double group_weight_first_order = 20.0;
    double group_weight_image = 30.0;
    double unified_coverage_tol = 0.05;
    double unified_progress_tol = 0.10;
    double unified_failed_weight_tol = 1.0;
    double unified_trace_system_weight = 20.0;
    double unified_trace_min_field_weight = 40.0;
    double unified_trace_progress_weight = 8.0;
    double unified_trace_chief_progress_weight = 25.0;
    double unified_trace_failed_weight = 12.0;
    double unified_trace_failure_type_weight = 8.0;
    double unified_edge_tol = 0.10;
    double unified_air_gap_tol = 0.10;
    double unified_center_thickness_tol = 0.20;
    double unified_ttl_tol = 1.0;
    double unified_bfl_tol = 1.0;
    double unified_constraint_edge_weight = 25.0;
    double unified_constraint_air_gap_weight = 12.0;
    double unified_constraint_center_weight = 12.0;
    double unified_constraint_bfl_weight = 18.0;
    double unified_constraint_ttl_weight = 12.0;
    double unified_constraint_curvature_weight = 4.0;
    double unified_constraint_aperture_weight = 15.0;
    double unified_efl_rel_tol = 0.01;
    double unified_length_abs_tol = 0.10;
    double unified_first_order_efl_weight = 1.0;
    double unified_first_order_ttl_weight = 0.4;
    double unified_first_order_bfl_weight = 0.5;
    double unified_first_order_working_f_number_weight = 1.0;
    double unified_first_order_image_height_weight = 6.0;
    double unified_image_mean_spot_weight = 3.0;
    double unified_image_single_ray_spot_tail_weight = 0.012;
    // For fields at or beyond this normalized radial coordinate, augment the
    // absolute spot-tail operand with eight unit-pupil edge probes interleaved
    // between the default cardinal/diagonal 8-arm directions.  Values above
    // 1.0 disable the selective enhancement without changing the tail weight.
    double single_ray_spot_tail_extra_edge_min_field_radius = 0.85;
    double unified_image_wavefront_weight = 1.0;
    double unified_image_max_spot_weight = 10.0;
    double unified_image_field_balance_weight = 4.0;
    double unified_image_chromatic_weight = 5.0;
    double unified_image_distortion_weight = 6.0;
    double unified_image_cra_weight = 5.0;
    double unified_gate_system_coverage_lo = 0.85;
    double unified_gate_min_field_lo = 0.50;
    double unified_gate_chief_progress_lo = 0.70;
    double unified_min_first_order_gate = 0.25;
    bool reject_ray_path_structural_failures = true;
    int axis_cross_allow_count = 1;
    double axis_crossing_epsilon = 1.0e-3;
    double center_early_focus_guard_mm = 0.5;
    double center_early_focus_edge_ratio = 0.75;
    std::vector<BoundConstraint> bounds;
    std::vector<MeritOperand> operands;
    std::vector<GlassProximityConstraint> glass_proximity;
};

// Bind automatic thickness manufacturability constraints to the active
// optimization variables.  Front ends should call this after loading their
// variable list and before scoring the initial system.
void configure_variable_thickness_constraints(
    MeritConfig* config,
    const std::vector<VariableSpec>& variables);


}  // namespace optiland
