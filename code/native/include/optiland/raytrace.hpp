#pragma once

#include "optiland/asphere_domain_contract.hpp"
#include "optiland/core.hpp"

#include <array>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <tuple>
#include <utility>

namespace optiland {
namespace raytrace_detail {
struct CalibrationFallbackEdgeSeed;
struct CalibrationAcceptChiefRay;
struct CalibrationProbeScale;
struct CalibrationTryGridFit;
struct CalibrationSolveSecantEdge;
struct CalibrationAcceptEdgeStopSample;
struct CalibrationCachedSameFieldEdgeSeed;
struct CalibrationTryScaledEdgeAnchor;
struct CalibrationBuildEdgeCalibration;
struct CalibrationSolveSeedSpaceEdge;
struct BudgetChiefAppendSameFieldCachedChiefSeeds;
struct BudgetChiefAppendFieldContinuationSeeds;
struct BudgetChiefSolveBudgetChiefFixedOrSecant;
struct BudgetChiefTryTrialSeedRangeForField;
struct BudgetChiefAppendAngleBridgeSeeds;
struct ThreeStepSolveSeedSpaceEdge;
struct ThreeStepBuildCentralFit;
struct ThreeStepTryScaledEdgeAnchor;
struct ThreeStepSolveEdge;
struct BuildCalibratedInterpolation;
struct CalibrationSweepInterpolationEdges;
} // namespace raytrace_detail


enum class AsphereRootPolicy {
    // Explicit legacy production contract shared by CPU and CUDA: residual-certified Newton
    // in a materialized 20-degree slope domain, with topology seeding for the
    // strong folded-branch class and a no-B2 rescue on true seed failure.
    SlopeDomainTopologySeed,
    // Existing CUDA production path: sample-confirm the Newton root and use
    // the certified first-root search when confirmation is inconclusive.
    ConfirmedWithCertifiedFallback,
    // DE scoring path: accept a residual-certified bounded-Newton root, while
    // retaining the full recovery search when all inexpensive seeds fail.
    FastRecover,
    // Reference path used for checkpoints and final handoff validation.
    CertifiedFirstRoot,
    // Isolated feasibility route for the locked 20-degree, vertex-connected
    // physical-domain contract.  Candidate generation and adaptive interval
    // certification are one solver; it never transfers control to a legacy
    // P0/B2/C0 fallback. This UC10 route is the process and GUI default.
    UnifiedCertifiedExperimental,
};

inline constexpr AsphereRootPolicy kDefaultAsphereRootPolicy =
    AsphereRootPolicy::UnifiedCertifiedExperimental;

void set_asphere_root_policy(AsphereRootPolicy policy);
AsphereRootPolicy asphere_root_policy();

bool ray_aiming_uses_session(RayAimingMode mode);
struct RayAimingDiagnosticsSnapshot {
    bool enabled = false;
    long long calibrated_generate_calls = 0;
    long long calibrated_generate_ns = 0;
    long long calibrated_model_requests = 0;
    long long calibrated_model_cache_hits = 0;
    long long calibrated_model_builds = 0;
    long long calibrated_model_build_ns = 0;
    long long calibrated_model_valid = 0;
    long long calibrated_model_invalid = 0;
    long long calibrated_paraxial_chief_attempts = 0;
    long long calibrated_paraxial_chief_accepted = 0;
    long long calibrated_budget_chief_attempts = 0;
    long long calibrated_budget_chief_seed_valid = 0;
    long long calibrated_budget_chief_accepted = 0;
    long long calibrated_budget_chief_rejected = 0;
    long long calibrated_budget_chief_singular = 0;
    long long calibrated_budget_chief_oob = 0;
    long long calibrated_budget_chief_trace_fail = 0;
    long long calibrated_budget_chief_residual_reject = 0;
    long long calibrated_budget_chief_inside_stop_accepted = 0;
    long long calibrated_budget_chief_stop_oob_reject = 0;
    long long calibrated_budget_chief_seed_extract_fail = 0;
    long long calibrated_budget_chief_ns = 0;
    long long calibrated_budget_chief_trial_rays = 0;
    long long calibrated_budget_chief_correction_calls = 0;
    long long calibrated_budget_chief_correction_iterations = 0;
    long long calibrated_budget_chief_stop_probe_calls = 0;
    long long calibrated_budget_chief_stop_probe_successes = 0;
    long long calibrated_budget_chief_stop_probe_ns = 0;
    long long calibrated_budget_chief_residual_count = 0;
    long long calibrated_budget_chief_residual_sum_scaled = 0;
    long long calibrated_budget_chief_residual_max_scaled = 0;
    long long calibrated_chief_anchor_requests = 0;
    long long calibrated_chief_anchor_cache_hits = 0;
    long long calibrated_chief_anchor_builds = 0;
    long long calibrated_chief_anchor_valid = 0;
    long long calibrated_chief_anchor_invalid = 0;
    long long calibrated_chief_anchor_ns = 0;
    long long calibrated_chief_stop_probe_calls = 0;
    long long calibrated_chief_stop_probe_successes = 0;
    long long calibrated_chief_stop_probe_ns = 0;
    long long calibrated_chief_residual_count = 0;
    long long calibrated_chief_residual_sum_scaled = 0;
    long long calibrated_chief_residual_max_scaled = 0;
    long long calibrated_probe_sample_calls = 0;
    long long calibrated_probe_sample_successes = 0;
    long long calibrated_probe_sample_ns = 0;
    long long calibrated_central_fit_attempts = 0;
    long long calibrated_central_fit_successes = 0;
    long long calibrated_central_fit_ns = 0;
    long long calibrated_grid_fit_attempts = 0;
    long long calibrated_grid_fit_successes = 0;
    long long calibrated_grid_fit_ns = 0;
    long long calibrated_edge_solve_calls = 0;
    long long calibrated_edge_correction_calls = 0;
    long long calibrated_edge_correction_iterations = 0;
    long long calibrated_edge_correction_ns = 0;
    long long calibrated_edge_residual_count = 0;
    long long calibrated_edge_residual_sum_scaled = 0;
    long long calibrated_edge_residual_max_scaled = 0;
    long long calibrated_interpolation_probe_calls = 0;
    long long calibrated_interpolation_probe_successes = 0;
    long long calibrated_interpolation_probe_seed_failures = 0;
    long long calibrated_interpolation_probe_trace_failures = 0;
    long long calibrated_interpolation_residual_over_limit = 0;
    long long calibrated_interpolation_residual_count = 0;
    long long calibrated_interpolation_residual_sum_scaled = 0;
    long long calibrated_interpolation_residual_max_scaled = 0;
    long long calibrated_fallback_calls = 0;
    long long calibrated_fallback_none_calls = 0;
    long long calibrated_fallback_other_calls = 0;
    long long calibrated_fallback_model_invalid_calls = 0;
    long long calibrated_fallback_seed_oob_calls = 0;
    long long calibrated_fallback_ray_invalid_calls = 0;
    long long calibrated_correction_calls = 0;
    long long stop_residual_eval_calls = 0;
    long long stop_residual_eval_successes = 0;
    long long stop_residual_eval_ns = 0;
    long long stop_aim_trace_residual_calls = 0;
    long long stop_aim_trace_residual_successes = 0;
    long long stop_aim_trace_residual_ns = 0;
    long long stop_aim_parallel_jacobian_pairs = 0;
    long long stop_aim_parallel_jacobian_ns = 0;
    long long three_step_generate_calls = 0;
    long long three_step_generate_ns = 0;
    long long three_step_model_requests = 0;
    long long three_step_model_cache_hits = 0;
    long long three_step_model_builds = 0;
    long long three_step_model_build_ns = 0;
    long long three_step_model_valid = 0;
    long long three_step_model_invalid = 0;
    long long three_step_reverse_trial_calls = 0;
    long long three_step_reverse_trial_successes = 0;
    long long three_step_reverse_trial_ns = 0;
    long long three_step_center_from_reverse = 0;
    long long three_step_center_fallback_calls = 0;
    long long three_step_center_residual_count = 0;
    long long three_step_center_residual_sum_scaled = 0;
    long long three_step_center_residual_max_scaled = 0;
    long long three_step_edge_solve_calls = 0;
    long long three_step_edge_initial_ray_calls = 0;
    long long three_step_edge_correction_calls = 0;
    long long three_step_edge_correction_iterations = 0;
    long long three_step_edge_correction_ns = 0;
    long long three_step_edge_solve_ns = 0;
    long long three_step_edge_residual_count = 0;
    long long three_step_edge_residual_sum_scaled = 0;
    long long three_step_edge_residual_max_scaled = 0;
    long long three_step_model_stop_probe_calls = 0;
    long long three_step_model_stop_probe_successes = 0;
    long long three_step_model_stop_probe_ns = 0;
    long long three_step_generate_stop_probe_calls = 0;
    long long three_step_generate_stop_probe_successes = 0;
    long long three_step_generate_stop_probe_ns = 0;
    long long three_step_generate_correction_calls = 0;
    long long three_step_generate_correction_iterations = 0;
    long long three_step_generate_correction_ns = 0;
    long long three_step_fallback_calls = 0;
    long long three_step_fallback_none_calls = 0;
};
void set_ray_aiming_diagnostics_enabled(bool enabled);
bool ray_aiming_diagnostics_enabled();
void reset_ray_aiming_diagnostics();
RayAimingDiagnosticsSnapshot ray_aiming_diagnostics_snapshot();
void set_stop_aim_parallel_jacobian_enabled(bool enabled);
bool stop_aim_parallel_jacobian_enabled();
double surface_sag(const Surface& surface, double x, double y);
double profile_surface_sag(const Surface& surface, double y, double profile_radius);
Vec3 surface_normal(const Surface& surface, const Vec3& local_point);
bool intersect_surface(
    const Surface& surface,
    const Ray& local_ray,
    double* t,
    bool ignore_stop_aperture = false);
bool refract(Vec3 incident, Vec3 normal, double n1, double n2, Vec3* transmitted);
Vec3 reflect(Vec3 incident, Vec3 normal);

class ParallelStopAimTraceWorker;

class SequentialRayTracer {
public:
    explicit SequentialRayTracer(const LensSystem& system) : system_(system) {}

