#pragma once

#include "optiland/core.hpp"

#include <vector>

namespace optiland {

struct SpotStats {
    double centroid_x = 0.0;
    double centroid_y = 0.0;
    double rms_radius = 0.0;
    double max_radius = 0.0;
    double single_ray_max_radius = 0.0;
    std::size_t valid_rays = 0;
    std::size_t attempted_rays = 0;
    std::size_t failed_rays = 0;
    // Calibrated-paraxial pupil interpolation can place an otherwise
    // well-aimed internal sample just outside the physical stop.  Such a
    // sample is not evidence that the optical system cannot trace; keep it
    // observable while excluding it from trace-failure coverage.
    std::size_t sampling_rejected_rays = 0;
    double valid_weight = 0.0;
    double failed_weight = 0.0;
    double sampling_rejected_weight = 0.0;
    double total_weight = 0.0;
    double coverage = 1.0;
    double trace_progress = 1.0;
    double chief_progress = 1.0;
    double trace_penalty = 0.0;
    double aim_penalty = 0.0;
    double backward_penalty = 0.0;
    double backward_weight = 0.0;
    std::size_t backward_rays = 0;
    bool chief_ok = true;
    int axis_cross_count = 0;
    int axis_cross_excess = 0;
    int center_early_focus = 0;
    int chief_entry_side_failed = 0;
    int structural_failure = 0;
    std::size_t single_ray_spot_guard_failed_rays = 0;
    double single_ray_spot_guard_failure_squared = 0.0;
    double single_ray_guard_surface_order_edge_deficit = 0.0;
    double min_pupil_order_jacobian =
        std::numeric_limits<double>::quiet_NaN();
    double pupil_order_worst_surface_progress =
        std::numeric_limits<double>::quiet_NaN();
    double pupil_order_prior_violation = 0.0;
    int pupil_order_fold = 0;
    int pupil_order_hard_fold = 0;
    std::vector<double> ray_spot_residuals;
    std::vector<double> single_ray_spot_guard_residuals;
    std::vector<double> single_ray_spot_guard_failure_strengths;
};

struct FieldSpot {
    Field field;
    double wavelength_um = 0.0;
    SpotStats stats;
    // Sparse primary-wave estimate of projected exit-pupil solid angle.
    // The area is normalized across fields after all fields are traced.
    double relative_illumination_proxy_area =
        std::numeric_limits<double>::quiet_NaN();
    double relative_illumination_proxy =
        std::numeric_limits<double>::quiet_NaN();
    // Derived from the same rays as the pooled polychromatic spot.
    double worst_wavelength_rms_radius = std::numeric_limits<double>::quiet_NaN();
    double spectral_rms_spread = std::numeric_limits<double>::quiet_NaN();
};

struct WavefrontStats {
    double rms_waves = std::numeric_limits<double>::quiet_NaN();
    double mean_waves = std::numeric_limits<double>::quiet_NaN();
    double peak_to_valley_waves = std::numeric_limits<double>::quiet_NaN();
    double single_ray_max_abs_waves = std::numeric_limits<double>::quiet_NaN();
    std::size_t valid_rays = 0;
    std::size_t attempted_rays = 0;
    std::size_t failed_rays = 0;
    std::size_t sampling_rejected_rays = 0;
    double valid_weight = 0.0;
    double failed_weight = 0.0;
    double sampling_rejected_weight = 0.0;
    double total_weight = 0.0;
    double coverage = 1.0;
    bool chief_ok = true;
    std::vector<double> single_ray_wavefront_guard_residuals;
};

struct FieldWavefront {
    Field field;
    double wavelength_um = 0.0;
    std::size_t field_index = 0;
    std::size_t wavelength_index = 0;
    WavefrontStats stats;
};

struct ChiefRayMetrics {
    double max_distortion = std::numeric_limits<double>::quiet_NaN();
    std::string distortion_model = "auto";
    double max_chief_ray_angle = std::numeric_limits<double>::quiet_NaN();
    double chief_ray_path_bend_mean_deg = std::numeric_limits<double>::quiet_NaN();
    double chief_ray_path_bend_mean_off_axis_deg = std::numeric_limits<double>::quiet_NaN();
    double chief_ray_path_bend_max_deg = std::numeric_limits<double>::quiet_NaN();
    double chief_ray_path_bend_max_step_deg = std::numeric_limits<double>::quiet_NaN();
    double chief_ray_path_excess_mean = std::numeric_limits<double>::quiet_NaN();
    double chief_ray_path_excess_max = std::numeric_limits<double>::quiet_NaN();
    double lateral_color = std::numeric_limits<double>::quiet_NaN();
    int distortion_sign_flip_fields = 0;
};

// ZEMAX-compatible on-axis working F/#.  The image-space numerical aperture
// is obtained from the real chief ray and the four cardinal marginal rays at
// the primary wavelength: F/# = 1 / (2 * NA).  Unlike EFL / entrance-pupil
// diameter, this includes the real pupil mapping performed by the system.
struct WorkingFNumberMetrics {
    double working_f_number = std::numeric_limits<double>::quiet_NaN();
    double image_space_numerical_aperture =
        std::numeric_limits<double>::quiet_NaN();
    std::size_t valid_marginal_rays = 0;
};

// Relative-illumination integration follows the image-space direction-cosine
// definition used by optical-design programs: the projected solid angle of
// the exit pupil is the mapped area in (l, m) cosine space.  Stored field
// vignetting factors may optionally be removed for the analysis while real
// surface apertures remain active.
struct RelativeIlluminationConfig {
    int ray_density = 20;
    int field_density = 21;
    double wavelength_um = std::numeric_limits<double>::quiet_NaN();
    double scan_hx = 0.0;
    double scan_hy = 1.0;
    bool remove_vignetting_factors = true;
    bool normalize_to_maximum = true;
};

struct RelativeIlluminationPoint {
    double field_hx = 0.0;
    double field_hy = 0.0;
    double field_angle_deg = 0.0;
    double wavelength_um = 0.0;
    double projected_cosine_area = std::numeric_limits<double>::quiet_NaN();
    double relative_illumination = std::numeric_limits<double>::quiet_NaN();
    double effective_f_number = std::numeric_limits<double>::quiet_NaN();
    std::size_t attempted_rays = 0;
    std::size_t valid_rays = 0;
    std::size_t integrated_samples = 0;
    std::size_t orientation_flip_samples = 0;
};

struct RelativeIlluminationResult {
    RelativeIlluminationConfig config;
    double wavelength_um = 0.0;
    double normalization_area = std::numeric_limits<double>::quiet_NaN();
    std::size_t normalization_field_index = 0;
    std::vector<RelativeIlluminationPoint> points;
};

// Reconstruct field vignetting from the hard clear apertures seen by the real
// ray tracer.  Project coordinates follow
//
//   P'x = fvcx + (1 - fvdx) * Px
//   P'y = fvcy + (1 - fvdy) * Py
//
// so Zemax VDX/VDY map to fvcx/fvcy and Zemax VCX/VCY map to
// fvdx/fvdy.  Automatic semi/mechanical apertures are deliberately excluded;
// only aperture_radius (or a fixed clear semi-aperture fallback) clips rays.
struct AutomaticVignettingConfig {
    // Reference-pupil mapping in which the returned factors are expressed.
    // Direct/Paraxial scan their launch/entrance-pupil coordinates; iterative
    // aiming modes scan their real stop-target coordinates.  The hard-aperture
    // boundary search is iterative in either case.
    RayAimingMode aiming_mode = RayAimingMode::AdaptivePilotGrid;
    // A boundary solve may intentionally use exact real-ray stop targeting
    // while the serialized handoff remains on a faster calibrated model.  In
    // that case this explicit validation mode replaces per-ray fallback: the
    // measurement and the handoff contracts are both visible and independently
    // strict.
    RayAimingMode validation_aiming_mode = RayAimingMode::AdaptivePilotGrid;
    bool use_distinct_validation_aiming_mode = false;
    std::size_t wavelength_index = std::numeric_limits<std::size_t>::max();
    // Set Vignetting acts on the current physical stop.  When the stop already
    // has a positive semi-aperture (fixed or automatic, for example imported
    // ZMX DIAM/CLAP), use it as the normalized pupil radius without changing
    // the caller's aperture specification.
    // Disable this only when the stop radius must be freshly derived from the
    // system aperture specification.
    bool use_current_stop_surface_radius = true;
    int coarse_samples = 33;
    // Even, cell-centered pupil grid used to estimate the centroid of rays
    // that pass every hard aperture.  The default is calibrated against the
    // OpticStudio Set Vignetting result for the supplied multi-aperture lens.
    int centroid_grid_cells = 98;
    int boundary_iterations = 44;
    double pupil_tolerance = 1.0e-11;
    double aperture_tolerance_fraction = 1.0e-10;
    bool require_full_trace = false;
    // Solve the common hard-aperture pupil transmitted by every configured
    // wavelength.  The interactive Set Vignetting operation keeps its
    // historical primary-wavelength default; sparse-to-dense field bridges
    // enable this explicitly because their aperture contract is polychromatic.
    bool all_wavelengths = false;
    // Numerical recovery is useful for the interactive aperture estimator,
    // but a strict handoff must be solved by the serialized aiming mode
    // itself so that the same coefficients remain valid after the bridge.
    bool allow_aiming_fallback = true;
    // Exact APG batches use worker-local sessions containing only the current
    // field/wavelength chief and pilot cache. Cross-request previous-* seeds
    // are intentionally excluded; failed independent targets are replayed by
    // the original serial continuation/session path.
    bool use_batch_apg = false;
    int batch_apg_workers = 1;
    bool audit_batch_apg = false;
    // After measuring the hard-aperture envelope, validate the serialized
    // field factors with the selected aiming mode and all requested edge rays.
    // If a boundary lies exactly on a limiting surface, contract both pupil
    // axes just enough to obtain a strict, fully traceable handoff.
    bool validate_final_aiming_contract = false;
};

struct AutomaticVignettingFieldResult {
    std::size_t field_index = 0;
    Field original_field;
    Field solved_field;
    double pupil_x_min = -1.0;
    double pupil_x_max = 1.0;
    double pupil_y_min = -1.0;
    double pupil_y_max = 1.0;
    double pupil_center_residual = std::numeric_limits<double>::infinity();
    int pupil_center_iterations = 0;
    int x_min_limiting_surface = -1;
    int x_max_limiting_surface = -1;
    int y_min_limiting_surface = -1;
    int y_max_limiting_surface = -1;
    double pupil_x_contraction = 1.0;
    double pupil_contraction = 1.0;
    std::string seed_source;
    std::size_t connected_pupil_samples = 0;
    std::size_t branch_reseed_attempts = 0;
    std::size_t branch_reseed_successes = 0;
    std::size_t pupil_evaluations = 0;
    std::size_t envelope_trace_calls = 0;
    std::size_t hard_aperture_surface_tests = 0;
    double aiming_seconds = 0.0;
    double envelope_trace_seconds = 0.0;
    double centroid_seconds = 0.0;
    double centered_sections_seconds = 0.0;
    std::size_t batch_apg_evaluations = 0;
    std::size_t batch_apg_aim_failures = 0;
    std::size_t batch_apg_serial_recoveries = 0;
    std::size_t batch_apg_aimed_mismatches = 0;
    std::size_t batch_apg_feasible_mismatches = 0;
    std::size_t batch_apg_limiting_surface_mismatches = 0;
    std::size_t batch_apg_seed_mismatches = 0;
    std::size_t batch_apg_workers = 1;
    double batch_apg_max_seed_delta = 0.0;
    double batch_apg_max_boundary_ratio_delta = 0.0;
    bool success = false;
    std::string message;
};

struct AutomaticVignettingResult {
    bool success = false;
    std::size_t wavelength_index = 0;
    double wavelength_um = std::numeric_limits<double>::quiet_NaN();
    std::vector<AutomaticVignettingFieldResult> fields;
    double final_validation_seconds = 0.0;
    double total_seconds = 0.0;
    std::string message;
};

SpotStats compute_spot_stats(const std::vector<Ray>& rays);
SpotStats compute_spot_stats(
    const std::vector<Ray>& rays,
    const MeritConfig& config,
    std::size_t surface_count = 0,
    RayAimingMode aiming_mode = RayAimingMode::Direct,
    int stop_surface_index = -1);
std::vector<FieldSpot> spot_diagram_stats(const LensSystem& system, int rings);
std::vector<FieldSpot> spot_diagram_stats(const LensSystem& system, const MeritConfig& config);
std::vector<FieldSpot> polychromatic_field_spot_stats(const LensSystem& system, const MeritConfig& config);
std::vector<FieldWavefront> wavefront_stats(const LensSystem& system, const MeritConfig& config);
double mean_rms_spot(const LensSystem& system, int rings, double* max_spot = nullptr);
double mean_rms_spot(const LensSystem& system, const MeritConfig& config, double* max_spot = nullptr);
double mean_rms_wavefront(const LensSystem& system, const MeritConfig& config, double* max_wavefront = nullptr);
ChiefRayMetrics chief_ray_metrics(const LensSystem& system);
ChiefRayMetrics chief_ray_metrics(const LensSystem& system, const MeritConfig& config);
WorkingFNumberMetrics working_f_number_metrics(const LensSystem& system);
RelativeIlluminationResult relative_illumination(
    const LensSystem& system,
    const RelativeIlluminationConfig& config = {});
AutomaticVignettingResult set_automatic_vignetting(
    LensSystem& system,
    const AutomaticVignettingConfig& config = {});
double estimate_effective_focal_length(const LensSystem& system);
double back_focal_length(const LensSystem& system);
double min_edge_thickness(const LensSystem& system);
void update_clear_apertures_from_trace(
    LensSystem& system,
    int rings = 4,
    const std::string& distribution = "hexapolar",
    unsigned int seed = 1,
    RayAimingMode mode = RayAimingMode::Robust,
    double margin_factor = 1.02,
    bool preserve_explicit_stop = true,
    int arms = 6,
    bool use_current_stop_surface_radius = false,
    bool require_full_trace = false,
    double vignetting_boundary_support_tolerance = 0.02,
    double vignetting_boundary_support_selectivity_slope = 0.05,
    bool vignetting_global_trim_realizability = false,
    bool vignetting_physical_cut_match_enabled = false,
    bool primary_wavelength_apertures = false);
void sync_stop_aperture_to_aiming_radius(LensSystem& system);
Score evaluate_merit(const LensSystem& system, const MeritConfig& config);

}  // namespace optiland
