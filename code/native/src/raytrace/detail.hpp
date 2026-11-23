#pragma once

// Private interfaces shared by the raytrace implementation units.
#include "optiland/spectral_aperture.hpp"
#include "optiland/raytrace.hpp"
#include "optiland/cpu_vignetted_continuation.hpp"
#include "optiland/cpu_xy_continuation_solve.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <condition_variable>
#include <mutex>
#include <thread>
#endif


namespace optiland { namespace raytrace_detail {

extern std::atomic<AsphereRootPolicy> g_asphere_root_policy;

extern std::atomic<bool> g_stop_aim_parallel_jacobian_enabled;

bool environment_flag_enabled(const char* name);

std::string normalized_mapping_mode(std::string value, const std::string& fallback);

struct RayAimingDiagnosticsState {
    std::atomic<bool> enabled{false};
    std::atomic<long long> calibrated_generate_calls{0};
    std::atomic<long long> calibrated_generate_ns{0};
    std::atomic<long long> calibrated_model_requests{0};
    std::atomic<long long> calibrated_model_cache_hits{0};
    std::atomic<long long> calibrated_model_builds{0};
    std::atomic<long long> calibrated_model_build_ns{0};
    std::atomic<long long> calibrated_model_valid{0};
    std::atomic<long long> calibrated_model_invalid{0};
    std::atomic<long long> calibrated_paraxial_chief_attempts{0};
    std::atomic<long long> calibrated_paraxial_chief_accepted{0};
    std::atomic<long long> calibrated_budget_chief_attempts{0};
    std::atomic<long long> calibrated_budget_chief_seed_valid{0};
    std::atomic<long long> calibrated_budget_chief_accepted{0};
    std::atomic<long long> calibrated_budget_chief_rejected{0};
    std::atomic<long long> calibrated_budget_chief_singular{0};
    std::atomic<long long> calibrated_budget_chief_oob{0};
    std::atomic<long long> calibrated_budget_chief_trace_fail{0};
    std::atomic<long long> calibrated_budget_chief_residual_reject{0};
    std::atomic<long long> calibrated_budget_chief_inside_stop_accepted{0};
    std::atomic<long long> calibrated_budget_chief_stop_oob_reject{0};
    std::atomic<long long> calibrated_budget_chief_seed_extract_fail{0};
    std::atomic<long long> calibrated_budget_chief_ns{0};
    std::atomic<long long> calibrated_budget_chief_trial_rays{0};
    std::atomic<long long> calibrated_budget_chief_correction_calls{0};
    std::atomic<long long> calibrated_budget_chief_correction_iterations{0};
    std::atomic<long long> calibrated_budget_chief_stop_probe_calls{0};
    std::atomic<long long> calibrated_budget_chief_stop_probe_successes{0};
    std::atomic<long long> calibrated_budget_chief_stop_probe_ns{0};
    std::atomic<long long> calibrated_budget_chief_residual_count{0};
    std::atomic<long long> calibrated_budget_chief_residual_sum_scaled{0};
    std::atomic<long long> calibrated_budget_chief_residual_max_scaled{0};
    std::atomic<long long> calibrated_chief_anchor_requests{0};
    std::atomic<long long> calibrated_chief_anchor_cache_hits{0};
    std::atomic<long long> calibrated_chief_anchor_builds{0};
    std::atomic<long long> calibrated_chief_anchor_valid{0};
    std::atomic<long long> calibrated_chief_anchor_invalid{0};
    std::atomic<long long> calibrated_chief_anchor_ns{0};
    std::atomic<long long> calibrated_chief_stop_probe_calls{0};
    std::atomic<long long> calibrated_chief_stop_probe_successes{0};
    std::atomic<long long> calibrated_chief_stop_probe_ns{0};
    std::atomic<long long> calibrated_chief_residual_count{0};
    std::atomic<long long> calibrated_chief_residual_sum_scaled{0};
    std::atomic<long long> calibrated_chief_residual_max_scaled{0};
    std::atomic<long long> calibrated_probe_sample_calls{0};
    std::atomic<long long> calibrated_probe_sample_successes{0};
    std::atomic<long long> calibrated_probe_sample_ns{0};
    std::atomic<long long> calibrated_central_fit_attempts{0};
    std::atomic<long long> calibrated_central_fit_successes{0};
    std::atomic<long long> calibrated_central_fit_ns{0};
    std::atomic<long long> calibrated_grid_fit_attempts{0};
    std::atomic<long long> calibrated_grid_fit_successes{0};
    std::atomic<long long> calibrated_grid_fit_ns{0};
    std::atomic<long long> calibrated_edge_solve_calls{0};
    std::atomic<long long> calibrated_edge_correction_calls{0};
    std::atomic<long long> calibrated_edge_correction_iterations{0};
    std::atomic<long long> calibrated_edge_correction_ns{0};
    std::atomic<long long> calibrated_edge_residual_count{0};
    std::atomic<long long> calibrated_edge_residual_sum_scaled{0};
    std::atomic<long long> calibrated_edge_residual_max_scaled{0};
    std::atomic<long long> calibrated_interpolation_probe_calls{0};
    std::atomic<long long> calibrated_interpolation_probe_successes{0};
    std::atomic<long long> calibrated_interpolation_probe_seed_failures{0};
    std::atomic<long long> calibrated_interpolation_probe_trace_failures{0};
    std::atomic<long long> calibrated_interpolation_residual_over_limit{0};
    std::atomic<long long> calibrated_interpolation_residual_count{0};
    std::atomic<long long> calibrated_interpolation_residual_sum_scaled{0};
    std::atomic<long long> calibrated_interpolation_residual_max_scaled{0};
    std::atomic<long long> calibrated_fallback_calls{0};
    std::atomic<long long> calibrated_fallback_none_calls{0};
    std::atomic<long long> calibrated_fallback_other_calls{0};
    std::atomic<long long> calibrated_fallback_model_invalid_calls{0};
    std::atomic<long long> calibrated_fallback_seed_oob_calls{0};
    std::atomic<long long> calibrated_fallback_ray_invalid_calls{0};
    std::atomic<long long> calibrated_correction_calls{0};
    std::atomic<long long> stop_residual_eval_calls{0};
    std::atomic<long long> stop_residual_eval_successes{0};
    std::atomic<long long> stop_residual_eval_ns{0};
    std::atomic<long long> stop_aim_trace_residual_calls{0};
    std::atomic<long long> stop_aim_trace_residual_successes{0};
    std::atomic<long long> stop_aim_trace_residual_ns{0};
    std::atomic<long long> stop_aim_parallel_jacobian_pairs{0};
    std::atomic<long long> stop_aim_parallel_jacobian_ns{0};
    std::atomic<long long> three_step_generate_calls{0};
    std::atomic<long long> three_step_generate_ns{0};
    std::atomic<long long> three_step_model_requests{0};
    std::atomic<long long> three_step_model_cache_hits{0};
    std::atomic<long long> three_step_model_builds{0};
    std::atomic<long long> three_step_model_build_ns{0};
    std::atomic<long long> three_step_model_valid{0};
    std::atomic<long long> three_step_model_invalid{0};
    std::atomic<long long> three_step_reverse_trial_calls{0};
    std::atomic<long long> three_step_reverse_trial_successes{0};
    std::atomic<long long> three_step_reverse_trial_ns{0};
    std::atomic<long long> three_step_center_from_reverse{0};
    std::atomic<long long> three_step_center_fallback_calls{0};
    std::atomic<long long> three_step_center_residual_count{0};
    std::atomic<long long> three_step_center_residual_sum_scaled{0};
    std::atomic<long long> three_step_center_residual_max_scaled{0};
    std::atomic<long long> three_step_edge_solve_calls{0};
    std::atomic<long long> three_step_edge_initial_ray_calls{0};
    std::atomic<long long> three_step_edge_correction_calls{0};
    std::atomic<long long> three_step_edge_correction_iterations{0};
    std::atomic<long long> three_step_edge_correction_ns{0};
    std::atomic<long long> three_step_edge_solve_ns{0};
    std::atomic<long long> three_step_edge_residual_count{0};
    std::atomic<long long> three_step_edge_residual_sum_scaled{0};
    std::atomic<long long> three_step_edge_residual_max_scaled{0};
    std::atomic<long long> three_step_model_stop_probe_calls{0};
    std::atomic<long long> three_step_model_stop_probe_successes{0};
    std::atomic<long long> three_step_model_stop_probe_ns{0};
    std::atomic<long long> three_step_generate_stop_probe_calls{0};
    std::atomic<long long> three_step_generate_stop_probe_successes{0};
    std::atomic<long long> three_step_generate_stop_probe_ns{0};
    std::atomic<long long> three_step_generate_correction_calls{0};
    std::atomic<long long> three_step_generate_correction_iterations{0};
    std::atomic<long long> three_step_generate_correction_ns{0};
    std::atomic<long long> three_step_fallback_calls{0};
    std::atomic<long long> three_step_fallback_none_calls{0};
};

RayAimingDiagnosticsState& diagnostics_state();

bool diagnostics_active();

void add_diag(std::atomic<long long>& counter, long long value = 1);

constexpr double kDiagResidualScale = 1.0e9;

long long scaled_residual(double residual);

void update_diag_max(std::atomic<long long>& target, long long value);

void add_diag_residual(std::atomic<long long>& count,
                       std::atomic<long long>& sum_scaled,
                       std::atomic<long long>& max_scaled,
                       double residual);

long long aiming_clock_ns();

class ScopedDiagTimer {
public:
    explicit ScopedDiagTimer(std::atomic<long long>& target)
        : target_(diagnostics_active() ? &target : nullptr),
          start_(aiming_clock_ns()) {}