    Ray generate_ray(const Field& field, const PupilPoint& pupil, double wavelength_um) const;
    Ray generate_ray_with_mode(
        const Field& field,
        const PupilPoint& pupil,
        double wavelength_um,
        RayAimingMode mode) const;
    RayTraceResult trace_field(
        const Field& field,
        double wavelength_um,
        int rings,
        bool keep_history = true) const;
    RayTraceResult trace_field(
        const Field& field,
        double wavelength_um,
        const std::vector<PupilPoint>& pupils,
        bool keep_history = true) const;
    // Automatic-stop aperture construction must trace its replacement rays
    // without clipping them at the previous automatic stop radius. Explicit
    // stops and preceding fixed apertures retain their physical clipping.
    bool trace_ray(
        Ray& ray,
        std::vector<TracePoint>* history = nullptr,
        bool ignore_automatic_stop_aperture = false) const;
    void trace_rays(std::vector<Ray>& rays) const;
    void trace_rays_with_selected_histories(
        std::vector<Ray>& rays,
        const std::vector<unsigned char>& history_mask,
        std::vector<std::vector<TracePoint>>* histories) const;
    bool trace_to_surface(
        Ray ray,
        std::size_t surface_index,
        TracePoint* point,
        bool ignore_target_aperture = false,
        bool ignore_intermediate_apertures = false) const;
    // Trace one mathematical ray prefix with every physical aperture clip
    // disabled and retain the hit at each surface.  This is intentionally
    // narrower than trace_ray(history): automatic vignetting needs the
    // unclipped hard-aperture envelope, not a normally clipped diagnostic ray.
    // Reusing one prefix preserves trace_to_surface(..., true, true) geometry
    // while avoiding a complete restart for every hard-aperture surface.
    bool trace_unclipped_to_surface_history(
        Ray ray,
        std::size_t surface_index,
        std::vector<TracePoint>* history) const;
    std::size_t stop_surface_index() const;
    bool first_surface_point(const Ray& ray, Vec3* point) const;
    Ray generate_ray_through_first_surface_point(
        const Field& field,
        double wavelength_um,
        double x,
        double y,
        double nominal_weight = 1.0) const;
    Ray solve_stop_aim_from_initial(
        const Field& field,
        const PupilPoint& pupil,
        double wavelength_um,
        Ray initial,
        bool strict_failure = false,
        int max_iterations_override = -1,
        bool ignore_intermediate_apertures = false,
        bool honor_first_surface_seed = false,
        double first_surface_seed_x = 0.0,
        double first_surface_seed_y = 0.0) const;
    double entrance_pupil_radius(double wavelength_um) const;
    double entrance_pupil_location(double wavelength_um) const;
    double exit_pupil_location(double wavelength_um) const;
    double aperture_stop_radius(double wavelength_um) const;
    double stop_target_radius(double wavelength_um, RayAimingMode mode) const;

