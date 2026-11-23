/*
 * Augments spot statistics with missing edge and selective tail guard rays.
 * Called by polychromatic spot evaluation after the RMS centroid is available.
 * Physical-stop eligibility, dedicated guard pupil indices and soft paraxial failures
 * must stay separate from the ordinary RMS sample set and its intensity weights.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

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
    SpotStats& stats) {
    if (!std::isfinite(stats.centroid_x) ||
        !std::isfinite(stats.centroid_y) ||
        stats.valid_rays == 0) {
        return;
    }
    double max_radius = std::isfinite(stats.single_ray_max_radius)
                            ? stats.single_ray_max_radius
                            : stats.max_radius;
    if (!std::isfinite(max_radius)) {
        max_radius = 0.0;
    }
    const auto edge_pupils = missing_single_ray_guard_edge_pupils(rms_pupils);
    const auto extra_edge_pupils =
        uses_selective_spot_tail_edge_sampling(config, field)
            ? missing_pupil_points(
                  single_ray_spot_tail_extra_edge_pupils(),
                  rms_pupils)
            : std::vector<PupilPoint>{};
    constexpr std::size_t kGuardPupilIndexOffset = 1000000;
    // Direct rays retain their historical best-effort behavior.  Plain and
    // calibrated paraxial use one shared soft-failure contract: a failed exact
    // edge ray is penalized only when its actual stop intercept is inside the
    // physical stop.  This rejects false guard failures caused by a rough
    // entrance-pupil mapping that launches the ray outside the real stop.
    const bool soft_paraxial_edge_guard =
        system.ray_aiming.mode == RayAimingMode::Paraxial ||
        system.ray_aiming.mode == RayAimingMode::CalibratedParaxial;
    const bool strict_edge_guard =
        system.ray_aiming.mode != RayAimingMode::Direct &&
        !soft_paraxial_edge_guard;
    stats.single_ray_spot_guard_failure_strengths.assign(
        stats.single_ray_spot_guard_residuals.size() / 2,
        0.0);
    bool edge_guard_failed = false;

    // Some pupil distributions already contain one or more of the four exact
    // edge pupils.  Apply the same stop-intercept gate to those base samples;
    // otherwise their NaN residuals would bypass this policy and fall through
    // to the DLS generic invalid-residual penalty.
    std::size_t ray_index = 0;
    for (const auto& wave : waves) {
        if (wave.weight <= 0.0) {
            continue;
        }
        for (std::size_t pi = 0; pi < rms_pupils.size(); ++pi, ++ray_index) {
            if (!soft_paraxial_edge_guard ||
                ray_index >= rms_rays.size() ||
                ray_index >= stats.single_ray_spot_guard_failure_strengths.size() ||
                (!is_single_ray_guard_edge_pupil(rms_pupils[pi]) &&
                 !is_selective_spot_tail_edge_pupil(
                     config, field, rms_pupils[pi]))) {
                continue;
            }
            const Ray& ray = rms_rays[ray_index];
            if (ray_is_usable_for_spot(ray, config)) {
                continue;
            }
            const bool penalize_soft_failure =
                guard_ray_reached_inside_physical_stop(system, tracer, ray);
            const std::size_t residual_index = 2 * ray_index;
            if (residual_index + 1 >=
                stats.single_ray_spot_guard_residuals.size()) {
                continue;
            }
            if (penalize_soft_failure) {
                stats.single_ray_spot_guard_residuals[residual_index] =
                    std::numeric_limits<double>::quiet_NaN();
                stats.single_ray_spot_guard_residuals[residual_index + 1] =
                    std::numeric_limits<double>::quiet_NaN();
            } else {
                stats.single_ray_spot_guard_residuals[residual_index] = 0.0;
                stats.single_ray_spot_guard_residuals[residual_index + 1] = 0.0;
            }
            if (penalize_soft_failure) {
                stats.single_ray_guard_surface_order_edge_deficit = std::max(
                    stats.single_ray_guard_surface_order_edge_deficit,
                    guard_surface_order_edge_deficit(system, ray, config));
                const double progress =
                    ray_trace_progress_fraction(ray, system.surfaces.size());
                const double failure_strength =
                    1.0 + std::max(0.0, 1.0 - progress);
                stats.single_ray_spot_guard_failure_strengths[ray_index] =
                    failure_strength;
                stats.single_ray_spot_guard_failure_squared +=
                    failure_strength * failure_strength;
                ++stats.single_ray_spot_guard_failed_rays;
            }
        }
    }

    for (std::size_t wi = 0; wi < waves.size(); ++wi) {
        const auto& wave = waves[wi];
        if (!std::isfinite(wave.value_um) ||
            !std::isfinite(wave.weight) ||
            wave.weight <= 0.0) {
            continue;
        }
        for (std::size_t ei = 0; ei < edge_pupils.size(); ++ei) {
            Ray ray = ray_aiming_uses_session(system.ray_aiming.mode)
                          ? aiming.generate_image_guard(
                                field_index,
                                wi,
                                kGuardPupilIndexOffset + ei,
                                field,
                                wave,
                                edge_pupils[ei])
                          : tracer.generate_ray(field, edge_pupils[ei], wave.value_um);
            tracer.trace_ray(ray, nullptr);
            ray.intensity *= wave.weight;
            ray.nominal_weight *= wave.weight;
            if (normal_secondary_aperture_blockage(ray)) {
                stats.single_ray_spot_guard_residuals.push_back(0.0);
                stats.single_ray_spot_guard_residuals.push_back(0.0);
                stats.single_ray_spot_guard_failure_strengths.push_back(0.0);
                continue;
            }
            if (!ray_is_usable_for_spot(ray, config)) {
                const bool guard_inside_physical_stop =
                    guard_ray_reached_inside_physical_stop(system, tracer, ray);
                const bool penalize_soft_failure =
                    soft_paraxial_edge_guard &&
                    guard_inside_physical_stop;
                if (guard_inside_physical_stop) {
                    stats.single_ray_guard_surface_order_edge_deficit = std::max(
                        stats.single_ray_guard_surface_order_edge_deficit,
                        guard_surface_order_edge_deficit(system, ray, config));
                }
                if (strict_edge_guard || penalize_soft_failure) {
                    edge_guard_failed = true;
                    stats.single_ray_spot_guard_residuals.push_back(
                        std::numeric_limits<double>::quiet_NaN());
                    stats.single_ray_spot_guard_residuals.push_back(
                        std::numeric_limits<double>::quiet_NaN());
                } else {
                    stats.single_ray_spot_guard_residuals.push_back(0.0);
                    stats.single_ray_spot_guard_residuals.push_back(0.0);
                }
                double failure_strength = 0.0;
                if (penalize_soft_failure) {
                    const double progress =
                        ray_trace_progress_fraction(ray, system.surfaces.size());
                    failure_strength = 1.0 + std::max(0.0, 1.0 - progress);
                    stats.single_ray_spot_guard_failure_squared +=
                        failure_strength * failure_strength;
                    ++stats.single_ray_spot_guard_failed_rays;
                }
                stats.single_ray_spot_guard_failure_strengths.push_back(
                    failure_strength);
                continue;
            }
            stats.single_ray_guard_surface_order_edge_deficit = std::max(
                stats.single_ray_guard_surface_order_edge_deficit,
                backward_surface_order_edge_deficit(ray, config));
            const double dx = ray.p.x - stats.centroid_x;
            const double dy = ray.p.y - stats.centroid_y;
            const double radius = std::sqrt(dx * dx + dy * dy);
            if (std::isfinite(radius)) {
                max_radius = std::max(max_radius, radius);
                stats.single_ray_spot_guard_residuals.push_back(dx);
                stats.single_ray_spot_guard_residuals.push_back(dy);
                stats.single_ray_spot_guard_failure_strengths.push_back(0.0);
            } else {
                const bool penalize_soft_failure =
                    soft_paraxial_edge_guard &&
                    guard_ray_reached_inside_physical_stop(system, tracer, ray);
                if (strict_edge_guard || penalize_soft_failure) {
                    edge_guard_failed = true;
                    stats.single_ray_spot_guard_residuals.push_back(
                        std::numeric_limits<double>::quiet_NaN());
                    stats.single_ray_spot_guard_residuals.push_back(
                        std::numeric_limits<double>::quiet_NaN());
                } else {
                    stats.single_ray_spot_guard_residuals.push_back(0.0);
                    stats.single_ray_spot_guard_residuals.push_back(0.0);
                }
                const double failure_strength =
                    penalize_soft_failure ? 1.0 : 0.0;
                if (failure_strength > 0.0) {
                    stats.single_ray_spot_guard_failure_squared +=
                        failure_strength * failure_strength;
                    ++stats.single_ray_spot_guard_failed_rays;
                }
                stats.single_ray_spot_guard_failure_strengths.push_back(
                    failure_strength);
            }
        }
    }

    // The selective tail probes are deliberately excluded from the RMS
    // quadrature.  They only tighten the absolute maximum-radius proxy in the
    // outer field band.  Trace them through the same pupil-symmetry machinery
    // as the RMS set, so an 8-direction logical probe costs four physical rays
    // for a meridional off-axis field and one for a centered axial field.
    constexpr std::size_t kTailExtraPupilIndexOffset = 2000000;
    if (!extra_edge_pupils.empty()) {
        for (std::size_t wi = 0; wi < waves.size(); ++wi) {
            const auto& wave = waves[wi];
            if (!std::isfinite(wave.value_um) ||
                !std::isfinite(wave.weight) ||
                wave.weight <= 0.0) {
                continue;
            }
            SymmetricTraceBatch extra_batch = trace_pupil_batch(
                system,
                config,
                tracer,
                aiming,
                field_index,
                wi,
                field,
                wave,
                extra_edge_pupils,
                false,
                kTailExtraPupilIndexOffset,
                nullptr,
                true);
            for (Ray ray : extra_batch.rays) {
                ray.intensity *= wave.weight;
                ray.nominal_weight *= wave.weight;
                if (normal_secondary_aperture_blockage(ray)) {
                    stats.single_ray_spot_guard_residuals.push_back(0.0);
                    stats.single_ray_spot_guard_residuals.push_back(0.0);
                    stats.single_ray_spot_guard_failure_strengths.push_back(0.0);
                    continue;
                }
                if (!ray_is_usable_for_spot(ray, config)) {
                    const bool guard_inside_physical_stop =
                        guard_ray_reached_inside_physical_stop(
                            system, tracer, ray);
                    const bool penalize_soft_failure =
                        soft_paraxial_edge_guard &&
                        guard_inside_physical_stop;
                    if (guard_inside_physical_stop) {
                        stats.single_ray_guard_surface_order_edge_deficit =
                            std::max(
                                stats.single_ray_guard_surface_order_edge_deficit,
                                guard_surface_order_edge_deficit(
                                    system, ray, config));
                    }
                    if (strict_edge_guard || penalize_soft_failure) {
                        edge_guard_failed = true;
                        stats.single_ray_spot_guard_residuals.push_back(
                            std::numeric_limits<double>::quiet_NaN());
                        stats.single_ray_spot_guard_residuals.push_back(
                            std::numeric_limits<double>::quiet_NaN());
                    } else {
                        stats.single_ray_spot_guard_residuals.push_back(0.0);
                        stats.single_ray_spot_guard_residuals.push_back(0.0);
                    }
                    double failure_strength = 0.0;
                    if (penalize_soft_failure) {
                        const double progress = ray_trace_progress_fraction(
                            ray, system.surfaces.size());
                        failure_strength =
                            1.0 + std::max(0.0, 1.0 - progress);
                        stats.single_ray_spot_guard_failure_squared +=
                            failure_strength * failure_strength;
                        ++stats.single_ray_spot_guard_failed_rays;
                    }
                    stats.single_ray_spot_guard_failure_strengths.push_back(
                        failure_strength);
                    continue;
                }
                const double dx = ray.p.x - stats.centroid_x;
                stats.single_ray_guard_surface_order_edge_deficit = std::max(
                    stats.single_ray_guard_surface_order_edge_deficit,
                    backward_surface_order_edge_deficit(ray, config));
                const double dy = ray.p.y - stats.centroid_y;
                const double radius = std::hypot(dx, dy);
                if (std::isfinite(radius)) {
                    max_radius = std::max(max_radius, radius);
                    stats.single_ray_spot_guard_residuals.push_back(dx);
                    stats.single_ray_spot_guard_residuals.push_back(dy);
                    stats.single_ray_spot_guard_failure_strengths.push_back(0.0);
                } else {
                    const bool penalize_soft_failure =
                        soft_paraxial_edge_guard &&
                        guard_ray_reached_inside_physical_stop(
                            system, tracer, ray);
                    if (strict_edge_guard || penalize_soft_failure) {
                        edge_guard_failed = true;
                        stats.single_ray_spot_guard_residuals.push_back(
                            std::numeric_limits<double>::quiet_NaN());
                        stats.single_ray_spot_guard_residuals.push_back(
                            std::numeric_limits<double>::quiet_NaN());
                    } else {
                        stats.single_ray_spot_guard_residuals.push_back(0.0);
                        stats.single_ray_spot_guard_residuals.push_back(0.0);
                    }
                    const double failure_strength =
                        penalize_soft_failure ? 1.0 : 0.0;
                    if (failure_strength > 0.0) {
                        stats.single_ray_spot_guard_failure_squared +=
                            failure_strength * failure_strength;
                        ++stats.single_ray_spot_guard_failed_rays;
                    }
                    stats.single_ray_spot_guard_failure_strengths.push_back(
                        failure_strength);
                }
            }
        }
    }
    stats.single_ray_max_radius =
        edge_guard_failed && strict_edge_guard
            ? std::numeric_limits<double>::quiet_NaN()
            : max_radius;
}

}  // namespace optiland