    ~ScopedDiagTimer() {
        if (!target_) {
            return;
        }
        target_->fetch_add(aiming_clock_ns() - start_, std::memory_order_relaxed);
    }

private:
    std::atomic<long long>* target_;
    long long start_;
};

bool cached_wavelength_equal(double a, double b);

const char* ray_failure_code_probe_label(RayFailureCode code);

bool lookup_wavelength_cache(
    const std::vector<std::pair<double, double>>& cache,
    double wavelength_um,
    double* value);

double store_wavelength_cache(
    std::vector<std::pair<double, double>>& cache,
    double wavelength_um,
    double value);

bool aperture_clip_exceeded(double radial, double clip_radius);

constexpr unsigned char kPreparedGeometrySkip = 0;

constexpr unsigned char kPreparedGeometryPlane = 1;

constexpr unsigned char kPreparedGeometrySphere = 2;

double aperture_clip_limit_sq(double clip_radius);

bool stop_surface_forward_root_interval(
    const Surface& surface,
    const Ray& ray,
    double* lower,
    double* upper);

bool aperture_clip_exceeded_sq(double radial_sq, double clip_limit_sq);

bool stable_standard_sphere_intersect(
    double curvature,
    double px,
    double py,
    double pz,
    double dx,
    double dy,
    double dz,
    double* t);

bool fast_standard_sphere_or_plane_intersect(
    const Surface& surface,
    double px,
    double py,
    double pz,
    double dx,
    double dy,
    double dz,
    double* t);

bool prepared_geometry_intersect(
    unsigned char geometry,
    double radius,
    double inv_radius,
    double px,
    double py,
    double pz,
    double dx,
    double dy,
    double dz,
    double* t);

bool fast_standard_sphere_or_plane_intersect(const Surface& surface, const Ray& local_ray, double* t);

Vec3 prepared_geometry_normal(
    unsigned char geometry,
    double radius,
    double inv_radius,
    double px,
    double py,
    double pz);

bool refract_assume_unit(Vec3 incident, Vec3 normal, double n1, double n2, Vec3* transmitted);

double condition_number_2x2(double a, double b, double c, double d);

void append_unique_positive(std::vector<double>& values, double value);

double aperture_aware_halfwidth(double base_halfwidth, double min_halfwidth, double stop_scale);

double aperture_aware_probe_step(
    double base_step,
    double min_step,
    double max_step,
    double stop_scale);

std::vector<double> aperture_aware_probe_steps(
    double first_surface_radius,
    double stop_scale,
    double probe_fraction,
    double min_probe_step,
    double max_probe_step);

Vec3 field_direction_cosines(const LensSystem& system, const Field& field);

double infinite_object_start_distance(
    const LensSystem& system,
    double aperture_radius,
    double target_z);

double infinite_object_aim_position_step_cap(const LensSystem& system, double stop_radius);

bool set_finite_object_ray(
    Ray* ray,
    const Vec3& target,
    const Vec3& direction,
    double object_distance);

void mark_ray_failure(
    Ray& ray,
    RayFailureCode code,
    int surface = -1,
    double severity = 1.0,
    bool invalidate = true);

void record_backward_surface_overlap(
    Ray& ray,
    const LensSystem& system,
    std::size_t surface_index,
    double vertex_z,
    double x,
    double y,
    double z);

void mark_backward_propagation_warning(
    Ray& ray,
    int surface,
    double severity);

struct ScalarInterval {
    double lo = 0.0;
    double hi = 0.0;
    bool valid = false;
};

ScalarInterval outward_interval(double lo, double hi);

ScalarInterval point_interval(double value);

ScalarInterval whole_interval();

ScalarInterval interval_add(const ScalarInterval& a, const ScalarInterval& b);

ScalarInterval interval_subtract(const ScalarInterval& a, const ScalarInterval& b);

ScalarInterval interval_multiply(const ScalarInterval& a, const ScalarInterval& b);

ScalarInterval interval_scale(const ScalarInterval& value, double scale);

ScalarInterval interval_divide(const ScalarInterval& numerator, const ScalarInterval& denominator);

ScalarInterval interval_square(const ScalarInterval& value);

ScalarInterval interval_sqrt(const ScalarInterval& value);

ScalarInterval affine_interval(double origin, double direction, double lo, double hi);

ScalarInterval even_asphere_sag_interval(
    const Surface& surface,
    const ScalarInterval& r2);

ScalarInterval even_asphere_residual_interval(
    const Surface& surface,
    const Ray& ray,
    double lo,
    double hi);

ScalarInterval even_asphere_derivative_interval(
    const Surface& surface,
    const Ray& ray,
    double lo,
    double hi);

bool even_asphere_residual_at(
    const Surface& surface,
    const Ray& ray,
    double tv,
    double* residual);

bool even_asphere_derivative_at(
    const Surface& surface,
    const Ray& ray,
    double tv,
    double* derivative);

bool even_asphere_residual_and_derivative_uc10(
    const Surface& surface,
    const Ray& ray,
    double tv,
    double* residual,
    double* derivative);

bool refine_even_asphere_seed(
    const Surface& surface,
    const Ray& ray,
    double seed,
    double lower,
    double upper,
    double* refined,
    bool strict_position = false);

bool refine_even_asphere_seed_uc10(
    const Surface& surface,
    const Ray& ray,
    double seed,
    double lower,
    double upper,
    double* refined);

bool intersect_even_asphere_from_local_seed(
    const Surface& surface,
    const Ray& ray,
    double seed,
    double* root);

bool solve_even_asphere_bracket(
    const Surface& surface,
    const Ray& ray,
    double lo,
    double hi,
    double f_lo,
    double f_hi,
    double* root,
    bool strict_residual = false);

enum class FirstRootSearchStatus {
    NoRoot,
    Found,
    Ambiguous,
};

struct FirstRootSearchBudget {
    int nodes = 0;
    int max_nodes = 4096;
};

FirstRootSearchStatus find_first_even_asphere_root_interval(
    const Surface& surface,
    const Ray& ray,
    double lo,
    double hi,
    int depth,
    FirstRootSearchBudget* budget,
    double* root,
    bool invalid_interval_is_ambiguous = false);

bool certify_even_asphere_residual_monotonic(
    const Surface& surface,
    const Ray& ray,
    double lower,
    double upper);

bool polish_and_certify_even_asphere_first_root_candidate(
    const Surface& surface,
    const Ray& ray,
    double lower,
    double upper,
    double candidate,
    double* certified_candidate);

FirstRootSearchStatus find_certified_root_before(
    const Surface& surface,
    const Ray& ray,
    double lower,
    double candidate,
    double* root);

bool even_asphere_forward_search_interval(
    const Surface& surface,
    const Ray& ray,
    double requested_lower,
    double requested_upper,
    double* lower,
    double* upper);

FirstRootSearchStatus find_first_even_asphere_root_fallback(
    const Surface& surface,
    const Ray& ray,
    double requested_lower,
    double requested_upper,
    double* root);

double production_asphere_radial_slope(
    const Surface& surface,
    double radius);

bool production_asphere_radial_slope_interval(
    const Surface& surface,
    double radius_lo,
    double radius_hi,
    double* slope_lo,
    double* slope_hi);

bool production_asphere_slope_interval_below_limit(
    const Surface& surface,
    double radius_lo,
    double radius_hi,
    double max_slope);

double production_asphere_slope_domain_radius_squared_uncached(
    const Surface& surface);

enum class UnifiedAsphereDomainStatus {
    Finite,
    Unbounded,
    Unresolved,
};

struct UnifiedAsphereDomain {
    UnifiedAsphereDomainStatus status =
        UnifiedAsphereDomainStatus::Unresolved;
    double radius_squared = -1.0;
};

double unified_asphere_slope_margin(
    const Surface& surface,
    double radius,
    double max_slope);

UnifiedAsphereDomain unified_asphere_domain_uncached(const Surface& surface);

std::uint64_t production_asphere_domain_signature(const Surface& surface);

double production_asphere_slope_domain_radius_squared(const Surface& surface);

UnifiedAsphereDomain unified_asphere_domain(const Surface& surface);

bool unified_direction_certificate(const Ray& ray);

bool restrict_search_to_production_slope_domain(
    const Surface& surface,
    const Ray& ray,
    double radius_squared,
    double* lower,
    double* upper);

bool production_asphere_requires_topology_seed(
    const Surface& surface,
    double domain_radius_squared);

bool production_ray_requires_topology_seed(
    const Surface& surface,
    const Ray& ray,
    double domain_radius_squared);

bool refine_production_asphere_root_fast(
    const Surface& surface,
    const Ray& ray,
    double start_t,
    double* root);

bool production_topology_linear_seed(
    const Surface& surface,
    const Ray& ray,
    double search_lower,
    double search_upper,
    double* root);

bool production_topology_seeded_before(
    const Surface& surface,
    const Ray& ray,
    double search_lower,
    double candidate,
    double* root);

int collect_production_precondition_seeds(
    const Surface& surface,
    const Ray& ray,
    double* seeds);

bool refine_production_preconditioned_seed(
    const Surface& surface,
    const Ray& ray,
    double raw_seed,
    double lower,
    double upper,
    double* root);

bool production_seed_preconditioned_no_b2(
    const Surface& surface,
    const Ray& ray,
    double lower,
    double upper,
    double* root);

double spectral_surface_radius(const Surface& surface);

void record_secondary_aperture_hit(
    Ray& ray, const Surface& surface, int index, double x, double y);

} // namespace raytrace_detail

void set_asphere_root_policy(AsphereRootPolicy policy);

AsphereRootPolicy asphere_root_policy();

void set_stop_aim_parallel_jacobian_enabled(bool enabled);

bool stop_aim_parallel_jacobian_enabled();

void set_ray_aiming_diagnostics_enabled(bool enabled);

bool ray_aiming_diagnostics_enabled();

void reset_ray_aiming_diagnostics();

RayAimingDiagnosticsSnapshot ray_aiming_diagnostics_snapshot();

bool ray_aiming_uses_session(RayAimingMode mode);

double surface_sag(const Surface& surface, double x, double y);

double surface_profile_radius_limit(const Surface& surface);

double profile_surface_sag(const Surface& surface, double y, double profile_radius);

Vec3 surface_normal(const Surface& surface, const Vec3& local_point);

bool intersect_surface(
    const Surface& surface,
    const Ray& local_ray,
    double* t,
    bool ignore_stop_aperture);

bool refract(Vec3 incident, Vec3 normal, double n1, double n2, Vec3* transmitted);

Vec3 reflect(Vec3 incident, Vec3 normal);

double material_before_surface(const LensSystem& system, std::size_t surface_index, double wavelength_um);

double surface_vertex_z_for_system(const LensSystem& system, std::size_t surface_index);

bool surface_aperture_allows(const Surface& surface, double x, double y);

bool localize_first_surface_seed_ray(
    const LensSystem& system,
    double wavelength_um,
    double seed_x,
    double seed_y,
    Ray* ray);

bool reverse_refract_at_surface(
    const LensSystem& system,
    std::size_t surface_index,
    double wavelength_um,
    Ray* ray);

bool reverse_trace_stop_center_to_first(
    const LensSystem& system,
    double wavelength_um,
    double meridional_x,
    double meridional_y,
    double angle_deg,
    Vec3* first_surface_point,
    Vec3* object_space_forward_direction);

void paraxial_refract_forward(
    const LensSystem& system,
    std::size_t surface_index,
    double wavelength_um,
    double y,
    double* u);

void paraxial_refract_reverse(
    const LensSystem& system,
    std::size_t surface_index,
    double wavelength_um,
    double y,
    double* u);

bool paraxial_trace_to_surface(
    const LensSystem& system,
    double y0,
    double u0,
    double z0,
    double wavelength_um,
    std::size_t surface_index,
    double* y_at_surface,
    double* u_after_surface = nullptr,
    std::size_t start_surface = std::numeric_limits<std::size_t>::max());

bool first_surface_to_stop_abcd(
    const LensSystem& system,
    double wavelength_um,
    std::size_t stop_index,
    double* a,
    double* b);

double paraxial_effective_focal_length(const LensSystem& system, double wavelength_um);
} // namespace optiland

#include "diagnostics_inline.hpp"
