#pragma once
#include "optiland/analysis.hpp"
#include "optiland/fixed_budget_sampling.hpp"
#include "optiland/pupil_distinct.hpp"

#include "optiland/raytrace.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <queue>
#include <string>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <thread>
#endif


namespace optiland {
struct FiniteRayFieldMetric {
    double field_radius = std::numeric_limits<double>::quiet_NaN();
    double chief_t = std::numeric_limits<double>::quiet_NaN();
    double chief_s = std::numeric_limits<double>::quiet_NaN();
    double best_focus_shift_mm = std::numeric_limits<double>::quiet_NaN();
    double tangential_focus_shift_mm = std::numeric_limits<double>::quiet_NaN();
    double sagittal_focus_shift_mm = std::numeric_limits<double>::quiet_NaN();
    double astig_focus_gap_mm = std::numeric_limits<double>::quiet_NaN();
    double spherical_focus_spread_mm = std::numeric_limits<double>::quiet_NaN();
    double tangential_coma_mm = std::numeric_limits<double>::quiet_NaN();
    bool valid = false;
};

struct FiniteRayAberrationMetrics {
    double field_focus_spread_mm = std::numeric_limits<double>::quiet_NaN();
    double astig_focus_gap_mm = std::numeric_limits<double>::quiet_NaN();
    double axial_color_focus_shift_mm = std::numeric_limits<double>::quiet_NaN();
    double lateral_color_mm = std::numeric_limits<double>::quiet_NaN();
    double spherical_focus_spread_mm = std::numeric_limits<double>::quiet_NaN();
    double coma_tangential_mm = std::numeric_limits<double>::quiet_NaN();
    double valid_fraction = std::numeric_limits<double>::quiet_NaN();
    double metric_count = 0.0;
};

namespace analysis_detail {
enum class MeritPupilMetric {
    Spot,
    Wavefront,
    Neutral,
};

enum class PupilSymmetry {
    None,
    MirrorX,
    MirrorY,
    SignXY,
    Dihedral4,
};

struct PupilTransform {
    double xx = 1.0;
    double xy = 0.0;
    double yx = 0.0;
    double yy = 1.0;
};

struct SymmetricTraceBatch {
    std::vector<Ray> initial_rays;
    std::vector<Ray> rays;
    std::vector<std::vector<TracePoint>> histories;
    std::size_t physically_traced_rays = 0;
};

struct AxisCrossResult {
    int count = 0;
    double first_z = std::numeric_limits<double>::quiet_NaN();
    bool has_cross = false;
};

struct StructuralRaySample {
    PupilPoint pupil;
    std::vector<TracePoint> history;
    bool usable = false;
};

struct ObjectSpaceReferencePlane {
    bool enabled = false;
    Vec3 point;
    Vec3 normal = {0.0, 0.0, 1.0};
    double refractive_index = 1.0;
};

struct WavefrontStatsAccumulator {
    WavefrontStats stats;
    double sum_weight = 0.0;
    double sum_opd = 0.0;
    double sum_opd2 = 0.0;
    double min_opd = std::numeric_limits<double>::infinity();
    double max_opd = -std::numeric_limits<double>::infinity();
    double max_abs_opd = 0.0;
};

struct WavefrontReferenceSphere {
    bool valid = false;
    Vec3 center;
    double radius = std::numeric_limits<double>::quiet_NaN();
};

struct CentroidDircosWavefrontSample {
    std::size_t wave_index = 0;
    double opd_waves = 0.0;
    double dir_x = 0.0;
    double dir_y = 0.0;
    double weight = 0.0;
    bool aperture_blocked = false;
};

struct CentroidDircosWavefrontFit {
    std::vector<double> coefficients;
    std::vector<int> piston_columns;
    int dir_x_column = -1;
    int dir_y_column = -1;
};

class AutomaticVignettingFieldSolver {
public:
    struct Sample {
        bool aimed = false;
        bool feasible = false;
        double max_boundary_ratio = 0.0;
        int limiting_surface = -1;
        Ray ray;
    };

    struct Interval {
        bool valid = false;
        double lower = 0.0;
        double upper = 0.0;
        int lower_surface = -1;
        int upper_surface = -1;
        std::string message;
    };

    AutomaticVignettingFieldSolver(
        const LensSystem& system,
        std::size_t field_index,
        std::size_t wave_index,
        const AutomaticVignettingConfig& config,
        AimingSession* shared_aiming = nullptr,
        std::vector<std::pair<double, double>> preferred_seeds = {});

    AutomaticVignettingFieldSolver(
        const LensSystem& system,
        std::size_t field_index,
        const std::vector<std::size_t>& wave_indices,
        const AutomaticVignettingConfig& config,
        AimingSession* shared_aiming = nullptr,
        std::vector<std::pair<double, double>> preferred_seeds = {});