    const LensSystem& system() const { return system_; }

private:
    Ray generate_direct_ray(const Field& field, const PupilPoint& pupil, double wavelength_um) const;
    Ray generate_paraxial_ray(const Field& field, const PupilPoint& pupil, double wavelength_um) const;
    Ray generate_ray_through_surface_point(
        const Field& field,
        double wavelength_um,
        std::size_t surface_index,
        double x,
        double y,
        double nominal_weight) const;
    Ray aim_iterative(
        const Field& field,
        const PupilPoint& pupil,
        double wavelength_um,
        bool robust) const;
    double surface_vertex_z(std::size_t surface_index) const;
    double paraxial_stop_radius(double wavelength_um) const;
    double real_stop_radius(double wavelength_um) const;
    double effective_aperture_clip_radius(std::size_t surface_index, double wavelength_um) const;
    std::size_t stop_index() const;
    bool prepared_trace_supported() const;
    bool trace_ray_prepared_no_history(Ray& ray) const;
    void trace_rays_prepared_no_history(
        std::vector<Ray>& rays,
        const std::vector<unsigned char>* history_mask = nullptr,
        std::vector<std::vector<TracePoint>>* histories = nullptr) const;
    bool trace_to_surface_prepared(
        Ray ray,
        std::size_t surface_index,
        TracePoint* point,
        bool ignore_target_aperture,
        bool ignore_intermediate_apertures) const;
    void trace_stop_aim_jacobian_pair(
        Ray ray_x,
        Ray ray_y,
        std::size_t surface_index,
        bool ignore_intermediate_apertures,
        bool count_stop_aim_trace,
        TracePoint* point_x,
        TracePoint* point_y,
        bool* ok_x,
        bool* ok_y) const;
    struct PreparedTraceWaveCache {
        double wavelength_um = 0.0;
        std::vector<double> vertex_z;
        std::vector<double> n_before;
        std::vector<double> n_after;
        std::vector<double> aperture_clip_radius;
        std::vector<double> aperture_clip_limit_sq;
        std::vector<double> radius;
        std::vector<double> inv_radius;
        std::vector<double> backward_z_tol;
        std::vector<double> backward_reference;
        std::vector<unsigned char> geometry;
        std::vector<unsigned char> refract_surface;
    };
    const PreparedTraceWaveCache& prepared_trace_cache(double wavelength_um) const;

