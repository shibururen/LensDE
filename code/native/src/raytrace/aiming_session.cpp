// Initialize per-system aiming state and translate solved rays into reusable session seeds.
// The model builders and generators share these exact-key caches and STOP sampling helpers.
// Aiming acceptance and remembered solutions must preserve wavelength, field, and pupil identity.

#include "aiming_geometry_inline.hpp"
#include "diagnostics_inline.hpp"
#include "prepared_geometry_inline.hpp"
#include "trace_diagnostics_inline.hpp"
#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

AimingSession::AimingSession(const LensSystem& system)
    : system_(system),
      tracer_(system) {}

const AimingSession::CalibratedRuntimeOptions& AimingSession::calibrated_runtime_options() {
    auto& options = calibrated_runtime_options_;
    if (!options.initialized) {
        const char* path = std::getenv("OPTILAND_EDGE_PATH_MODE");
        options.continuation_mode = path ? std::atoi(path) : 1961;
        const char* debug = std::getenv("OPTILAND_CPU_CALIBRATED_CHIEF_DEBUG_FIELD");
        options.chief_debug_enabled = debug != nullptr;
        options.chief_debug_all = debug && std::strcmp(debug, "all") == 0;
        options.chief_debug_field = debug ? std::atoi(debug) : 0;
        options.model_dump = std::getenv("OPTILAND_CPU_CONTINUATION_MODEL_DUMP") != nullptr;
        options.initialized = true;
    }
    return options;
}

double AimingSession::first_surface_hard_radius(double wavelength_um) const {
    if (system_.defer_secondary_aperture_clipping(wavelength_um > 0.0
            ? wavelength_um : system_.primary_wavelength())) return 0.0;
    const std::size_t first = system_.first_ray_aiming_surface(system_.primary_wavelength());
    if (first < system_.surfaces.size()) {
        const auto& surface = system_.surfaces[first];
        if (surface.aperture_radius > 0.0) {
            return surface.aperture_radius;
        }
        if (surface.clear_aperture_fixed && surface.semi_aperture > 0.0) {
            return surface.semi_aperture;
        }
    }
    return 0.0;
}

double AimingSession::first_surface_search_radius(double wavelength_um) const {
    const double hard_radius = first_surface_hard_radius(wavelength_um);
    if (hard_radius > 0.0) {
        return hard_radius;
    }

    const double ep_radius =
        std::max(0.0, tracer_.entrance_pupil_radius(system_.primary_wavelength()));
    const double track = std::isfinite(system_.total_track()) ? std::abs(system_.total_track()) : 0.0;
    const double theta = std::isfinite(system_.max_field_deg)
                             ? std::abs(deg_to_rad(system_.max_field_deg))
                             : 0.0;
    const double field_extent = track * std::min(1.0, std::abs(std::sin(theta)));
    const double field_slope = std::isfinite(theta)
                                   ? std::min(64.0, std::abs(std::tan(theta)))
                                   : 0.0;
    const double slope_extent = track * field_slope;
    return std::max({
        1.0,
        4.0 * ep_radius,
        2.0 * field_extent + 4.0 * ep_radius,
        0.75 * slope_extent + 4.0 * ep_radius,
    });
}

Ray AimingSession::make_failed_ray(
    const Wavelength& wave,
    const PupilPoint& pupil,
    double residual) const {
    Ray ray;
    ray.wavelength_um = wave.value_um;
    ray.nominal_weight = std::max(0.0, pupil.weight);
    ray.intensity = ray.nominal_weight;
    ray.aim_converged = false;
    ray.aim_residual = residual;
    ray.aim_target_radius =
        tracer_.stop_target_radius(wave.value_um, RayAimingMode::AdaptivePilotGrid);
    const double scale = std::max(1.0e-9, std::abs(ray.aim_target_radius));
    mark_ray_failure(
        ray,
        RayFailureCode::AimNotConverged,
        static_cast<int>(tracer_.stop_surface_index()),
        std::isfinite(residual) ? residual / scale : 1.0,
        true);
    return ray;
}

bool AimingSession::is_strictly_aimed(const Ray& ray, const Field& field) const {
    if (!ray.valid || !ray.aim_converged ||
        !std::isfinite(ray.aim_residual) ||
        ray.aim_residual > system_.ray_aiming.tolerance) {
        return false;
    }
    if (!std::isfinite(system_.object_distance())) {
        const Vec3 expected = field_direction_cosines(system_, field);
        const double err = norm(ray.d - expected);
        const double tol = std::max(
            1.0e-14,
            system_.ray_aiming.adaptive_pilot_grid.target_field_direction_tolerance);
        if (err > tol) {
            return false;
        }
    }
    return true;
}

AimingSession::FirstSurfaceSeed AimingSession::seed_from_ray(const Ray& ray) const {
    Vec3 point;
    if (!tracer_.first_surface_point(ray, &point)) {
        return {};
    }
    return {point.x, point.y, true};
}

bool AimingSession::remember_solution(
    std::size_t field_index,
    std::size_t wave_index,
    std::size_t pupil_index,
    const Ray& ray) {
    const FirstSurfaceSeed seed = seed_from_ray(ray);
    if (!seed.valid) {
        return false;
    }
    solved_seeds_[FieldWavePupilKey{field_index, wave_index, pupil_index}] = seed;
    return true;
}

