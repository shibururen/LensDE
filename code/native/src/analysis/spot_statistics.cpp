/*
 * Reduces traced rays into weighted centroids, RMS/maximum radii and coverage statistics.
 * Spot evaluation supplies ray arrays and the configured hard/soft failure policy.
 * Nominal sampling weights, optical intensity and expected aperture rejections have
 * different roles; keep their accounting and invalid-result behavior unchanged.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

SpotStats compute_spot_stats(const std::vector<Ray>& rays) {
    SpotStats stats;
    if (rays.empty()) {
        stats.rms_radius = std::numeric_limits<double>::quiet_NaN();
        stats.max_radius = std::numeric_limits<double>::quiet_NaN();
        return stats;
    }

    double sx = 0.0;
    double sy = 0.0;
    double sum_w = 0.0;
    for (const auto& ray : rays) {
        const double nominal = ray_nominal_weight(ray);
        if (normal_secondary_aperture_blockage(ray)) {
            ++stats.sampling_rejected_rays;
            stats.sampling_rejected_weight += nominal;
            continue;
        }
        stats.total_weight += nominal;
        ++stats.attempted_rays;
        if (!ray.valid || !std::isfinite(ray.p.x) || !std::isfinite(ray.p.y) ||
            !std::isfinite(ray.intensity) || ray.intensity < 0.0) {
            stats.rms_radius = std::numeric_limits<double>::quiet_NaN();
            stats.max_radius = std::numeric_limits<double>::quiet_NaN();
            stats.failed_weight += nominal;
            ++stats.failed_rays;
            stats.coverage = stats.total_weight > 0.0 ? stats.valid_weight / stats.total_weight : 0.0;
            return stats;
        }
        if (ray.intensity <= 0.0) {
            continue;
        }
        sx += ray.intensity * ray.p.x;
        sy += ray.intensity * ray.p.y;
        sum_w += ray.intensity;
        stats.valid_weight += nominal;
        ++stats.valid_rays;
    }

    if (sum_w <= 0.0 || stats.valid_rays == 0) {
        stats.rms_radius = std::numeric_limits<double>::quiet_NaN();
        stats.max_radius = std::numeric_limits<double>::quiet_NaN();
        stats.coverage = stats.total_weight > 0.0 ? stats.valid_weight / stats.total_weight : 0.0;
        return stats;
    }

    stats.centroid_x = sx / sum_w;
    stats.centroid_y = sy / sum_w;

    double sum_r2 = 0.0;
    stats.ray_spot_residuals.clear();
    stats.ray_spot_residuals.reserve(2 * rays.size());
    stats.single_ray_spot_guard_residuals.clear();
    stats.single_ray_spot_guard_residuals.reserve(2 * rays.size());
    for (const auto& ray : rays) {
        if (ray.intensity <= 0.0 || normal_secondary_aperture_blockage(ray)) {
            stats.ray_spot_residuals.push_back(0.0);
            stats.ray_spot_residuals.push_back(0.0);
            stats.single_ray_spot_guard_residuals.push_back(
                std::numeric_limits<double>::quiet_NaN());
            stats.single_ray_spot_guard_residuals.push_back(
                std::numeric_limits<double>::quiet_NaN());
            continue;
        }
        const double dx = ray.p.x - stats.centroid_x;
        const double dy = ray.p.y - stats.centroid_y;
        const double r = std::sqrt(dx * dx + dy * dy);
        sum_r2 += ray.intensity * r * r;
        const double residual_weight = std::sqrt(ray.intensity / sum_w);
        stats.ray_spot_residuals.push_back(residual_weight * dx);
        stats.ray_spot_residuals.push_back(residual_weight * dy);
        stats.single_ray_spot_guard_residuals.push_back(dx);
        stats.single_ray_spot_guard_residuals.push_back(dy);
        stats.max_radius = std::max(stats.max_radius, r);
    }
    stats.rms_radius = std::sqrt(sum_r2 / sum_w);
    stats.single_ray_max_radius = stats.max_radius;
    stats.coverage = stats.total_weight > 0.0 ? stats.valid_weight / stats.total_weight : 1.0;
    return stats;
}

SpotStats compute_spot_stats(
    const std::vector<Ray>& rays,
    const MeritConfig& config,
    std::size_t surface_count,
    RayAimingMode aiming_mode,
    int stop_surface_index) {
    if (!soft_trace_policy(config)) {
        std::vector<Ray> accepted_rays;
        accepted_rays.reserve(rays.size());
        std::size_t rejected_rays = 0;
        double rejected_weight = 0.0;
        for (const auto& ray : rays) {
            if (calibrated_paraxial_stop_sampling_rejection(
                    ray, aiming_mode, stop_surface_index)) {
                ++rejected_rays;
                rejected_weight += ray_nominal_weight(ray);
                continue;
            }
            accepted_rays.push_back(ray);
        }
        SpotStats stats = compute_spot_stats(accepted_rays);
        stats.sampling_rejected_rays = rejected_rays;
        stats.sampling_rejected_weight = rejected_weight;
        for (const auto& ray : accepted_rays) {
            stats.single_ray_guard_surface_order_edge_deficit = std::max(
                stats.single_ray_guard_surface_order_edge_deficit,
                backward_surface_order_edge_deficit(ray, config));
            if (!ray.backward_propagation) {
                continue;
            }
            const double weight = ray_nominal_weight(ray);
            stats.backward_weight += weight;
            ++stats.backward_rays;
            stats.backward_penalty += ray_backward_propagation_penalty(ray, config);
        }
        return stats;
    }

    SpotStats stats;
    if (rays.empty()) {
        stats.rms_radius = std::numeric_limits<double>::quiet_NaN();
        stats.max_radius = std::numeric_limits<double>::quiet_NaN();
        stats.coverage = 0.0;
        stats.trace_progress = 0.0;
        return stats;
    }

    double sx = 0.0;
    double sy = 0.0;
    double progress_sum = 0.0;
    for (const auto& ray : rays) {
        const double weight = ray_nominal_weight(ray);
        if (calibrated_paraxial_stop_sampling_rejection(
                ray, aiming_mode, stop_surface_index)) {
            stats.sampling_rejected_weight += weight;
            ++stats.sampling_rejected_rays;
            continue;
        }
        stats.total_weight += weight;
        progress_sum += weight * ray_trace_progress_fraction(ray, surface_count);
        ++stats.attempted_rays;
        stats.single_ray_guard_surface_order_edge_deficit = std::max(
            stats.single_ray_guard_surface_order_edge_deficit,
            backward_surface_order_edge_deficit(ray, config));
        if (!ray_aim_is_usable_for_spot(ray, config)) {
            stats.aim_penalty += ray_failure_penalty(ray, config);
        }
        if (ray.backward_propagation) {
            stats.backward_weight += weight;
            ++stats.backward_rays;
            stats.backward_penalty += ray_backward_propagation_penalty(ray, config);
        }
        if (!ray_is_usable_for_spot(ray, config)) {
            stats.failed_weight += weight;
            ++stats.failed_rays;
            stats.trace_penalty += ray_failure_penalty(ray, config);
            continue;
        }
        sx += weight * ray.p.x;
        sy += weight * ray.p.y;
        stats.valid_weight += weight;
        ++stats.valid_rays;
    }

    stats.coverage = stats.total_weight > 0.0
                         ? stats.valid_weight / stats.total_weight
                         : (stats.sampling_rejected_rays > 0 ? 1.0 : 0.0);
    stats.trace_progress = stats.total_weight > 0.0
                               ? progress_sum / stats.total_weight
                               : (stats.sampling_rejected_rays > 0 ? 1.0 : 0.0);
    if (stats.valid_weight <= 0.0 || stats.valid_rays == 0) {
        stats.rms_radius = std::numeric_limits<double>::quiet_NaN();
        stats.max_radius = std::numeric_limits<double>::quiet_NaN();
        return stats;
    }

    stats.centroid_x = sx / stats.valid_weight;
    stats.centroid_y = sy / stats.valid_weight;

    double sum_r2 = 0.0;
    stats.ray_spot_residuals.clear();
    stats.ray_spot_residuals.reserve(2 * rays.size());
    stats.single_ray_spot_guard_residuals.clear();
    stats.single_ray_spot_guard_residuals.reserve(2 * rays.size());
    for (const auto& ray : rays) {
        if (calibrated_paraxial_stop_sampling_rejection(
                ray, aiming_mode, stop_surface_index)) {
            stats.ray_spot_residuals.push_back(0.0);
            stats.ray_spot_residuals.push_back(0.0);
            const double rejected_guard = normal_secondary_aperture_blockage(ray)
                ? 0.0 : std::numeric_limits<double>::quiet_NaN();
            stats.single_ray_spot_guard_residuals.push_back(
                rejected_guard);
            stats.single_ray_spot_guard_residuals.push_back(
                rejected_guard);
            continue;
        }
        if (!ray_is_usable_for_spot(ray, config)) {
            stats.ray_spot_residuals.push_back(0.0);
            stats.ray_spot_residuals.push_back(0.0);
            stats.single_ray_spot_guard_residuals.push_back(
                std::numeric_limits<double>::quiet_NaN());
            stats.single_ray_spot_guard_residuals.push_back(
                std::numeric_limits<double>::quiet_NaN());
            continue;
        }
        const double weight = ray_nominal_weight(ray);
        const double dx = ray.p.x - stats.centroid_x;
        const double dy = ray.p.y - stats.centroid_y;
        const double r = std::sqrt(dx * dx + dy * dy);
        sum_r2 += weight * r * r;
        const double residual_weight = std::sqrt(weight / stats.valid_weight);
        stats.ray_spot_residuals.push_back(residual_weight * dx);
        stats.ray_spot_residuals.push_back(residual_weight * dy);
        stats.single_ray_spot_guard_residuals.push_back(dx);
        stats.single_ray_spot_guard_residuals.push_back(dy);
        stats.max_radius = std::max(stats.max_radius, r);
    }
    stats.rms_radius = std::sqrt(sum_r2 / stats.valid_weight);
    stats.single_ray_max_radius = stats.max_radius;
    return stats;
}

bool single_ray_spot_tail_enabled(const MeritConfig& config) {
    if (config.operands.empty()) {
        return scoring_weight(
                   config.unified_image_single_ray_spot_tail_weight) > 0.0;
    }
    return std::any_of(
        config.operands.begin(),
        config.operands.end(),
        [](const MeritOperand& operand) {
            return is_single_ray_spot_tail_operand_name(operand.name) &&
                   std::isfinite(operand.weight) &&
                   operand.weight > 0.0;
        });
}

bool minimum_relative_illumination_enabled(const MeritConfig& config) {
    return std::isfinite(config.minimum_relative_illumination) &&
           config.minimum_relative_illumination > 0.0 &&
           config.minimum_relative_illumination <= 1.0;
}

bool relative_illumination_proxy_requested(const MeritConfig& config) {
    if (minimum_relative_illumination_enabled(config)) {
        return true;
    }
    return std::any_of(
        config.operands.begin(),
        config.operands.end(),
        [](const MeritOperand& operand) {
            return is_relative_illumination_proxy_operand_name(operand.name) &&
                   std::isfinite(operand.weight) && operand.weight > 0.0;
        });
}

bool uses_selective_spot_tail_edge_sampling(
    const MeritConfig& config,
    const Field& field) {
    return single_ray_spot_tail_enabled(config) &&
           std::hypot(field.hx, field.hy) + 1.0e-12 >=
               config.single_ray_spot_tail_extra_edge_min_field_radius;
}

bool is_selective_spot_tail_edge_pupil(
    const MeritConfig& config,
    const Field& field,
    const PupilPoint& pupil) {
    if (!uses_selective_spot_tail_edge_sampling(config, field)) {
        return false;
    }
    static const auto pupils = single_ray_spot_tail_extra_edge_pupils();
    return std::any_of(
        pupils.begin(),
        pupils.end(),
        [&](const PupilPoint& candidate) {
            return same_pupil_point(pupil, candidate);
        });
}

}  // namespace optiland