    const LensSystem& system_;
    mutable bool stop_index_cached_ = false;
    mutable std::size_t cached_stop_index_ = 0;
    mutable bool vertex_z_cache_valid_ = false;
    mutable std::vector<double> vertex_z_cache_;
    mutable std::vector<std::pair<double, double>> entrance_pupil_radius_cache_;
    mutable std::vector<std::pair<double, double>> entrance_pupil_location_cache_;
    mutable std::vector<std::pair<double, double>> exit_pupil_location_cache_;
    mutable std::vector<std::pair<double, double>> paraxial_stop_radius_cache_;
    mutable std::vector<std::pair<double, double>> real_stop_radius_cache_;
    mutable bool prepared_trace_support_cached_ = false;
    mutable bool prepared_trace_supported_ = false;
    // Real-stop sizing can recursively trace the primary wavelength while a
    // non-primary prepared trace is active.  Appending that primary cache must
    // not invalidate the active cache reference (or the pointers held by the
    // batched prepared path), so this storage must provide stable references.
    mutable std::deque<PreparedTraceWaveCache> prepared_trace_wave_caches_;
    // Created only by Set Vignetting/APG when semantic-preserving Jacobian
    // pair parallelism is enabled.  The worker owns an independent tracer, so
    // no mutable trace cache or AimingSession state is shared across threads.
    mutable std::shared_ptr<ParallelStopAimTraceWorker>
        parallel_stop_aim_trace_worker_;
    // A Win32 event round trip costs more than two very short ray prefixes.
    // Probe a small serial sample per tracer and enable the second worker only
    // when the measured prefix cost is above the observed crossover.
    mutable int stop_aim_parallel_jacobian_decision_ = 0;
    mutable std::size_t stop_aim_parallel_jacobian_probe_pairs_ = 0;
    mutable long long stop_aim_parallel_jacobian_probe_trace_ns_ = 0;
    mutable long long stop_aim_parallel_jacobian_probe_wall_ns_ = 0;
    mutable std::size_t stop_aim_parallel_jacobian_decision_pairs_ = 0;
};

// CPU adaptation of Optiland af111f1655fd5f2e253201d59f431ad1757318c4.
// Counts include calibration work; polish_iterations counts requested rays only.
struct RobustAimingDiagnostics {
    std::size_t requests = 0, cache_hits = 0, calibrations = 0, invalidations = 0;
    std::size_t solves = 0, stop_traces = 0, march_attempts = 0, scan_candidates = 0;
    std::size_t edge_fallbacks = 0, jacobian_refreshes = 0, jacobian_fallbacks = 0;
    std::size_t converged = 0, polish_iterations = 0;
    double calibration_ms = 0.0, chief_ms = 0.0, edge_ms = 0.0, polish_ms = 0.0;
    std::string chief_strategy;
};
class RobustAimingState;

class AimingSession {
public:
    // Runtime environment switches are captured on the first CP model build.
    // Create a new session after changing them; cached models keep one policy.
    explicit AimingSession(const LensSystem& system);
    // Explicit dispatch also supports callers requesting Robust on a lens whose
    // configured mode differs. Failed exact solves are always marked invalid.
    Ray generate_robust(const Field& field, const Wavelength& wave, const PupilPoint& pupil);
    RobustAimingDiagnostics robust_diagnostics() const;
    // Batch clients already trace generated rays. Reuse that physical result
    // for APG branch validation instead of tracing every healthy ray twice.
    Ray generate_for_trace(
        std::size_t field_index, std::size_t wave_index, std::size_t pupil_index,
        const Field& field, const Wavelength& wave, const PupilPoint& pupil);
    bool finish_trace(
        std::size_t field_index, std::size_t wave_index, std::size_t pupil_index,
        const Field& field, const Wavelength& wave, const PupilPoint& pupil,
        Ray& initial, Ray& traced);
    // Predict a replacement image guard from existing CP anchors. Does not
    // trace either the original guard or a boundary-validation ray.
    PupilPoint predict_secondary_image_guard(
        std::size_t field_index, std::size_t wave_index,
        const Field& field, const Wavelength& wave, const PupilPoint& pupil);
    Ray generate_image_guard(
        std::size_t field_index, std::size_t wave_index, std::size_t pupil_index,
        const Field& field, const Wavelength& wave, const PupilPoint& pupil);

    struct ChiefAimingProbeStep {
        std::string strategy = "abcd";
        double inward_factor = 0.0;
        double center_meridional_radius = 0.0;
        int iterations = 0;
        bool trace_to_stop = false;
        bool aim_converged = false;
        bool first_surface_seed_valid = false;
        double first_surface_x = 0.0;
        double first_surface_y = 0.0;
        double stop_x = 0.0;
        double stop_y = 0.0;
        double stop_radial = std::numeric_limits<double>::infinity();
        double stop_residual = std::numeric_limits<double>::infinity();
        double aim_residual = std::numeric_limits<double>::infinity();
        int last_valid_surface = -1;
        RayFailureCode failure_code = RayFailureCode::None;
        int failure_surface = -1;
        double failure_severity = 0.0;
        RayFailureCode trace_failure_code = RayFailureCode::None;
        int trace_failure_surface = -1;
        double trace_failure_severity = 0.0;
    };