bool AimingSession::sample_stop_from_ray(
    Ray ray,
    double* stop_x,
    double* stop_y,
    bool ignore_target_aperture,
    bool ignore_intermediate_apertures) const {
    if (!stop_x || !stop_y) {
        return false;
    }
    TracePoint hit;
    auto& diag = diagnostics_state();
    add_diag(diag.stop_residual_eval_calls);
    bool trace_ok = false;
    {
        ScopedDiagTimer eval_timer(diag.stop_residual_eval_ns);
        trace_ok = tracer_.trace_to_surface(
            ray,
            tracer_.stop_surface_index(),
            &hit,
            ignore_target_aperture,
            ignore_intermediate_apertures);
    }
    if (!trace_ok || !hit.valid) {
        return false;
    }
    add_diag(diag.stop_residual_eval_successes);
    *stop_x = hit.x;
    *stop_y = hit.y;
    return std::isfinite(*stop_x) && std::isfinite(*stop_y);
}

bool AimingSession::sample_stop_from_seed(
    const Field& field,
    const Wavelength& wave,
    const FirstSurfaceSeed& seed,
    double* stop_x,
    double* stop_y,
    Ray* traced_ray,
    bool ignore_target_aperture,
    bool ignore_intermediate_apertures) const {
    if (!seed.valid ||
        (first_surface_hard_radius(wave.value_um) > 0.0 &&
         aperture_clip_exceeded(
             std::hypot(seed.x, seed.y),
             first_surface_hard_radius(wave.value_um)))) {
        return false;
    }
    Ray pilot = tracer_.generate_ray_through_first_surface_point(
        field,
        wave.value_um,
        seed.x,
        seed.y,
        1.0);
    if (traced_ray) {
        *traced_ray = pilot;
    }
    return sample_stop_from_ray(
        pilot,
        stop_x,
        stop_y,
        ignore_target_aperture,
        ignore_intermediate_apertures);
}

double AimingSession::center_meridional_radius(const Wavelength& wave) {
    const auto existing = center_meridional_radius_cache_.find(wave.value_um);
    if (existing != center_meridional_radius_cache_.end()) {
        return existing->second;
    }
    double radius = 0.0;
    if (!system_.fields.empty()) {
        const auto center_it = std::min_element(
            system_.fields.begin(),
            system_.fields.end(),
            [](const Field& a, const Field& b) {
                return std::hypot(a.hx, a.hy) < std::hypot(b.hx, b.hy);
            });
        const Field& center_field = *center_it;
        const Ray plus = tracer_.generate_ray_with_mode(
            center_field,
            PupilPoint{0.0, 1.0, 1.0},
            wave.value_um,
            RayAimingMode::Paraxial);
        const Ray minus = tracer_.generate_ray_with_mode(
            center_field,
            PupilPoint{0.0, -1.0, 1.0},
            wave.value_um,
            RayAimingMode::Paraxial);
        const FirstSurfaceSeed plus_seed = seed_from_ray(plus);
        const FirstSurfaceSeed minus_seed = seed_from_ray(minus);
        if (plus_seed.valid && minus_seed.valid) {
            radius = 0.5 * std::hypot(
                plus_seed.x - minus_seed.x,
                plus_seed.y - minus_seed.y);
            if (!std::isfinite(radius)) {
                radius = 0.0;
            }
        }
    }
    center_meridional_radius_cache_[wave.value_um] = radius;
    return radius;
}

double AimingSession::center_iterative_edge_radius(const Wavelength& wave) {
    const auto existing = center_iterative_edge_radius_cache_.find(wave.value_um);
    if (existing != center_iterative_edge_radius_cache_.end()) {
        return existing->second;
    }
    double radius = 0.0;
    if (!system_.fields.empty()) {
        auto field_center_score = [](const Field& candidate) {
            double score = std::hypot(candidate.hx, candidate.hy);
            if (!std::isfinite(score)) {
                score = std::numeric_limits<double>::infinity();
            }
            return score;
        };
        auto center_it = system_.fields.begin();
        auto best_score = field_center_score(*center_it);
        for (auto it = std::next(system_.fields.begin()); it != system_.fields.end(); ++it) {
            const auto score = field_center_score(*it);
            if (score < best_score) {
                best_score = score;
                center_it = it;
            }
        }
        const Field& center_field = *center_it;
        std::vector<double> candidates;
        auto add_axis_radius = [&](const PupilPoint& plus_pupil,
                                   const PupilPoint& minus_pupil) {
            const Ray plus = tracer_.generate_ray_with_mode(
                center_field,
                plus_pupil,
                wave.value_um,
                RayAimingMode::Iterative);
            const Ray minus = tracer_.generate_ray_with_mode(
                center_field,
                minus_pupil,
                wave.value_um,
                RayAimingMode::Iterative);
            if (!plus.valid || !minus.valid ||
                !plus.aim_converged || !minus.aim_converged) {
                return;
            }
            const FirstSurfaceSeed plus_seed = seed_from_ray(plus);
            const FirstSurfaceSeed minus_seed = seed_from_ray(minus);
            if (!plus_seed.valid || !minus_seed.valid) {
                return;
            }
            const double axis_radius = 0.5 * std::hypot(
                plus_seed.x - minus_seed.x,
                plus_seed.y - minus_seed.y);
            if (std::isfinite(axis_radius) && axis_radius > 1.0e-9) {
                candidates.push_back(axis_radius);
            }
        };
        add_axis_radius(PupilPoint{1.0, 0.0, 1.0}, PupilPoint{-1.0, 0.0, 1.0});
        add_axis_radius(PupilPoint{0.0, 1.0, 1.0}, PupilPoint{0.0, -1.0, 1.0});
        if (!candidates.empty()) {
            radius = *std::min_element(candidates.begin(), candidates.end());
        }
    }
    center_iterative_edge_radius_cache_[wave.value_um] = radius;
    return radius;
}

} // namespace optiland
