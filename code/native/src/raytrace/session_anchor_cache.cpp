// Materialize chief-anchor and pilot-cache entries needed by session ray generation.
// Generators reuse these per-session records instead of independently rebuilding equivalent anchors.
// Publish cache readiness only after its associated field, wavelength, and solve checks complete.

#include "aiming_geometry_inline.hpp"
#include "prepared_geometry_inline.hpp"
#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

AimingSession::ChiefAnchor AimingSession::ensure_chief_anchor(
    std::size_t field_index,
    std::size_t wave_index,
    const Field& field,
    const Wavelength& wave) {
    const FieldWaveKey key{field_index, wave_index};
    auto existing = chief_anchors_.find(key);
    if (existing != chief_anchors_.end()) {
        return existing->second;
    }

    ChiefAnchor anchor;
    const auto& cfg = system_.ray_aiming.adaptive_pilot_grid;
    const PupilPoint chief{0.0, 0.0, 1.0};
    if (cfg.use_chief_gate) {
        Ray chief_iter = tracer_.generate_ray_with_mode(field, chief, wave.value_um, RayAimingMode::Iterative);
        if (is_strictly_aimed(chief_iter, field)) {
            anchor.seed = seed_from_ray(chief_iter);
            anchor.ray = chief_iter;
            anchor.valid = anchor.seed.valid;
            if (anchor.valid) {
                chief_anchors_[key] = anchor;
                return anchor;
            }
        }
    }

    std::vector<FirstSurfaceSeed> seeds;
    if (cfg.use_previous_field_chief && field_index > 0) {
        auto it = chief_anchors_.find(FieldWaveKey{field_index - 1, wave_index});
        if (it != chief_anchors_.end() && it->second.valid) {
            append_seed(seeds, it->second.seed, wave.value_um);
        }
        if (cfg.use_previous_field_extrapolation && field_index > 1) {
            auto it0 = chief_anchors_.find(FieldWaveKey{field_index - 1, wave_index});
            auto it1 = chief_anchors_.find(FieldWaveKey{field_index - 2, wave_index});
            auto f = fields_seen_.find(field_index);
            auto f0 = fields_seen_.find(field_index - 1);
            auto f1 = fields_seen_.find(field_index - 2);
            if (it0 != chief_anchors_.end() && it1 != chief_anchors_.end() &&
                it0->second.valid && it1->second.valid &&
                f != fields_seen_.end() && f0 != fields_seen_.end() && f1 != fields_seen_.end()) {
                const double rho = std::hypot(f->second.hx, f->second.hy);
                const double rho0 = std::hypot(f0->second.hx, f0->second.hy);
                const double rho1 = std::hypot(f1->second.hx, f1->second.hy);
                double alpha = (rho - rho0) / std::max(1.0e-12, rho0 - rho1);
                alpha = std::clamp(alpha, 0.0, std::max(0.0, cfg.max_field_extrapolation_factor));
                append_seed(
                    seeds,
                    {it0->second.seed.x + alpha * (it0->second.seed.x - it1->second.seed.x),
                     it0->second.seed.y + alpha * (it0->second.seed.y - it1->second.seed.y),
                     true}, wave.value_um);
            }
        }
    }
    if (cfg.use_primary_wave_as_anchor && !system_.wavelengths.empty()) {
        std::size_t primary_index = 0;
        for (std::size_t i = 0; i < system_.wavelengths.size(); ++i) {
            if (system_.wavelengths[i].primary) {
                primary_index = i;
                break;
            }
        }
        if (primary_index != wave_index) {
            auto it = chief_anchors_.find(FieldWaveKey{field_index, primary_index});
            if (it != chief_anchors_.end() && it->second.valid) {
                append_seed(seeds, it->second.seed, wave.value_um);
            }
        }
    }
    if (wave_index > 0) {
        auto it = chief_anchors_.find(FieldWaveKey{field_index, wave_index - 1});
        if (it != chief_anchors_.end() && it->second.valid) {
            append_seed(seeds, it->second.seed, wave.value_um);
        }
    }
    append_seed(seeds, {0.0, 0.0, true}, wave.value_um);

    for (const auto& seed : seeds) {
        Ray solved = solve_from_seed(field, wave, chief, seed);
        if (is_strictly_aimed(solved, field)) {
            anchor.seed = seed_from_ray(solved);
            anchor.ray = solved;
            anchor.valid = anchor.seed.valid;
            if (anchor.valid) {
                chief_anchors_[key] = anchor;
                return anchor;
            }
        }
    }

    const double radius = first_surface_search_radius(wave.value_um);
    const double hard_radius = first_surface_hard_radius(wave.value_um);
    const int n = std::max(3, cfg.coarse_grid_n);
    double halfwidth = std::clamp(
        cfg.coarse_min_halfwidth_fraction * radius,
        1.0e-9,
        std::max(1.0e-9, cfg.coarse_max_halfwidth_fraction * radius));
    const double max_halfwidth = std::max(halfwidth, cfg.coarse_max_halfwidth_fraction * radius);
    const std::size_t stop_index = tracer_.stop_surface_index();
    const double rs = tracer_.stop_target_radius(wave.value_um, RayAimingMode::AdaptivePilotGrid);
    const double stop_scale = std::max(1.0e-9, std::abs(rs));
    const double target_x = field.fvcx * rs;
    const double target_y = field.fvcy * rs;
    auto accept_chief_by_stop = [&](const Ray& ray, double residual_fraction) {
        double sx = 0.0;
        double sy = 0.0;
        if (!sample_stop_from_ray(ray, &sx, &sy)) {
            return false;
        }
        const double residual = std::hypot(sx - target_x, sy - target_y);
        const double limit = std::max(system_.ray_aiming.tolerance, residual_fraction * stop_scale);
        if (!std::isfinite(residual) || residual > limit) {
            return false;
        }
        anchor.seed = seed_from_ray(ray);
        anchor.ray = ray;
        anchor.valid = anchor.seed.valid;
        if (anchor.valid) {
            chief_anchors_[key] = anchor;
            return true;
        }
        return false;
    };
    std::vector<FirstSurfaceSeed> centers = seeds;
    if (centers.empty()) {
        centers.push_back({0.0, 0.0, true});
    }
    std::vector<double> halfwidths;
    append_unique_positive(halfwidths, aperture_aware_halfwidth(halfwidth, 1.0e-9, stop_scale));
    for (int expansion = 0; expansion <= std::max(0, cfg.max_coarse_expansions); ++expansion) {
        append_unique_positive(halfwidths, halfwidth);
        if (halfwidth >= max_halfwidth * (1.0 - 1.0e-12)) {
            break;
        }
        halfwidth = std::min(max_halfwidth, halfwidth * 1.5);
    }
    for (const double search_halfwidth : halfwidths) {
        std::vector<std::pair<double, FirstSurfaceSeed>> ranked;
        for (const auto& center : centers) {
            if (!center.valid) {
                continue;
            }
            for (int iy = 0; iy < n; ++iy) {
                const double off_y = -search_halfwidth +
                                     (n == 1 ? 0.0 : 2.0 * search_halfwidth * iy / static_cast<double>(n - 1));
                for (int ix = 0; ix < n; ++ix) {
                    const double off_x = -search_halfwidth +
                                         (n == 1 ? 0.0 : 2.0 * search_halfwidth * ix / static_cast<double>(n - 1));
                    const double x = center.x + off_x;
                    const double y = center.y + off_y;
                    if (hard_radius > 0.0 &&
                        aperture_clip_exceeded(std::hypot(x, y), hard_radius)) {
                        continue;
                    }
                    Ray pilot = tracer_.generate_ray_through_first_surface_point(field, wave.value_um, x, y, 1.0);
                    TracePoint hit;
                    if (!tracer_.trace_to_surface(pilot, stop_index, &hit) || !hit.valid) {
                        continue;
                    }
                    const double err = (hit.x - target_x) * (hit.x - target_x) +
                                       (hit.y - target_y) * (hit.y - target_y);
                    ranked.push_back({err, {x, y, true}});
                }
            }
        }
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });
        const int tries = std::min<int>(std::max(3, cfg.nearest_k), static_cast<int>(ranked.size()));
        for (int i = 0; i < tries; ++i) {
            Ray solved = solve_from_seed(field, wave, chief, ranked[static_cast<std::size_t>(i)].second);
            if (is_strictly_aimed(solved, field)) {
                anchor.seed = seed_from_ray(solved);
                anchor.ray = solved;
                anchor.valid = anchor.seed.valid;
                if (anchor.valid) {
                    chief_anchors_[key] = anchor;
                    return anchor;
                }
            }
        }
    }

    Ray robust_chief = tracer_.generate_ray_with_mode(field, chief, wave.value_um, RayAimingMode::Robust);
    if (accept_chief_by_stop(robust_chief, 0.05)) {
        return anchor;
    }

    chief_anchors_[key] = anchor;
    return anchor;
}