    struct ChiefAimingProbe {
        std::size_t field_index = 0;
        std::size_t wave_index = 0;
        double hx = 0.0;
        double hy = 0.0;
        double wavelength_um = 0.0;
        double target_stop_x = 0.0;
        double target_stop_y = 0.0;
        double stop_target_radius = 0.0;
        double physical_stop_radius = 0.0;
        bool abcd_singular = false;
        bool abcd_seed_valid = false;
        bool abcd_seed_oob = false;
        double abcd_seed_x = 0.0;
        double abcd_seed_y = 0.0;
        bool full_anchor_valid = false;
        double full_seed_x = 0.0;
        double full_seed_y = 0.0;
        double full_stop_x = 0.0;
        double full_stop_y = 0.0;
        double full_stop_radial = std::numeric_limits<double>::infinity();
        double full_stop_residual = std::numeric_limits<double>::infinity();
        std::vector<ChiefAimingProbeStep> budget_steps;
    };

    std::vector<ChiefAimingProbe> probe_chief_aiming(int max_budget_iterations = 3);

    struct ThreeStepChiefProbe {
        std::size_t field_index = 0;
        std::size_t wave_index = 0;
        double hx = 0.0;
        double hy = 0.0;
        double wavelength_um = 0.0;
        double field_radius = 0.0;
        double target_angle_deg = 0.0;
        int trial_rays = 0;
        double max_reverse_angle_deg = 0.0;
        int reverse_sample_count = 0;
        double min_sample_angle_deg = std::numeric_limits<double>::quiet_NaN();
        double max_sample_angle_deg = std::numeric_limits<double>::quiet_NaN();
        bool bracketed = false;
        bool clamped_low = false;
        bool clamped_high = false;
        double lo_angle_deg = std::numeric_limits<double>::quiet_NaN();
        double hi_angle_deg = std::numeric_limits<double>::quiet_NaN();
        double interpolation_t = std::numeric_limits<double>::quiet_NaN();
        bool predicted_valid = false;
        double predicted_x = 0.0;
        double predicted_y = 0.0;
        bool predicted_trace_to_stop = false;
        double predicted_stop_residual = std::numeric_limits<double>::infinity();
        double predicted_stop_direction_angle_deg = std::numeric_limits<double>::quiet_NaN();
        RayFailureCode predicted_failure_code = RayFailureCode::None;
        int predicted_failure_surface = -1;
        double predicted_failure_severity = 0.0;
        bool actual_valid = false;
        double actual_x = 0.0;
        double actual_y = 0.0;
        double actual_stop_residual = std::numeric_limits<double>::infinity();
        double actual_stop_direction_angle_deg = std::numeric_limits<double>::quiet_NaN();
        bool reverse_actual_stop_angle_valid = false;
        double reverse_actual_stop_angle_object_angle_deg =
            std::numeric_limits<double>::quiet_NaN();
        double reverse_actual_stop_angle_x = 0.0;
        double reverse_actual_stop_angle_y = 0.0;
        double reverse_actual_stop_angle_delta_r = std::numeric_limits<double>::infinity();
        double reverse_floor_stop_angle_deg = std::numeric_limits<double>::quiet_NaN();
        double reverse_floor_object_angle_deg = std::numeric_limits<double>::quiet_NaN();
        double reverse_ceil_stop_angle_deg = std::numeric_limits<double>::quiet_NaN();
        double reverse_ceil_object_angle_deg = std::numeric_limits<double>::quiet_NaN();
        double reverse_minus_0p1_object_angle_deg =
            std::numeric_limits<double>::quiet_NaN();
        double reverse_plus_0p1_object_angle_deg =
            std::numeric_limits<double>::quiet_NaN();
        bool model_valid = false;
        double model_fit_residual = std::numeric_limits<double>::infinity();
        double meridional_x = 0.0;
        double meridional_y = 0.0;
        double sagittal_x = 0.0;
        double sagittal_y = 0.0;
        double meridional_radius = 0.0;
        double sagittal_radius = 0.0;
        double delta_x = 0.0;
        double delta_y = 0.0;
        double delta_r = 0.0;
    };

    std::vector<ThreeStepChiefProbe> probe_three_step_chief_aiming();

