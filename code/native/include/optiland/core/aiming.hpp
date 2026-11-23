#pragma once
#include <string>

namespace optiland {
enum class ApertureType {
    EntrancePupilDiameter,
    ImageFNumber,
    ObjectNumericalAperture,
    FloatByStopSize,
};

enum class VignettingBoundarySupportContract {
    SelectiveApertureContact = 0,
    GlobalTrimRealizability = 1,
};

enum class RayAimingMode {
    Direct = 0,
    Paraxial = 1,
    Iterative = 2,
    Robust = 3,
    AdaptivePilotGrid = 4,
    CalibratedParaxial = 5,
    ThreeStep = 6,
};

enum class RayAimingStopRadiusSource {
    Paraxial = 0,
    Real = 1,
};

struct AdaptivePilotGridConfig {
    bool enabled = true;
    bool try_iterative_first = true;
    bool try_iterative_for_each_ray = true;
    int iterative_failure_sample_limit = 0;
    bool require_final_convergence = true;
    double target_field_direction_tolerance = 1.0e-10;
    bool use_chief_gate = true;
    bool use_primary_wave_as_anchor = true;
    bool use_previous_field_chief = true;
    bool use_previous_field_extrapolation = true;
    double max_field_extrapolation_factor = 2.0;
    bool use_previous_wave_seed = true;
    bool use_previous_field_seed = true;
    bool use_previous_pupil_seed = true;
    bool use_chief_jacobian_seed = true;
    int coarse_grid_n = 13;
    int fine_grid_n = 5;
    int nearest_k = 3;
    int max_coarse_expansions = 3;
    int max_fine_expansions = 2;
    double coarse_min_halfwidth_fraction = 0.10;
    double coarse_max_halfwidth_fraction = 0.60;
    double fine_min_halfwidth_fraction = 0.03;
    double fine_max_halfwidth_fraction = 0.25;
    double fine_default_halfwidth_fraction = 0.06;
    double jacobian_probe_fraction = 0.03;
    double jacobian_safety_factor = 1.4;
    bool allow_delaunay_interpolation = false;
    bool allow_barycentric_interpolation = false;
    bool use_multistart = true;
    bool emit_diagnostics = true;
};

struct CalibratedParaxialConfig {
    bool enabled = true;
    std::string sampling_mode = "chief_edge_interpolation";
    bool allow_grid_upgrade = true;
    int pilot_grid_n = 3;
    double jacobian_probe_fraction = 0.02;
    double min_probe_step_mm = 0.03;
    double max_probe_step_mm = 0.30;
    double max_condition_number = 1000.0;
    double fit_rms_tolerance_stop_fraction = 0.03;
    // Interior chief/edge interpolation probes are diagnostic-only. Model
    // validity is determined by the chief ray and the four calibrated edges.
    int validation_sample_count = 5;
    double validation_residual_stop_fraction = 0.05;
    bool use_previous_field_model = true;
    bool use_previous_wave_model = true;
    std::string chief_anchor_mode = "abcd_budget_angle_bridge_fast";
    int budget_chief_iterations = 8;
    std::string budget_chief_solver = "hybrid";
    double budget_chief_residual_stop_fraction = 1.0e-6;
    // Bound non-converged budget-chief solutions by
    // budget_chief_accept_stop_fraction. The historical "loose_stop" name is
    // retained as a bounded-residual compatibility alias in the tracers.
    std::string budget_chief_acceptance = "residual";
    double budget_chief_accept_stop_fraction = 0.05;
    double budget_chief_min_a = 1.0e-6;
    double budget_chief_field_continuation_angle_deg = 75.0;
    bool budget_chief_ignore_aperture_clip = true;
    double budget_chief_skip_abcd_angle_deg = 60.0;
    std::string fallback = "none";
    int correction_iterations = 0;
    bool edge_calibration_enabled = true;
    int edge_calibration_iterations = 8;
    std::string edge_calibration_solver = "secant";
    double edge_calibration_residual_stop_fraction = 1.0e-6;
    double edge_calibration_weight_power = 2.0;
};

struct ThreeStepAimingConfig {
    bool enabled = true;
    std::string sampling_mode = "chief_edge_interpolation";
    int chief_reverse_trial_rays = 33;
    double chief_reverse_max_angle_deg = 89.0;
    int edge_passes = 2;
    int meridional_iterations = 8;
    int sagittal_iterations = 8;
    std::string edge_solver = "secant";
    double edge_residual_stop_fraction = 1.0e-6;
    double edge_initial_radius_fraction = 1.0;
    double accept_stop_fraction = 0.05;
    int correction_iterations = 0;
    bool sample_stop_residual_debug = false;
    std::string fallback = "adaptive_pilot_grid";
};

struct RayAimingConfig {
    RayAimingMode mode = RayAimingMode::Paraxial;
    RayAimingStopRadiusSource stop_radius_source = RayAimingStopRadiusSource::Paraxial;
    int max_iter = 20;
    double tolerance = 1.0e-8;
    double start_offset = -1.0;
    bool require_convergence = false;
    // Robust's calibrated/exact CPU implementation always rejects an
    // unconverged requested ray, matching upstream's per-ray failure contract.
    int continuation_steps = 8;
    CalibratedParaxialConfig calibrated_paraxial;
    ThreeStepAimingConfig three_step;
    AdaptivePilotGridConfig adaptive_pilot_grid;
};

struct Aperture {
    ApertureType type = ApertureType::EntrancePupilDiameter;
    double value = 10.0;
};


}  // namespace optiland