AimingSession::PilotCache& AimingSession::ensure_pilot_cache(
    std::size_t field_index,
    std::size_t wave_index,
    const Field& field,
    const Wavelength& wave,
    const ChiefAnchor& anchor) {
    const FieldWaveKey key{field_index, wave_index};
    PilotCache& cache = pilot_caches_[key];
    if (cache.built) {
        return cache;
    }
    cache.built = true;
    cache.chief_seed = anchor.seed;
    if (!anchor.valid || !anchor.seed.valid) {
        return cache;
    }

    const auto& cfg = system_.ray_aiming.adaptive_pilot_grid;
    const double radius = first_surface_search_radius(wave.value_um);
    const double hard_radius = first_surface_hard_radius(wave.value_um);
    const int n = std::max(3, cfg.fine_grid_n);
    const double rs = tracer_.stop_target_radius(wave.value_um, RayAimingMode::AdaptivePilotGrid);
    const double stop_scale = std::max(1.0e-9, std::abs(rs));
    const double min_halfwidth = std::max(1.0e-9, cfg.fine_min_halfwidth_fraction * radius);
    double halfwidth = std::clamp(
        cfg.fine_default_halfwidth_fraction * radius,
        min_halfwidth,
        std::max(1.0e-9, cfg.fine_max_halfwidth_fraction * radius));
    const double max_halfwidth = std::max(halfwidth, cfg.fine_max_halfwidth_fraction * radius);
    const std::size_t stop_index = tracer_.stop_surface_index();
    auto sample_stop = [&](const FirstSurfaceSeed& seed, double* sx, double* sy) {
        if (!seed.valid ||
            (hard_radius > 0.0 &&
             aperture_clip_exceeded(std::hypot(seed.x, seed.y), hard_radius))) {
            return false;
        }
        Ray pilot = tracer_.generate_ray_through_first_surface_point(
            field,
            wave.value_um,
            seed.x,
            seed.y,
            1.0);
        TracePoint hit;
        if (!tracer_.trace_to_surface(pilot, stop_index, &hit) || !hit.valid) {
            return false;
        }
        *sx = hit.x;
        *sy = hit.y;
        return true;
    };

    std::vector<double> halfwidths;
    append_unique_positive(halfwidths, aperture_aware_halfwidth(halfwidth, min_halfwidth, stop_scale));
    for (int expansion = 0; expansion <= std::max(0, cfg.max_fine_expansions); ++expansion) {
        append_unique_positive(halfwidths, halfwidth);
        if (halfwidth >= max_halfwidth * (1.0 - 1.0e-12)) {
            break;
        }
        halfwidth = std::min(max_halfwidth, halfwidth * 1.5);
    }

    for (const double search_halfwidth : halfwidths) {
        for (int iy = 0; iy < n; ++iy) {
            const double dy = -search_halfwidth +
                              (n == 1 ? 0.0 : 2.0 * search_halfwidth * iy / static_cast<double>(n - 1));
            for (int ix = 0; ix < n; ++ix) {
                const double dx = -search_halfwidth +
                                  (n == 1 ? 0.0 : 2.0 * search_halfwidth * ix / static_cast<double>(n - 1));
                FirstSurfaceSeed seed{anchor.seed.x + dx, anchor.seed.y + dy, true};
                double sx = 0.0;
                double sy = 0.0;
                if (sample_stop(seed, &sx, &sy)) {
                    cache.samples.push_back({seed, sx, sy, true});
                }
            }
        }
    }

    double c_x = 0.0;
    double c_y = 0.0;
    double x_x = 0.0;
    double x_y = 0.0;
    double y_x = 0.0;
    double y_y = 0.0;
    const double min_probe = std::min(0.05, 0.01 * radius);
    const double max_probe = std::max(0.05, std::min(0.5, 0.25 * radius));
    const double h = aperture_aware_probe_step(
        cfg.jacobian_probe_fraction * radius,
        min_probe,
        max_probe,
        stop_scale);
    FirstSurfaceSeed sx_seed{anchor.seed.x + h, anchor.seed.y, true};
    FirstSurfaceSeed sy_seed{anchor.seed.x, anchor.seed.y + h, true};
    if (sample_stop(anchor.seed, &c_x, &c_y) &&
        sample_stop(sx_seed, &x_x, &x_y) &&
        sample_stop(sy_seed, &y_x, &y_y)) {
        cache.j11 = (x_x - c_x) / h;
        cache.j21 = (x_y - c_y) / h;
        cache.j12 = (y_x - c_x) / h;
        cache.j22 = (y_y - c_y) / h;
        const double det = cache.j11 * cache.j22 - cache.j12 * cache.j21;
        cache.has_jacobian = std::isfinite(det) && std::abs(det) > 1.0e-14;
    }
    return cache;
}

} // namespace optiland