    struct CalibratedEdgeInitialProbe {
        std::size_t field_index = 0;
        std::size_t wave_index = 0;
        std::size_t edge_index = 0;
        double hx = 0.0;
        double hy = 0.0;
        double wavelength_um = 0.0;
        double px = 0.0;
        double py = 0.0;
        bool is_center_field = false;
        double stop_target_radius = 0.0;
        double target_stop_x = 0.0;
        double target_stop_y = 0.0;
        double entrance_pupil_radius = 0.0;
        double near_pupil_edge_radius = 0.0;
        bool model_valid = false;
        bool center_footprint_valid = false;
        bool chief_seed_valid = false;
        double chief_x = 0.0;
        double chief_y = 0.0;
        bool truth_valid = false;
        double truth_x = 0.0;
        double truth_y = 0.0;
        double truth_stop_residual = std::numeric_limits<double>::infinity();
        bool calibrated_anchor_valid = false;
        double calibrated_anchor_x = 0.0;
        double calibrated_anchor_y = 0.0;
        double calibrated_anchor_to_truth_dx = 0.0;
        double calibrated_anchor_to_truth_dy = 0.0;
        double calibrated_anchor_to_truth_dr = std::numeric_limits<double>::infinity();
        bool near_seed_valid = false;
        double near_seed_x = 0.0;
        double near_seed_y = 0.0;
        double near_seed_to_truth_dx = 0.0;
        double near_seed_to_truth_dy = 0.0;
        double near_seed_to_truth_dr = std::numeric_limits<double>::infinity();
        double near_seed_stop_residual = std::numeric_limits<double>::infinity();
        bool center_seed_valid = false;
        double center_vector_x = 0.0;
        double center_vector_y = 0.0;
        double center_vector_radius = 0.0;
        double center_seed_x = 0.0;
        double center_seed_y = 0.0;
        double center_seed_to_truth_dx = 0.0;
        double center_seed_to_truth_dy = 0.0;
        double center_seed_to_truth_dr = std::numeric_limits<double>::infinity();
        double center_seed_stop_residual = std::numeric_limits<double>::infinity();
    };

    std::vector<CalibratedEdgeInitialProbe> probe_calibrated_edge_initials();

    struct CalibratedEdgeIterativeProbeStep {
        std::size_t field_index = 0;
        std::size_t wave_index = 0;
        std::size_t edge_index = 0;
        int iteration = 0;
        double hx = 0.0;
        double hy = 0.0;
        double wavelength_um = 0.0;
        double px = 0.0;
        double py = 0.0;
        double target_stop_x = 0.0;
        double target_stop_y = 0.0;
        double seed_x = 0.0;
        double seed_y = 0.0;
        bool guess_valid = false;
        bool trace_ok = false;
        bool stop_valid = false;
        double stop_x = 0.0;
        double stop_y = 0.0;
        double residual = std::numeric_limits<double>::infinity();
        bool accepted = false;
        bool terminated = false;
        RayFailureCode failure_code = RayFailureCode::None;
        int failure_surface = -1;
        double failure_severity = 0.0;
        const char* trace_failure_reason = "";
        int trace_failure_surface = -1;
        const char* reason = "";
    };

    std::vector<CalibratedEdgeIterativeProbeStep> probe_center_edge_iterative_steps();

    Ray generate(
        std::size_t field_index,
        std::size_t wave_index,
        std::size_t pupil_index,
        const Field& field,
        const Wavelength& wave,
        const PupilPoint& pupil);

    // Configured (vignetted) pupil center. Reuse the sampled-field calibrated
    // model's chief anchor; missing edge anchors do not invalidate the chief.
    // This never clears FVC/FVD or requests a STOP-center principal ray.
    Ray generate_chief(
        std::size_t field_index,
        std::size_t wave_index,
        const Field& field,
        const Wavelength& wave);

    // Source compatibility only: this old name has always consumed the field
    // supplied by its caller. It is not an unvignetted-principal-ray API.
    Ray generate_physical_chief(
        std::size_t fi, std::size_t wi, const Field& field, const Wavelength& wave) {
        return generate_chief(fi, wi, field, wave);
    }

private:
    struct FirstSurfaceSeed {
        double x = 0.0;
        double y = 0.0;
        bool valid = false;
    };
    struct ChiefAnchor {
        FirstSurfaceSeed seed;
        Ray ray;
        bool valid = false;
        bool stop_valid = false;
        double stop_x = 0.0;
        double stop_y = 0.0;
        double stop_residual = std::numeric_limits<double>::infinity();
    };
    struct ChiefBridgeAnchor {
        std::size_t wave_index = 0;
        Field field;
        double rho = 0.0;
        ChiefAnchor anchor;
    };
    struct PilotSample {
        FirstSurfaceSeed seed;
        double stop_x = 0.0;
        double stop_y = 0.0;
        bool valid = false;
    };
    struct PilotCache {
        bool built = false;
        FirstSurfaceSeed chief_seed;
        std::vector<PilotSample> samples;
        bool has_jacobian = false;
        double j11 = 0.0;
        double j12 = 0.0;
        double j21 = 0.0;
        double j22 = 0.0;
    };
    struct CalibratedParaxialModel {
        struct EdgeAnchor {
            double px = 0.0;
            double py = 0.0;
            FirstSurfaceSeed seed;
            // Retain the stop hit from the existing calibration trace.
            double stop_x = std::numeric_limits<double>::quiet_NaN();
            double stop_y = std::numeric_limits<double>::quiet_NaN();
            double correction_x = 0.0;
            double correction_y = 0.0;
            bool valid = false;
        };
        bool built = false;
        bool valid = false;
        FirstSurfaceSeed chief_seed;
        double chief_stop_x = 0.0;
        double chief_stop_y = 0.0;
        double b11 = 1.0;
        double b12 = 0.0;
        double b21 = 0.0;
        double b22 = 1.0;
        double fit_rms = std::numeric_limits<double>::infinity();
        double condition_number = std::numeric_limits<double>::infinity();
        std::array<EdgeAnchor, 4> edge_anchors{};
    };
    struct CalibratedCenterBeamFootprint {
        bool built = false;
        bool valid = false;
        double wavelength_um = std::numeric_limits<double>::quiet_NaN();
        FirstSurfaceSeed chief_seed;
        std::array<FirstSurfaceSeed, 4> edge_vectors{};
    };
    struct ThreeStepModel {
        struct EdgeCorrection {
            double correction_x = 0.0;
            double correction_y = 0.0;
            bool valid = false;
        };
        bool built = false;
        bool valid = false;
        FirstSurfaceSeed center;
        FirstSurfaceSeed meridional_plus;
        FirstSurfaceSeed meridional_minus;
        FirstSurfaceSeed sagittal_plus;
        FirstSurfaceSeed sagittal_minus;
        double meridional_x = 0.0;
        double meridional_y = 0.0;
        double sagittal_x = 0.0;
        double sagittal_y = 0.0;
        double center_stop_x = 0.0;
        double center_stop_y = 0.0;
        double b11 = 1.0;
        double b12 = 0.0;
        double b21 = 0.0;
        double b22 = 1.0;
        double central_fit_rms = std::numeric_limits<double>::infinity();
        double condition_number = std::numeric_limits<double>::infinity();
        bool central_fit_valid = false;
        std::array<EdgeCorrection, 4> edge_corrections{};
        double fit_residual = std::numeric_limits<double>::infinity();
    };
    struct ThreeStepReverseMap {
        bool built = false;
        bool valid = false;
        double wavelength_um = std::numeric_limits<double>::quiet_NaN();
        std::vector<std::pair<double, double>> samples;
    };

