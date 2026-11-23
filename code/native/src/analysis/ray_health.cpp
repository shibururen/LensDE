/*
 * Classifies trace failures and measures ray-path, stop and image-space health.
 * Spot/wavefront statistics and merit trace penalties consume these shared checks.
 * Expected aperture sampling rejection stays distinct from aiming, surface-miss and
 * backward failures so coverage and guard penalties retain their physical meaning.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
int axis_sign(double value, double eps) {
    if (!std::isfinite(value) || std::abs(value) <= eps) {
        return 0;
    }
    return value < 0.0 ? -1 : 1;
}
}  // namespace analysis_detail

namespace analysis_detail {
double trace_axis_value(const TracePoint& point, double ux, double uy) {
    return point.x * ux + point.y * uy;
}
}  // namespace analysis_detail

namespace analysis_detail {
AxisCrossResult axis_sign_flip_count_and_first_z(
    const std::vector<TracePoint>& history,
    double ux,
    double uy,
    double eps) {
    AxisCrossResult result;
    int last_sign = 0;
    double last_axis = 0.0;
    double last_z = 0.0;
    bool have_last = false;

    for (const auto& point : history) {
        if (!point.valid ||
            !std::isfinite(point.x) ||
            !std::isfinite(point.y) ||
            !std::isfinite(point.z)) {
            continue;
        }
        const double axis = trace_axis_value(point, ux, uy);
        const int sign = axis_sign(axis, eps);
        if (sign == 0) {
            continue;
        }
        if (last_sign != 0 && sign != last_sign) {
            ++result.count;
            if (!result.has_cross) {
                const double denom = std::abs(last_axis) + std::abs(axis);
                const double fraction = denom > 0.0 ? std::abs(last_axis) / denom : 0.5;
                result.first_z = last_z + fraction * (point.z - last_z);
                result.has_cross = std::isfinite(result.first_z);
            }
        }
        last_sign = sign;
        last_axis = axis;
        last_z = point.z;
        have_last = true;
    }

    if (!have_last) {
        result.count = 0;
        result.has_cross = false;
        result.first_z = std::numeric_limits<double>::quiet_NaN();
    }
    return result;
}
}  // namespace analysis_detail

namespace analysis_detail {
void apply_center_field_structure_metrics(
    const std::vector<StructuralRaySample>& samples,
    const MeritConfig& config,
    double image_z,
    double last_optical_z,
    SpotStats& stats) {
    if (samples.empty()) {
        return;
    }

    const double eps = std::max(0.0, config.axis_crossing_epsilon);
    double min_abs_py = std::numeric_limits<double>::infinity();
    for (const auto& sample : samples) {
        if (!sample.usable) {
            continue;
        }
        min_abs_py = std::min(min_abs_py, std::abs(sample.pupil.py));
    }
    if (!std::isfinite(min_abs_py)) {
        return;
    }
    const double meridional_tol = min_abs_py + 1.0e-8;
    double max_abs_px = 0.0;
    for (const auto& sample : samples) {
        if (!sample.usable || std::abs(sample.pupil.py) > meridional_tol) {
            continue;
        }
        max_abs_px = std::max(max_abs_px, std::abs(sample.pupil.px));
    }
    if (max_abs_px <= 1.0e-12) {
        return;
    }

    const double edge_ratio = std::clamp(config.center_early_focus_edge_ratio, 0.0, 1.0);
    const double edge_threshold = edge_ratio * max_abs_px;
    const double guard = std::max(0.0, config.center_early_focus_guard_mm);
    const int allow_count = std::max(0, config.axis_cross_allow_count);
    bool monitored = false;

    for (const auto& sample : samples) {
        if (!sample.usable ||
            std::abs(sample.pupil.py) > meridional_tol ||
            std::abs(sample.pupil.px) + 1.0e-12 < edge_threshold) {
            continue;
        }
        monitored = true;
        const AxisCrossResult cross = axis_sign_flip_count_and_first_z(sample.history, 1.0, 0.0, eps);
        stats.axis_cross_count = std::max(stats.axis_cross_count, cross.count);
        const bool early_inside_lens_train =
            cross.has_cross &&
            std::isfinite(last_optical_z) &&
            cross.first_z < last_optical_z - guard;
        const bool early_without_lens_train_reference =
            cross.has_cross &&
            !std::isfinite(last_optical_z) &&
            std::isfinite(image_z) &&
            cross.first_z < image_z - guard;
        if (early_inside_lens_train || early_without_lens_train_reference) {
            stats.center_early_focus = 1;
        }
    }

    if (!monitored) {
        return;
    }
    stats.axis_cross_excess = std::max(0, stats.axis_cross_count - allow_count);
    stats.structural_failure =
        (stats.axis_cross_excess > 0 || stats.center_early_focus != 0) ? 1 : stats.structural_failure;
}
}  // namespace analysis_detail

namespace analysis_detail {
void apply_off_axis_chief_structure_metrics(
    const Field& field,
    const Ray& initial_chief,
    const std::vector<TracePoint>& history,
    std::size_t first_surface,
    const MeritConfig& config,
    SpotStats& stats) {
    const double radius = std::hypot(field.hx, field.hy);
    if (radius <= 1.0e-9 || history.empty()) {
        return;
    }
    const double ux = field.hx / radius;
    const double uy = field.hy / radius;
    const double eps = std::max(0.0, config.axis_crossing_epsilon);
    const int allow_count = std::max(0, config.axis_cross_allow_count);

    const AxisCrossResult cross = axis_sign_flip_count_and_first_z(history, ux, uy, eps);
    stats.axis_cross_count = std::max(stats.axis_cross_count, cross.count);
    stats.axis_cross_excess = std::max(stats.axis_cross_excess, cross.count - allow_count);

    if (first_surface < history.size() && history[first_surface].valid) {
        const double first_axis = trace_axis_value(history[first_surface], ux, uy);
        const double input_axis = initial_chief.d.x * ux + initial_chief.d.y * uy;
        const int height_sign = axis_sign(first_axis, eps);
        const int direction_sign = axis_sign(input_axis, eps);
        if (height_sign != 0 && direction_sign != 0 && height_sign == direction_sign) {
            stats.chief_entry_side_failed = 1;
        }
    }

    // A shifted vignetted pupil can place its chief on the incoming field's
    // side of the first surface. Keep that observation, but it is not a hard
    // structural failure under the vignetting-optimization contract.
    const bool reject_entry_side = !config.vignetting_optimization_enabled();
    stats.structural_failure =
        (stats.axis_cross_excess > 0 ||
         (reject_entry_side && stats.chief_entry_side_failed != 0))
            ? 1 : stats.structural_failure;
}
}  // namespace analysis_detail

namespace analysis_detail {
double ray_nominal_weight(const Ray& ray) {
    if (std::isfinite(ray.nominal_weight) && ray.nominal_weight > 0.0) {
        return ray.nominal_weight;
    }
    if (std::isfinite(ray.intensity) && ray.intensity > 0.0) {
        return ray.intensity;
    }
    return 0.0;
}
}  // namespace analysis_detail

namespace analysis_detail {
double ray_trace_progress_fraction(const Ray& ray, std::size_t surface_count) {
    if (ray.valid &&
        std::isfinite(ray.p.x) &&
        std::isfinite(ray.p.y) &&
        std::isfinite(ray.p.z)) {
        return 1.0;
    }

    double progress = 0.0;
    if (ray.failure_surface >= 0 && surface_count > 1) {
        progress = std::clamp(
            static_cast<double>(ray.failure_surface + 1) / static_cast<double>(surface_count),
            0.0,
            1.0);
    }
    if (ray.failure_code == RayFailureCode::AimNotConverged &&
        std::isfinite(ray.aim_residual)) {
        const double scale = std::max(1.0e-9, std::abs(ray.aim_target_radius));
        const double normalized_residual = std::max(0.0, ray.aim_residual / scale);
        const double residual_score = 1.0 / (1.0 + normalized_residual);
        progress = std::max(progress, 0.5 * residual_score);
    }
    return std::clamp(progress, 0.0, 1.0);
}
}  // namespace analysis_detail

namespace analysis_detail {
double ray_failure_penalty(const Ray& ray, const MeritConfig& config) {
    const double weight = ray_nominal_weight(ray);
    const double severity = std::isfinite(ray.failure_severity)
                                ? std::max(0.0, ray.failure_severity)
                                : 1.0;
    const double curved = std::log1p(severity * severity);
    switch (ray.failure_code) {
        case RayFailureCode::AimNotConverged:
            return weight * std::abs(config.aim_failure_weight) * curved;
        case RayFailureCode::ApertureClip:
            return weight * std::abs(config.aperture_failure_weight) * curved;
        case RayFailureCode::TotalInternalReflection:
            return weight * std::abs(config.tir_failure_weight) * std::log1p(severity + 1.0);
        case RayFailureCode::SurfaceMiss:
        case RayFailureCode::SagInvalid:
            return weight * std::abs(config.surface_miss_weight) * std::log1p(severity + 1.0);
        case RayFailureCode::BackwardPropagation:
            return weight * std::abs(config.backward_failure_weight) * curved;
        case RayFailureCode::NonFinite:
        case RayFailureCode::GeometryInvalid:
            return weight * std::abs(config.trace_failure_weight) * std::log1p(severity + 1.0);
        case RayFailureCode::None:
        default:
            return weight * std::abs(config.trace_failure_weight);
    }
}
}  // namespace analysis_detail

namespace analysis_detail {
double ray_backward_propagation_penalty(const Ray& ray, const MeritConfig& config) {
    if (!ray.backward_propagation) {
        return 0.0;
    }
    const double weight = ray_nominal_weight(ray);
    const double severity = std::isfinite(ray.backward_severity)
                                ? std::max(0.0, ray.backward_severity)
                                : 1.0;
    return weight * std::abs(config.backward_failure_weight) * std::log1p(severity * severity);
}
}  // namespace analysis_detail

namespace analysis_detail {
bool ray_aim_is_usable_for_spot(const Ray& ray, const MeritConfig& config) {
    if (ray.aim_converged) {
        return true;
    }
    return config.allow_soft_aim_rays_for_spot &&
           std::isfinite(ray.aim_residual) &&
           ray.aim_residual <= config.soft_aim_residual_for_spot;
}
}  // namespace analysis_detail

namespace analysis_detail {
bool normal_secondary_aperture_blockage(const Ray& ray) {
    if (!ray.aperture_blocked || !ray.aim_converged ||
        ray.aperture_blocked_surface < 0) {
        return false;
    }
    // During vignetting optimization a secondary wavelength keeps tracing
    // after its first physical aperture hit so an inward replacement can be
    // predicted without another confirmation trace.  Once that hit occurred,
    // a later surface miss/backward event is physically unreachable and must
    // not turn the already-blocked sample back into a trace failure.
    if (!ray.valid &&
        (ray.failure_surface < 0 ||
         ray.failure_surface <= ray.aperture_blocked_surface)) {
        return false;
    }
    if (ray.backward_propagation &&
        (ray.backward_surface < 0 ||
         ray.backward_surface <= ray.aperture_blocked_surface)) {
        return false;
    }
    return true;
}
}  // namespace analysis_detail

namespace analysis_detail {
bool ray_is_usable_for_spot(const Ray& ray, const MeritConfig& config) {
    return ray.valid && !ray.aperture_blocked &&
           ray.intensity > 0.0 &&
           std::isfinite(ray.intensity) &&
           std::isfinite(ray.p.x) &&
           std::isfinite(ray.p.y) &&
           ray_aim_is_usable_for_spot(ray, config);
}
}  // namespace analysis_detail

namespace analysis_detail {
bool calibrated_paraxial_stop_sampling_rejection(
    const Ray& ray,
    RayAimingMode aiming_mode,
    int stop_surface_index) {
    if (normal_secondary_aperture_blockage(ray)) return true;
    return aiming_mode == RayAimingMode::CalibratedParaxial &&
           stop_surface_index >= 0 &&
           ray.aim_converged &&
           !ray.backward_propagation &&
           ray.failure_code == RayFailureCode::ApertureClip &&
           ray.failure_surface == stop_surface_index &&
           ray.stop_reached;
}
}  // namespace analysis_detail

namespace analysis_detail {
bool guard_ray_reached_inside_physical_stop(
    const LensSystem& system,
    const SequentialRayTracer& tracer,
    const Ray& ray) {
    if ((normal_secondary_aperture_blockage(ray)) ||
        !ray.stop_reached ||
        !std::isfinite(ray.stop_x) ||
        !std::isfinite(ray.stop_y)) {
        return false;
    }
    const std::size_t stop = tracer.stop_surface_index();
    if (stop >= system.surfaces.size() || !system.surfaces[stop].is_stop) {
        return false;
    }
    const Surface& stop_surface = system.surfaces[stop];
    double stop_radius = 0.0;
    if (stop_surface.clear_aperture_fixed ||
        system.aperture.type == ApertureType::FloatByStopSize) {
        stop_radius = stop_surface.aperture_radius > 0.0
                          ? stop_surface.aperture_radius
                          : stop_surface.semi_aperture;
    } else {
        stop_radius = tracer.aperture_stop_radius(ray.wavelength_um);
    }
    if (!std::isfinite(stop_radius) || stop_radius <= 0.0) {
        return false;
    }
    // Match the tracer's aperture-clip tolerance exactly.  A ray outside this
    // envelope is an entrance-pupil mapping miss, not evidence that a physical
    // in-stop marginal ray fails downstream.
    return std::hypot(ray.stop_x, ray.stop_y) <= 1.01 * stop_radius;
}
}  // namespace analysis_detail

namespace analysis_detail {
double backward_surface_order_edge_deficit(const Ray& ray, const MeritConfig& config) {
    const double air = ray.min_backward_air_gap < -1.0e-6
                           ? std::max(0.0, config.air_edge_thickness_lower) -
                                 ray.min_backward_air_gap
                           : 0.0;
    const double glass = ray.min_backward_glass_gap < -1.0e-6
                             ? std::max(0.0, config.glass_edge_thickness_lower) -
                                   ray.min_backward_glass_gap
                             : 0.0;
    return std::max(air, glass);
}
}  // namespace analysis_detail

namespace analysis_detail {
double guard_surface_order_edge_deficit(
    const LensSystem& system,
    const Ray& ray,
    const MeritConfig& config) {
    const double traced_deficit = backward_surface_order_edge_deficit(ray, config);
    if (ray.failure_code != RayFailureCode::SurfaceMiss ||
        ray.failure_surface < 0 ||
        !std::isfinite(ray.p.x) ||
        !std::isfinite(ray.p.y) ||
        !std::isfinite(ray.p.z)) {
        return traced_deficit;
    }
    const std::size_t surface_index = static_cast<std::size_t>(ray.failure_surface);
    const std::size_t first = system.first_optical_surface();
    if (surface_index <= first || surface_index >= system.surfaces.size()) {
        return traced_deficit;
    }
    const Surface& previous = system.surfaces[surface_index - 1];
    const Surface& missed = system.surfaces[surface_index];
    if (previous.kind == SurfaceKind::Object ||
        missed.kind == SurfaceKind::Object ||
        missed.kind == SurfaceKind::Image ||
        !std::isfinite(previous.thickness)) {
        return traced_deficit;
    }

    const double missed_sag = surface_sag(missed, ray.p.x, ray.p.y);
    if (!std::isfinite(missed_sag)) {
        return traced_deficit;
    }
    const double missed_z = surface_vertex_z(system, surface_index) + missed_sag;
    const double virtual_edge = missed_z - ray.p.z;
    // Only classify a miss as a hidden edge overlap when the next surface is
    // already behind the last valid intercept.  Other SurfaceMiss causes keep
    // their ordinary trace-failure residual.
    // The failed guard ray and the independently evaluated missed-surface sag
    // can differ by sub-nanometre root-refinement noise.  Do not turn that
    // numerical residue into a full glass/air edge-thickness deficit.  One
    // nanometre is still three orders of magnitude below the 0.001 mm
    // manufacturing-constraint tolerance used by this merit contract.
    constexpr double kSurfaceOrderOverlapToleranceMm = 1.0e-6;
    if (!std::isfinite(virtual_edge) ||
        virtual_edge >= -kSurfaceOrderOverlapToleranceMm) {
        return traced_deficit;
    }

    const double required_edge = interval_is_glass(previous)
                                     ? std::max(0.0, config.glass_edge_thickness_lower)
                                     : std::max(0.0, config.air_edge_thickness_lower);
    return std::max(traced_deficit, required_edge - virtual_edge);
}
}  // namespace analysis_detail

namespace analysis_detail {
bool field_is_off_axis(const Field& field) {
    return std::hypot(field.hx, field.hy) > 1.0e-9;
}
}  // namespace analysis_detail

namespace analysis_detail {
bool ray_is_usable_for_wavefront(const Ray& ray, const MeritConfig& config) {
    return ray_is_usable_for_spot(ray, config) &&
           std::isfinite(ray.p.z) &&
           std::isfinite(ray.d.x) &&
           std::isfinite(ray.d.y) &&
           std::isfinite(ray.d.z) &&
           std::isfinite(ray.opd);
}
}  // namespace analysis_detail

namespace analysis_detail {
double image_space_refractive_index(const LensSystem& system, double wavelength_um) {
    const std::size_t first = system.first_optical_surface();
    if (system.surfaces.empty() || first >= system.surfaces.size()) {
        return 1.0;
    }
    const std::size_t last = system.surfaces.size() - 1;
    if (system.surfaces[last].kind == SurfaceKind::Image && last > first) {
        const double n = system.surfaces[last - 1].material_after.refractive_index(wavelength_um);
        return std::isfinite(n) && std::abs(n) > kEps ? n : 1.0;
    }
    const double n = system.surfaces[last].material_after.refractive_index(wavelength_um);
    return std::isfinite(n) && std::abs(n) > kEps ? n : 1.0;
}
}  // namespace analysis_detail

namespace analysis_detail {
double object_space_refractive_index(const LensSystem& system, double wavelength_um) {
    if (system.has_object_surface()) {
        const double n = system.surfaces.front().material_after.refractive_index(wavelength_um);
        if (std::isfinite(n) && std::abs(n) > kEps) {
            return n;
        }
    }
    return 1.0;
}
}  // namespace analysis_detail

}  // namespace optiland