    Sample evaluate(double pupil_x, double pupil_y);

    struct BranchSeed {
        double x = 0.0;
        double y = 0.0;
        Ray ray;
    };

    Sample continue_branch(const BranchSeed& seed, double x, double y);

    Sample recover_branch(double x, double y);

    bool enable_branch_recovery() {
        if (config_.aiming_mode != RayAimingMode::AdaptivePilotGrid ||
            branch_seeds_.empty() || branch_recovery_enabled_) return false;
        branch_recovery_enabled_ = true;
        batch_apg_worker_pool_.clear();
        for (auto& solver : additional_wave_solvers_) {
            solver->branch_seeds_ = branch_seeds_;
            solver->branch_recovery_enabled_ = true;
        }
        return true;
    }

    Sample recover_physical_seed(double x, double y);

    bool recover_axis_seed(double x, double y);

    Sample evaluate_from_inside(double x, double y, const BranchSeed& inside);

    // Set Vignetting recovery uses the same aperture/full-trace predicate as
    // ordinary samples. A converged stop target alone is not a usable branch.
    Sample check_branch_ray(const Ray& ray, double pupil_x, double pupil_y);

    Sample evaluate_single(double pupil_x, double pupil_y);

    Interval solve_axis(bool x_axis, double transverse, double preferred);

    bool grid_centroid(double* centroid_x, double* centroid_y);

    const std::string& seed_source() const { return seed_source_; }

    std::size_t connected_sample_count() const {
        return connected_sample_count_;
    }

    std::size_t branch_reseed_attempt_count() const {
        std::size_t total = branch_reseed_attempts_;
        for (const auto& solver : additional_wave_solvers_) {
            total += solver->branch_reseed_attempt_count();
        }
        return total;
    }

    std::size_t branch_reseed_success_count() const {
        std::size_t total = branch_reseed_successes_;
        for (const auto& solver : additional_wave_solvers_) {
            total += solver->branch_reseed_success_count();
        }
        return total;
    }

    std::size_t pupil_evaluation_count() const {
        std::size_t total = pupil_evaluations_;
        for (const auto& solver : additional_wave_solvers_) {
            total += solver->pupil_evaluation_count();
        }
        return total;
    }

    std::size_t envelope_trace_count() const {
        std::size_t total = envelope_trace_calls_;
        for (const auto& solver : additional_wave_solvers_) {
            total += solver->envelope_trace_count();
        }
        return total;
    }

    std::size_t hard_aperture_surface_test_count() const {
        std::size_t total = hard_aperture_surface_tests_;
        for (const auto& solver : additional_wave_solvers_) {
            total += solver->hard_aperture_surface_test_count();
        }
        return total;
    }

    double aiming_seconds() const {
        double total = aiming_seconds_;
        for (const auto& solver : additional_wave_solvers_) {
            total += solver->aiming_seconds();
        }
        return total;
    }

    double envelope_trace_seconds() const {
        double total = envelope_trace_seconds_;
        for (const auto& solver : additional_wave_solvers_) {
            total += solver->envelope_trace_seconds();
        }
        return total;
    }

    std::size_t batch_apg_evaluation_count() const {
        return batch_apg_evaluations_;
    }

    std::size_t batch_apg_aim_failure_count() const {
        return batch_apg_aim_failures_;
    }

    std::size_t batch_apg_serial_recovery_count() const {
        return batch_apg_serial_recoveries_;
    }

    std::size_t batch_apg_aimed_mismatch_count() const {
        return batch_apg_aimed_mismatches_;
    }

    std::size_t batch_apg_feasible_mismatch_count() const {
        return batch_apg_feasible_mismatches_;
    }

    std::size_t batch_apg_limiting_surface_mismatch_count() const {
        return batch_apg_limiting_surface_mismatches_;
    }

    std::size_t batch_apg_seed_mismatch_count() const {
        return batch_apg_seed_mismatches_;
    }

    std::size_t batch_apg_worker_count() const {
        return batch_apg_workers_;
    }

    double batch_apg_max_seed_delta() const {
        return batch_apg_max_seed_delta_;
    }

    double batch_apg_max_boundary_ratio_delta() const {
        return batch_apg_max_boundary_ratio_delta_;
    }

private:
    struct BatchApgThreadContext {
        AutomaticVignettingFieldSolver* worker = nullptr;
        const std::vector<std::pair<double, double>>* pupils = nullptr;
        std::vector<Sample>* samples = nullptr;
        std::atomic<std::size_t>* next_index = nullptr;
    };