    using FieldWaveKey = std::pair<std::size_t, std::size_t>;
    using FieldWavePupilKey = std::tuple<std::size_t, std::size_t, std::size_t>;

    struct TransmittedApgSeed {
        PupilPoint pupil;
        FirstSurfaceSeed seed;
    };
    std::map<FieldWaveKey, std::vector<TransmittedApgSeed>> transmitted_apg_seeds_;

    struct CalibratedRuntimeOptions {
        bool initialized = false;
        int continuation_mode = 1961;
        bool chief_debug_enabled = false;
        bool chief_debug_all = false;
        int chief_debug_field = 0;
        bool model_dump = false;
    };
    CalibratedRuntimeOptions calibrated_runtime_options_;
    const CalibratedRuntimeOptions& calibrated_runtime_options();

    const LensSystem& system_;
    SequentialRayTracer tracer_;
    std::shared_ptr<RobustAimingState> robust_state_;
    std::map<FieldWaveKey, ChiefAnchor> chief_anchors_;
    std::vector<ChiefBridgeAnchor> chief_bridge_anchors_;
    std::map<FieldWaveKey, PilotCache> pilot_caches_;
    std::map<FieldWaveKey, CalibratedParaxialModel> calibrated_models_;
    std::map<std::size_t, CalibratedCenterBeamFootprint> calibrated_center_beam_footprints_;
    std::map<double, double> center_meridional_radius_cache_;
    std::map<double, double> center_iterative_edge_radius_cache_;
    std::map<FieldWaveKey, ThreeStepModel> three_step_models_;
    std::map<std::size_t, ThreeStepReverseMap> three_step_reverse_maps_;
    std::map<FieldWavePupilKey, FirstSurfaceSeed> solved_seeds_;
    std::map<FieldWaveKey, int> iterative_failures_;
    std::map<std::size_t, Field> fields_seen_;

    Ray make_failed_ray(
        const Wavelength& wave,
        const PupilPoint& pupil,
        double residual = std::numeric_limits<double>::infinity()) const;
    bool is_strictly_aimed(const Ray& ray, const Field& field) const;
    bool remember_solution(
        std::size_t field_index,
        std::size_t wave_index,
        std::size_t pupil_index,
        const Ray& ray);
    ChiefAnchor ensure_chief_anchor(
        std::size_t field_index,
        std::size_t wave_index,
        const Field& field,
        const Wavelength& wave);
    FirstSurfaceSeed estimate_abcd_chief_seed(
        const Field& field,
        const Wavelength& wave,
        bool* singular = nullptr) const;
    ChiefAnchor try_abcd_budget_chief_anchor(
        std::size_t field_index,
        std::size_t wave_index,
        const Field& field,
        const Wavelength& wave);
    PilotCache& ensure_pilot_cache(
        std::size_t field_index,
        std::size_t wave_index,
        const Field& field,
        const Wavelength& wave,
        const ChiefAnchor& anchor);
    CalibratedCenterBeamFootprint& ensure_calibrated_center_beam_footprint(
        std::size_t wave_index,
        const Wavelength& wave);
    CalibratedParaxialModel& ensure_calibrated_paraxial_model(
        std::size_t field_index,
        std::size_t wave_index,
        const Field& sampled_field,
        const Wavelength& wave);
    ThreeStepReverseMap& ensure_three_step_reverse_map(
        std::size_t wave_index,
        const Wavelength& wave);
    ThreeStepModel& ensure_three_step_model(
        std::size_t field_index,
        std::size_t wave_index,
        const Field& field,
        const Wavelength& wave);
    Ray generate_calibrated_paraxial(
        std::size_t field_index,
        std::size_t wave_index,
        std::size_t pupil_index,
        const Field& field,
        const Wavelength& wave,
        const PupilPoint& pupil);
    Ray generate_three_step(
        std::size_t field_index,
        std::size_t wave_index,
        std::size_t pupil_index,
        const Field& field,
        const Wavelength& wave,
        const PupilPoint& pupil);
    Ray generate_adaptive_pilot(
        std::size_t field_index,
        std::size_t wave_index,
        std::size_t pupil_index,
        const Field& field,
        const Wavelength& wave,
        const PupilPoint& pupil);
    Ray generate_adaptive_pilot_candidate(
        std::size_t field_index, std::size_t wave_index, std::size_t pupil_index,
        const Field& field, const Wavelength& wave, const PupilPoint& pupil);
    Ray recover_adaptive_pilot_trace(
        std::size_t field_index, std::size_t wave_index, std::size_t pupil_index,
        const Field& field, const Wavelength& wave, const PupilPoint& pupil,
        const Ray& original, Ray& traced, bool& recovered);
    FirstSurfaceSeed seed_from_ray(const Ray& ray) const;
    bool sample_stop_from_seed(
        const Field& field,
        const Wavelength& wave,
        const FirstSurfaceSeed& seed,
        double* stop_x,
        double* stop_y,
        Ray* traced_ray = nullptr,
        bool ignore_target_aperture = false,
        bool ignore_intermediate_apertures = false) const;
    double center_meridional_radius(const Wavelength& wave);
    double center_iterative_edge_radius(const Wavelength& wave);
    bool sample_stop_from_ray(
        Ray ray,
        double* stop_x,
        double* stop_y,
        bool ignore_target_aperture = false,
        bool ignore_intermediate_apertures = false) const;
    FirstSurfaceSeed shifted_chief_seed_by_transverse_radius(
        const Field& field,
        const Wavelength& wave,
        const FirstSurfaceSeed& chief_seed,
        const PupilPoint& edge,
        double beam_radius) const;
    Ray solve_from_seed(
        const Field& field,
        const Wavelength& wave,
        const PupilPoint& pupil,
        const FirstSurfaceSeed& seed) const;
    void append_seed(std::vector<FirstSurfaceSeed>& seeds, FirstSurfaceSeed seed, double wavelength_um) const;
    void append_seed_with_bounds(
        std::vector<FirstSurfaceSeed>& seeds, FirstSurfaceSeed seed,
        double hard_radius, double search_radius) const;
    void append_reuse_seeds(
        std::vector<FirstSurfaceSeed>& seeds,
        std::size_t field_index,
        std::size_t wave_index,
        std::size_t pupil_index) const;
    void append_chief_jacobian_seed(
        std::vector<FirstSurfaceSeed>& seeds,
        const PilotCache& cache,
        const Field& field,
        const PupilPoint& pupil,
        double wavelength_um) const;
    void append_pilot_nearest_seeds(
        std::vector<FirstSurfaceSeed>& seeds,
        const PilotCache& cache,
        const Field& field,
        const PupilPoint& pupil,
        double wavelength_um) const;
    double first_surface_hard_radius(double wavelength_um = 0.0) const;
    double first_surface_search_radius(double wavelength_um = 0.0) const;

    // Responsibility-specific build stages have explicit access to this session.
    friend struct raytrace_detail::CalibrationFallbackEdgeSeed;
    friend struct raytrace_detail::CalibrationAcceptChiefRay;
    friend struct raytrace_detail::CalibrationProbeScale;
    friend struct raytrace_detail::CalibrationTryGridFit;
    friend struct raytrace_detail::CalibrationSolveSecantEdge;
    friend struct raytrace_detail::CalibrationAcceptEdgeStopSample;
    friend struct raytrace_detail::CalibrationCachedSameFieldEdgeSeed;
    friend struct raytrace_detail::CalibrationTryScaledEdgeAnchor;
    friend struct raytrace_detail::CalibrationBuildEdgeCalibration;
    friend struct raytrace_detail::CalibrationSolveSeedSpaceEdge;
    friend struct raytrace_detail::BudgetChiefAppendSameFieldCachedChiefSeeds;
    friend struct raytrace_detail::BudgetChiefAppendFieldContinuationSeeds;
    friend struct raytrace_detail::BudgetChiefSolveBudgetChiefFixedOrSecant;
    friend struct raytrace_detail::BudgetChiefTryTrialSeedRangeForField;
    friend struct raytrace_detail::BudgetChiefAppendAngleBridgeSeeds;
    friend struct raytrace_detail::ThreeStepSolveSeedSpaceEdge;
    friend struct raytrace_detail::ThreeStepBuildCentralFit;
    friend struct raytrace_detail::ThreeStepTryScaledEdgeAnchor;
    friend struct raytrace_detail::ThreeStepSolveEdge;
    friend struct raytrace_detail::BuildCalibratedInterpolation;
    friend struct raytrace_detail::CalibrationSweepInterpolationEdges;
};

double paraxial_effective_focal_length(const LensSystem& system, double wavelength_um);

}  // namespace optiland