    static void run_batch_apg_thread(BatchApgThreadContext* context);

#ifdef _WIN32
    static DWORD WINAPI batch_apg_thread_entry(void* raw_context) {
        run_batch_apg_thread(
            static_cast<BatchApgThreadContext*>(raw_context));
        return 0;
    }
#endif

    void evaluate_batch(
        const std::vector<std::pair<double, double>>& pupils,
        std::vector<Sample>* samples);

    void initialize_hard_apertures();

    bool strictly_aimed(const Ray& ray) const {
        return ray.valid && ray.aim_converged &&
               std::isfinite(ray.aim_residual) &&
               ray.aim_residual <= system_.ray_aiming.tolerance;
    }

    bool aim_target(double pupil_x, double pupil_y, Ray* ray_out);

    Ray generate_session_ray(
        const PupilPoint& pupil,
        std::size_t pupil_index);

    bool branch_recovery_enabled_ = false;
    std::vector<BranchSeed> branch_seeds_;
    LensSystem system_;
    SequentialRayTracer tracer_;
    LensSystem reference_system_;
    SequentialRayTracer reference_tracer_;
    AimingSession aiming_;
    LensSystem exact_fallback_system_;
    AimingSession exact_fallback_aiming_;
    std::size_t field_index_ = 0;
    std::size_t wave_index_ = 0;
    AutomaticVignettingConfig config_;
    Field field_;
    Wavelength wave_;
    Ray anchor_ray_;
    PupilPoint anchor_pupil_{0.0, 0.0, 1.0};
    bool chief_ray_attempted_ = false;
    bool anchor_ready_ = false;
    bool independent_apg_targets_ = false;
    std::size_t independent_pupil_index_ = 1;
    AimingSession* shared_aiming_ = nullptr;
    std::vector<std::pair<double, double>> preferred_seeds_;
    std::string seed_source_ = "none";
    std::size_t connected_sample_count_ = 0;
    std::size_t branch_reseed_attempts_ = 0;
    std::size_t branch_reseed_successes_ = 0;
    std::vector<std::pair<std::size_t, double>> hard_apertures_;
    std::vector<TracePoint> hard_aperture_history_;
    std::size_t pupil_evaluations_ = 0;
    std::size_t envelope_trace_calls_ = 0;
    std::size_t hard_aperture_surface_tests_ = 0;
    double aiming_seconds_ = 0.0;
    double envelope_trace_seconds_ = 0.0;
    std::size_t batch_apg_evaluations_ = 0;
    std::size_t batch_apg_aim_failures_ = 0;
    std::size_t batch_apg_serial_recoveries_ = 0;
    std::size_t batch_apg_aimed_mismatches_ = 0;
    std::size_t batch_apg_feasible_mismatches_ = 0;
    std::size_t batch_apg_limiting_surface_mismatches_ = 0;
    std::size_t batch_apg_seed_mismatches_ = 0;
    std::size_t batch_apg_workers_ = 1;
    double batch_apg_max_seed_delta_ = 0.0;
    double batch_apg_max_boundary_ratio_delta_ = 0.0;
    std::vector<std::unique_ptr<AutomaticVignettingFieldSolver>>
        additional_wave_solvers_;
    std::vector<std::unique_ptr<AutomaticVignettingFieldSolver>>
        batch_apg_worker_pool_;
};

struct AutomaticVignettingCenteredSections {
    bool valid = false;
    AutomaticVignettingFieldSolver::Interval x_interval;
    AutomaticVignettingFieldSolver::Interval y_interval;
    double center_x = 0.0;
    double center_y = 0.0;
    double center_residual = std::numeric_limits<double>::infinity();
    int iterations = 0;
    std::string message;
};

struct AutomaticVignettingFieldBatchContext {
    const LensSystem* system = nullptr;
    const AutomaticVignettingConfig* config = nullptr;
    std::vector<AutomaticVignettingResult>* field_runs = nullptr;
    std::vector<Field>* solved_fields = nullptr;
    std::atomic<std::size_t>* next_field = nullptr;
};

double square_error(double value, double target);
double bound_penalty(double value, double lower, double upper, double weight);
bool thickness_constraint_is_active(
    const MeritConfig& config,
    std::size_t surface_index);
bool is_interval_thickness_constraint_kind(const std::string& kind);
std::vector<PupilPoint> single_ray_guard_edge_pupils();
std::vector<PupilPoint> single_ray_spot_tail_extra_edge_pupils();
bool same_pupil_point(const PupilPoint& lhs, const PupilPoint& rhs);
bool is_single_ray_guard_edge_pupil(const PupilPoint& pupil);
bool is_vignetting_boundary_support_operand_name(const std::string& raw_name);
std::vector<PupilPoint> missing_single_ray_guard_edge_pupils(
    const std::vector<PupilPoint>& base_pupils);
std::vector<PupilPoint> missing_pupil_points(
    const std::vector<PupilPoint>& candidates,
    const std::vector<PupilPoint>& base_pupils);
void append_invalid_wavefront_guard_residuals(WavefrontStats& stats, std::size_t count);
double active_lower_bound(double value);
double active_upper_bound(double value);
double scoring_weight(double weight);
std::string normalized_name(std::string name);
std::string canonical_distortion_model(std::string model);
bool distortion_uses_f_theta(const LensSystem& system, const MeritConfig& config);
std::string effective_distortion_model_name(const LensSystem& system, const MeritConfig& config);
std::vector<Field> fields_or_default(const LensSystem& system);
std::vector<Wavelength> wavelengths_or_default(const LensSystem& system);
bool mode_uses_prescribed_stop_target(RayAimingMode mode);
std::pair<int, int> merit_field_pupil_shape(
    const LensSystem& system,
    const MeritConfig& config,
    const Field& field);
std::vector<PupilPoint> merit_field_pupils(
    const LensSystem& system,
    const MeritConfig& config,
    const Field& field,
    MeritPupilMetric metric);
PupilSymmetry field_pupil_symmetry(
    const Field& field,
    const MeritConfig& config);
bool pupil_transform_between(
    const PupilPoint& source,
    const PupilPoint& target,
    PupilSymmetry symmetry,
    PupilTransform* transform);
Vec3 transform_transverse(const Vec3& value, const PupilTransform& transform);
Ray transform_symmetric_ray(
    const Ray& source,
    const PupilTransform& transform,
    double target_weight);
std::vector<TracePoint> transform_symmetric_history(
    const std::vector<TracePoint>& source,
    const PupilTransform& transform);
SymmetricTraceBatch trace_pupil_batch(
    const LensSystem& system,
    const MeritConfig& config,
    SequentialRayTracer& tracer,
    AimingSession& aiming,
    std::size_t field_index,
    std::size_t wave_index,
    const Field& field,
    const Wavelength& wave,
    const std::vector<PupilPoint>& pupils,
    bool keep_history = false,
    std::size_t pupil_index_offset = 0,
    const std::vector<unsigned char>* history_mask = nullptr,
    bool predict_image_guard = false);
std::vector<unsigned char> pupil_order_history_mask(
    const Field& field,
    const MeritConfig& config,
    const std::vector<PupilPoint>& pupils);
bool pupil_order_enabled_for_field(
    const Field& field,
    const MeritConfig& config);
std::vector<PupilPoint> missing_exact_pupil_order_pupils(
    const Field& field,
    const std::vector<PupilPoint>& base_pupils);
std::size_t pupil_order_last_optical_surface(const LensSystem& system);
void apply_pupil_order_metrics(
    const LensSystem& system,
    const Field& field,
    const MeritConfig& config,
    const std::vector<unsigned char>& history_mask,
    const std::vector<std::vector<TracePoint>>& histories,
    SpotStats& stats);
double surface_vertex_z(const LensSystem& system, std::size_t surface_index);
double field_angle_radial_rad(const LensSystem& system, const Field& field);
std::pair<double, double> field_transverse_unit(const Field& field);
double clear_aperture_radius(const Surface& surface);
double mechanical_aperture_radius(const Surface& surface);
double edge_sample_aperture_radius(const Surface& surface);
double edge_sag_radius(const Surface& surface, double fallback_radius);
double aperture_limited_edge_sag(
    const Surface& surface,
    double sample_radius,
    double fallback_radius);
double edge_thickness_sample_radius(
    const Surface& a,
    const Surface& b,
    double fallback_radius);
double edge_thickness_at(const LensSystem& system, std::size_t i);
double stop_air_edge_thickness_at(const LensSystem& system, std::size_t stop);
bool image_space_interval(const LensSystem& system, std::size_t i);
double glass_edge_reference_radius(const LensSystem& system, std::size_t i);
double glass_interval_outer_diameter(const LensSystem& system, std::size_t i);
double glass_center_thickness_od_ratio(const LensSystem& system, std::size_t i);
double glass_edge_thickness_od_ratio(const LensSystem& system, std::size_t i);
bool independent_air_stop_surface(const Surface& surface);
double surface_bl0_ratio(const Surface& surface);
double minimum_surface_tangent_axis_angle_deg(
    const Surface& surface,
    int sample_count);
double surface_constraint_value(
    const LensSystem& system,
    const std::string& raw_kind,
    std::size_t index,
    std::size_t term);
double constraint_value(const LensSystem& system, const BoundConstraint& bound);
bool is_worst_wavelength_spot_operand_name(const std::string& raw_name);
bool is_spectral_spot_spread_operand_name(const std::string& raw_name);
bool is_relative_illumination_proxy_operand_name(const std::string& raw_name);
bool is_vignette_meridional_excess_operand_name(const std::string& raw_name);
int chromatic_quality_family(const std::string& raw_name);
double operand_value(const LensSystem& system, const Score& score, const MeritOperand& operand);
bool is_rms_spot_operand_name(const std::string& raw_name);
bool is_rms_wavefront_operand_name(const std::string& raw_name);
bool is_single_ray_spot_guard_operand_name(const std::string& raw_name);
bool is_single_ray_spot_tail_operand_name(const std::string& raw_name);
bool is_single_ray_wavefront_guard_operand_name(const std::string& raw_name);
bool is_wavefront_operand_name(const std::string& raw_name);
bool merit_needs_wavefront(const MeritConfig& config);
bool wavefront_uses_centroid_reference(const MeritConfig& config);
bool operand_needs_chief_ray_metrics(const MeritOperand& operand);
bool merit_needs_chief_ray_metrics(const MeritConfig& config);
bool ray_aiming_model_reuse_safe(RayAimingMode mode);
double operand_penalty(double value, const MeritOperand& operand);
double glass_proximity_distance(
    const LensSystem& system,
    const GlassProximityConstraint& constraint);
double glass_region_distance(
    const LensSystem& system,
    const GlassProximityConstraint& constraint);
bool interval_is_glass(const Surface& surface);
void update_mechanical_apertures_from_groups(LensSystem& system);
double zemax_style_merit(double sum, double weight_sum);
MeritContribution make_contribution(
    const std::string& source,
    const std::string& name,
    const std::string& mode,
    double value,
    double target,
    double lower,
    double upper,
    double weight,
    double penalty,
    std::size_t index = 0,
    std::size_t term = 0);
double configured_image_field_weight(
    const LensSystem& system,
    const MeritConfig& config,
    std::size_t field_index);
bool adaptive_quality_enabled(const MeritConfig& config);
bool coverage_first_merit_enabled(const MeritConfig& config);
bool unified_trace_gate_enabled(const MeritConfig& config);
double clamp01(double value);
double smoothstep(double value, double lo, double hi);
double pseudo_huber(double residual, double delta);
bool is_upper_mode(const std::string& mode);
bool is_lower_mode(const std::string& mode);
bool is_range_mode(const std::string& mode);
bool is_penalty_mode(const std::string& mode);
double contribution_delta_for_unified(const MeritContribution& contribution);
bool is_trace_source(const MeritContribution& contribution);
bool is_image_quality_name(const std::string& name);
bool is_edge_like_manufacturing_constraint_name(const std::string& name);
bool is_ratio_like_manufacturing_constraint_name(const std::string& name);
bool is_surface_tangent_angle_constraint_name(const std::string& name);
MeritCategory classify_unified_contribution(const MeritContribution& contribution);
double unified_tolerance_for_contribution(
    const MeritContribution& contribution,
    MeritCategory category,
    const MeritConfig& config);
double unified_base_weight_for_contribution(
    const MeritContribution& contribution,
    MeritCategory category,
    const MeritConfig& config,
    const LensSystem& system);
double group_weight_for_category(MeritCategory category, const MeritConfig& config);
bool is_spot_contribution(const MeritContribution& contribution);
double contribution_delta_for_quality(const MeritContribution& contribution);
double contribution_reference_for_quality(const MeritContribution& contribution, double fallback);
double quality_tolerance_for_contribution(
    const MeritContribution& contribution,
    const MeritConfig& config);
double equivalent_spot_normalized_residual(
    const MeritConfig& config,
    double delta,
    double tolerance);
double adaptive_quality_merit(const Score& score, const MeritConfig& config);
void append_unified_trace_contributions(Score& score, const MeritConfig& config);
double group_gate_for_category(MeritCategory category, const Score& score);
void apply_unified_trace_gate_merit(
    Score& score,
    const MeritConfig& config,
    const LensSystem& system);
bool soft_trace_policy(const MeritConfig& config);
double trace_priority_metric(const Score& score, const MeritConfig& config);
int axis_sign(double value, double eps);
double trace_axis_value(const TracePoint& point, double ux, double uy);
AxisCrossResult axis_sign_flip_count_and_first_z(
    const std::vector<TracePoint>& history,
    double ux,
    double uy,
    double eps);
void apply_center_field_structure_metrics(
    const std::vector<StructuralRaySample>& samples,
    const MeritConfig& config,
    double image_z,
    double last_optical_z,
    SpotStats& stats);
void apply_off_axis_chief_structure_metrics(
    const Field& field,
    const Ray& initial_chief,
    const std::vector<TracePoint>& history,
    std::size_t first_surface,
    const MeritConfig& config,
    SpotStats& stats);
double ray_nominal_weight(const Ray& ray);
double ray_trace_progress_fraction(const Ray& ray, std::size_t surface_count);
double ray_failure_penalty(const Ray& ray, const MeritConfig& config);
double ray_backward_propagation_penalty(const Ray& ray, const MeritConfig& config);
bool ray_aim_is_usable_for_spot(const Ray& ray, const MeritConfig& config);
bool normal_secondary_aperture_blockage(const Ray& ray);
bool ray_is_usable_for_spot(const Ray& ray, const MeritConfig& config);
bool calibrated_paraxial_stop_sampling_rejection(
    const Ray& ray,
    RayAimingMode aiming_mode,
    int stop_surface_index);
bool guard_ray_reached_inside_physical_stop(
    const LensSystem& system,
    const SequentialRayTracer& tracer,
    const Ray& ray);
double backward_surface_order_edge_deficit(const Ray& ray, const MeritConfig& config);
double guard_surface_order_edge_deficit(
    const LensSystem& system,
    const Ray& ray,
    const MeritConfig& config);
bool field_is_off_axis(const Field& field);
bool ray_is_usable_for_wavefront(const Ray& ray, const MeritConfig& config);
double image_space_refractive_index(const LensSystem& system, double wavelength_um);
double object_space_refractive_index(const LensSystem& system, double wavelength_um);
ObjectSpaceReferencePlane make_object_space_reference_plane(
    const LensSystem& system,
    const SequentialRayTracer& tracer,
    const Ray& chief_initial,
    double wavelength_um);
double object_space_reference_offset(
    const ObjectSpaceReferencePlane& reference,
    const Ray& initial_ray);
bool image_to_reference_sphere_opd(
    const Ray& ray,
    const Vec3& center,
    double radius,
    double n_image,
    double* opd_image_to_sphere,
    Vec3* pupil_point = nullptr);
std::size_t primary_wavelength_index(
    const LensSystem& system,
    const std::vector<Wavelength>& waves);
bool build_primary_wavefront_reference_sphere(
    const LensSystem& system,
    const MeritConfig& config,
    SequentialRayTracer& tracer,
    AimingSession& chief_aiming,
    std::size_t field_index,
    const Field& field,
    const std::vector<Wavelength>& waves,
    WavefrontReferenceSphere& reference);
void finalize_wavefront_stats(WavefrontStatsAccumulator& accumulator);
void accumulate_wavefront_stats_for_field_wave(
    const LensSystem& system,
    const MeritConfig& config,
    SequentialRayTracer& tracer,
    AimingSession& aiming,
    AimingSession& chief_aiming,
    std::size_t field_index,
    std::size_t wave_index,
    const Field& field,
    const Wavelength& wave,
    const std::vector<PupilPoint>& pupils,
    const WavefrontReferenceSphere& reference_sphere,
    WavefrontStatsAccumulator& accumulator);
bool solve_normal_equations(
    const std::vector<std::vector<double>>& normal,
    const std::vector<double>& rhs,
    std::vector<double>& solution);
double sparse_projected_illumination_area(
    const Field& field,
    const std::vector<PupilPoint>& pupils,
    const std::vector<Ray>& rays,
    RayAimingMode aiming_mode,
    int stop_surface_index);
bool fit_centroid_dircos_wavefront_model(
    const std::vector<CentroidDircosWavefrontSample>& samples,
    std::size_t wave_count,
    bool use_dir_x,
    bool use_dir_y,
    CentroidDircosWavefrontFit& fit);
bool build_centroid_piston_only_wavefront_model(
    const std::vector<CentroidDircosWavefrontSample>& samples,
    std::size_t wave_count,
    CentroidDircosWavefrontFit& fit);
double centroid_dircos_wavefront_model_value(
    const CentroidDircosWavefrontSample& sample,
    const CentroidDircosWavefrontFit& fit);
void collect_centroid_dircos_wavefront_samples_for_field_wave(
    const LensSystem& system,
    const MeritConfig& config,
    SequentialRayTracer& tracer,
    AimingSession& aiming,
    AimingSession& chief_aiming,
    std::size_t field_index,
    std::size_t wave_index,
    const Field& field,
    const Wavelength& wave,
    const std::vector<PupilPoint>& pupils,
    const WavefrontReferenceSphere& reference_sphere,
    std::vector<CentroidDircosWavefrontSample>& samples,
    WavefrontStatsAccumulator& accumulator,
    std::size_t pupil_index_offset = 0);
void accumulate_centroid_dircos_wavefront_stats_for_field(
    const LensSystem& system,
    const MeritConfig& config,
    SequentialRayTracer& tracer,
    AimingSession& aiming,
    AimingSession& chief_aiming,
    std::size_t field_index,
    const Field& field,
    const std::vector<Wavelength>& waves,
    const std::vector<PupilPoint>& pupils,
    const WavefrontReferenceSphere& reference_sphere,
    WavefrontStatsAccumulator& accumulator);
WavefrontStats compute_wavefront_stats_for_field(
    const LensSystem& system,
    const MeritConfig& config,
    SequentialRayTracer& tracer,
    AimingSession& aiming,
    AimingSession& chief_aiming,
    std::size_t field_index,
    const Field& field,
    const std::vector<Wavelength>& waves,
    const std::vector<PupilPoint>& pupils);
double automatic_vignetting_hard_radius(const Surface& surface);
LensSystem automatic_vignetting_exact_fallback_system(const LensSystem& system);
LensSystem automatic_vignetting_reference_pupil_system(
    const LensSystem& system);
std::size_t automatic_vignetting_wavelength_index(
    const LensSystem& system,
    const AutomaticVignettingConfig& config);
bool automatic_vignetting_batch_apg_experiment_enabled();
bool automatic_vignetting_batch_apg_audit_enabled();
int automatic_vignetting_batch_apg_workers();
LensSystem automatic_vignetting_local_apg_worker_system(
    const LensSystem& system);
AutomaticVignettingCenteredSections
solve_automatic_vignetting_centered_sections(
    AutomaticVignettingFieldSolver& solver,
    double initial_center_x,
    double initial_center_y,
    double pupil_tolerance);
Field contract_automatic_vignetting_field(
    const Field& field,
    double contraction);
double automatic_vignetting_max_affine_pupil_radius_squared(
    const Field& field);
bool project_automatic_vignetting_x_inside_unit_pupil(
    Field* field,
    double* accepted_x_contraction);
bool automatic_vignetting_final_pupil_valid(
    const LensSystem& system,
    std::size_t field_index,
    const std::vector<std::size_t>& wave_indices);
bool contract_to_valid_automatic_vignetting_field(
    LensSystem& system,
    std::size_t field_index,
    const std::vector<std::size_t>& wave_indices,
    Field* solved,
    double* accepted_contraction);
void run_automatic_vignetting_field_batch(
    AutomaticVignettingFieldBatchContext* context);
DWORD WINAPI automatic_vignetting_field_batch_thread_entry(
    void* raw_context);
}  // namespace analysis_detail

WorkingFNumberMetrics working_f_number_metrics(const LensSystem& system);
SpotStats compute_spot_stats(const std::vector<Ray>& rays);
SpotStats compute_spot_stats(
    const std::vector<Ray>& rays,
    const MeritConfig& config,
    std::size_t surface_count,
    RayAimingMode aiming_mode,
    int stop_surface_index);
bool single_ray_spot_tail_enabled(const MeritConfig& config);
bool minimum_relative_illumination_enabled(const MeritConfig& config);
bool relative_illumination_proxy_requested(const MeritConfig& config);
bool uses_selective_spot_tail_edge_sampling(
    const MeritConfig& config,
    const Field& field);
bool is_selective_spot_tail_edge_pupil(
    const MeritConfig& config,
    const Field& field,
    const PupilPoint& pupil);
void update_spot_single_ray_guard_from_edges(
    const LensSystem& system,
    const MeritConfig& config,
    SequentialRayTracer& tracer,
    AimingSession& aiming,
    std::size_t field_index,
    const Field& field,
    const std::vector<Wavelength>& waves,
    const std::vector<PupilPoint>& rms_pupils,
    const std::vector<Ray>& rms_rays,
    SpotStats& stats);
std::vector<FieldSpot> spot_diagram_stats(const LensSystem& system, int rings);
std::vector<FieldSpot> spot_diagram_stats(const LensSystem& system, const MeritConfig& config);
std::vector<FieldSpot> polychromatic_field_spot_stats_with_context(
    const LensSystem& system,
    const MeritConfig& config,
    SequentialRayTracer& tracer,
    AimingSession& aiming);
std::vector<FieldSpot> polychromatic_field_spot_stats(const LensSystem& system, const MeritConfig& config);
std::vector<FieldWavefront> wavefront_stats_with_context(
    const LensSystem& system,
    const MeritConfig& config,
    SequentialRayTracer& tracer,
    AimingSession& aiming);
std::vector<FieldWavefront> wavefront_stats(const LensSystem& system, const MeritConfig& config);
double mean_rms_spot(const LensSystem& system, int rings, double* max_spot);
double mean_rms_spot(const LensSystem& system, const MeritConfig& config, double* max_spot);
double mean_rms_wavefront(const LensSystem& system, const MeritConfig& config, double* max_wavefront);
double field_tangent_radius(const LensSystem& system, const Field& field);
double field_angle_rad(const LensSystem& system, const Field& field);
double paraxial_reference_focal_length(const LensSystem& system);
bool image_height_side_flipped(double ideal_height, double spot_height);
ChiefRayMetrics chief_ray_metrics_with_context(
    const LensSystem& system,
    const MeritConfig& config,
    SequentialRayTracer& tracer,
    AimingSession& aiming);
ChiefRayMetrics chief_ray_metrics(const LensSystem& system);
RelativeIlluminationResult relative_illumination(
    const LensSystem& system,
    const RelativeIlluminationConfig& requested_config);
ChiefRayMetrics chief_ray_metrics(const LensSystem& system, const MeritConfig& config);
double ray_axis_coord(const Ray& ray, double ax, double ay);
double ray_axis_slope(const Ray& ray, double ax, double ay);
bool finite_image_ray(const Ray& ray);
double focus_shift_between(const Ray& ray, const Ray& chief, double ax, double ay);
double finite_mean(std::initializer_list<double> values);
double finite_spread(std::initializer_list<double> values);
Ray traced_finite_ray(
    SequentialRayTracer& tracer,
    AimingSession& aiming,
    const LensSystem& system,
    std::size_t field_index,
    std::size_t wave_index,
    std::size_t pupil_index,
    const Field& field,
    const Wavelength& wave,
    const PupilPoint& pupil);
FiniteRayFieldMetric finite_ray_field_metric(
    SequentialRayTracer& tracer,
    AimingSession& aiming,
    const LensSystem& system,
    std::size_t field_index,
    std::size_t wave_index,
    const Field& field,
    const Wavelength& wave,
    int* attempted,
    int* valid);
FiniteRayAberrationMetrics finite_ray_aberration_metrics_with_context(
    const LensSystem& system,
    const MeritConfig& config,
    SequentialRayTracer& tracer,
    AimingSession& aiming);
FiniteRayAberrationMetrics finite_ray_aberration_metrics(
    const LensSystem& system,
    const MeritConfig& config);
double potential_clamp01(double value);
double robust_norm_value(double value, double tolerance);
double direction_angle_deg(const TracePoint& a, const TracePoint& b);
double chief_path_bend_budget_deg(double field_angle_deg);
double health_from_weighted_norms(
    std::initializer_list<std::pair<double, double>> weighted_norms,
    double* rms_out = nullptr,
    double* worst_out = nullptr);
double first_order_tolerance(const std::string& raw_key, double reference, const MeritConfig& config);
bool operand_matches_first_order(const MeritOperand& operand, const std::string& key);
double first_order_norm_for_operand(double value, const MeritOperand& operand, const std::string& key, const MeritConfig& config);
double first_order_norm_for_key(const Score& score, const MeritConfig& config, const std::string& key);
void compute_topology_potential_v2(Score& score, const MeritConfig& config);
void compute_topology_potential_v3(Score& score, const MeritConfig& config);
double estimate_effective_focal_length(const LensSystem& system);
double paraxial_focus_position(const LensSystem& system, double wavelength_um);
double paraxial_axial_color_focus_shift(const LensSystem& system);
double back_focal_length(const LensSystem& system);
double min_edge_thickness(const LensSystem& system);
AutomaticVignettingResult set_automatic_vignetting(
    LensSystem& system,
    const AutomaticVignettingConfig& config);
void sync_stop_aperture_to_aiming_radius(LensSystem& system);
void update_clear_apertures_from_trace(
    LensSystem& system,
    int rings,
    const std::string& distribution,
    unsigned int seed,
    RayAimingMode mode,
    double margin_factor,
    bool preserve_explicit_stop,
    int arms,
    bool use_current_stop_surface_radius,
    bool require_full_trace,
    double vignetting_boundary_support_tolerance,
    double vignetting_boundary_support_selectivity_slope,
    bool vignetting_global_trim_realizability,
    bool vignetting_physical_cut_match_enabled,
    bool primary_wavelength_apertures);
void populate_dls_ray_spot_residuals(
    Score& score,
    const std::vector<FieldSpot>& field_spots,
    const MeritConfig& config);
double single_ray_guard_upper(
    const std::string& raw_mode,
    double target,
    double raw_upper);
double single_ray_guard_upper_from_contribution(const MeritContribution& contribution);
double single_ray_guard_upper_from_operand(const MeritOperand& operand);
double dls_contribution_residual_scale(
    const MeritContribution& contribution,
    double tolerance);
void populate_dls_single_ray_guard_residuals(
    Score& score,
    const std::vector<FieldSpot>& field_spots,
    const std::vector<FieldWavefront>& field_wavefronts,
    const MeritConfig& config);
}  // namespace optiland
