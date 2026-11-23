// Build and recover pilot-guided aiming candidates from chief Jacobians and nearby solved rays.
// AimingSession trace hooks update the same session caches after physical tracing succeeds.
// Keep seed priority, strict-aim acceptance, and post-trace cache publication in their current order.

#include "trace_diagnostics_inline.hpp"
#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

void AimingSession::append_chief_jacobian_seed(
    std::vector<FirstSurfaceSeed>& seeds,
    const PilotCache& cache,
    const Field& field,
    const PupilPoint& pupil,
    double wavelength_um) const {
    const auto& cfg = system_.ray_aiming.adaptive_pilot_grid;
    if (!cfg.use_chief_jacobian_seed || !cache.has_jacobian || !cache.chief_seed.valid) {
        return;
    }
    const double rs = tracer_.stop_target_radius(wavelength_um, RayAimingMode::AdaptivePilotGrid);
    const double target_x = (field.fvcx + pupil.px * (1.0 - field.fvdx)) * rs;
    const double target_y = (field.fvcy + pupil.py * (1.0 - field.fvdy)) * rs;
    const double chief_x = field.fvcx * rs;
    const double chief_y = field.fvcy * rs;
    const double ex = target_x - chief_x;
    const double ey = target_y - chief_y;
    const double det = cache.j11 * cache.j22 - cache.j12 * cache.j21;
    if (std::abs(det) <= 1.0e-14 || !std::isfinite(det)) {
        return;
    }
    const double dx = (cache.j22 * ex - cache.j12 * ey) / det;
    const double dy = (-cache.j21 * ex + cache.j11 * ey) / det;
    const double radius = first_surface_search_radius(wavelength_um);
    const double max_step = std::max(1.0e-9, cfg.fine_max_halfwidth_fraction * radius);
    if (std::hypot(dx, dy) > max_step * cfg.jacobian_safety_factor) {
        return;
    }
    append_seed(seeds, {cache.chief_seed.x + dx, cache.chief_seed.y + dy, true}, wavelength_um);
}

void AimingSession::append_pilot_nearest_seeds(
    std::vector<FirstSurfaceSeed>& seeds,
    const PilotCache& cache,
    const Field& field,
    const PupilPoint& pupil,
    double wavelength_um) const {
    if (cache.samples.empty()) {
        return;
    }
    const double rs = tracer_.stop_target_radius(wavelength_um, RayAimingMode::AdaptivePilotGrid);
    const double target_x = (field.fvcx + pupil.px * (1.0 - field.fvdx)) * rs;
    const double target_y = (field.fvcy + pupil.py * (1.0 - field.fvdy)) * rs;
    std::vector<std::pair<double, FirstSurfaceSeed>> ranked;
    ranked.reserve(cache.samples.size());
    for (const auto& sample : cache.samples) {
        if (!sample.valid) {
            continue;
        }
        const double err = (sample.stop_x - target_x) * (sample.stop_x - target_x) +
                           (sample.stop_y - target_y) * (sample.stop_y - target_y);
        ranked.push_back({err, sample.seed});
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });
    const int count = std::min<int>(
        std::max(1, system_.ray_aiming.adaptive_pilot_grid.nearest_k),
        static_cast<int>(ranked.size()));
    for (int i = 0; i < count; ++i) {
        append_seed(seeds, ranked[static_cast<std::size_t>(i)].second, wavelength_um);
    }
}

Ray AimingSession::generate_adaptive_pilot_candidate(
    std::size_t field_index,
    std::size_t wave_index,
    std::size_t pupil_index,
    const Field& field,
    const Wavelength& wave,
    const PupilPoint& pupil) {
    fields_seen_[field_index] = field;
    const auto& cfg = system_.ray_aiming.adaptive_pilot_grid;
    const bool strict =
        system_.ray_aiming.require_convergence ||
        system_.ray_aiming.adaptive_pilot_grid.require_final_convergence;
    const FieldWaveKey field_wave_key{field_index, wave_index};
    Ray iterative_ray;
    bool has_iterative_ray = false;
    if (!cfg.enabled) {
        return tracer_.generate_ray_with_mode(field, pupil, wave.value_um, RayAimingMode::Iterative);
    }
    const bool sample_limit_reached =
        cfg.iterative_failure_sample_limit > 0 &&
        iterative_failures_[field_wave_key] >= cfg.iterative_failure_sample_limit;
    if (cfg.try_iterative_first && cfg.try_iterative_for_each_ray && !sample_limit_reached) {
        iterative_ray = tracer_.generate_ray_with_mode(field, pupil, wave.value_um, RayAimingMode::Iterative);
        has_iterative_ray = true;
        if (is_strictly_aimed(iterative_ray, field)) {
            const FirstSurfaceSeed iterative_seed = seed_from_ray(iterative_ray);
            if (iterative_seed.valid) {
                Ray seeded_ray = solve_from_seed(field, wave, pupil, iterative_seed);
                if (is_strictly_aimed(seeded_ray, field)) {
                    iterative_ray = seeded_ray;
                    if (pupil_index == 0) {
                        ChiefAnchor anchor;
                        anchor.seed = seed_from_ray(iterative_ray);
                        anchor.ray = iterative_ray;
                        anchor.valid = anchor.seed.valid;
                        if (anchor.valid) {
                            chief_anchors_[FieldWaveKey{field_index, wave_index}] = anchor;
                        }
                    }
                    remember_solution(field_index, wave_index, pupil_index, iterative_ray);
                    return iterative_ray;
                }
            }
            remember_solution(field_index, wave_index, pupil_index, iterative_ray);
            return iterative_ray;
        }
        ++iterative_failures_[field_wave_key];
    }

    ChiefAnchor anchor = ensure_chief_anchor(field_index, wave_index, field, wave);
    Ray best_ray = has_iterative_ray ? iterative_ray : make_failed_ray(wave, pupil);
    bool has_best_ray = has_iterative_ray;
    double best_residual = has_iterative_ray && std::isfinite(iterative_ray.aim_residual)
                               ? iterative_ray.aim_residual
                               : std::numeric_limits<double>::infinity();

    std::vector<FirstSurfaceSeed> seeds;
    append_reuse_seeds(seeds, field_index, wave_index, pupil_index);
    if (anchor.valid) {
        PilotCache& cache = ensure_pilot_cache(field_index, wave_index, field, wave, anchor);
        append_chief_jacobian_seed(seeds, cache, field, pupil, wave.value_um);
        append_pilot_nearest_seeds(seeds, cache, field, pupil, wave.value_um);
        append_seed(seeds, anchor.seed, wave.value_um);
    }
    if (!cfg.use_multistart && seeds.size() > 1) {
        seeds.resize(1);
    }

    for (const auto& seed : seeds) {
        Ray solved = solve_from_seed(field, wave, pupil, seed);
        if (is_strictly_aimed(solved, field)) {
            remember_solution(field_index, wave_index, pupil_index, solved);
            return solved;
        }
        if (std::isfinite(solved.aim_residual) && solved.aim_residual < best_residual) {
            best_residual = solved.aim_residual;
            best_ray = solved;
            has_best_ray = true;
        }
    }

    if (!has_iterative_ray && has_best_ray) {
        return best_ray;
    }
    if (has_iterative_ray && strict && iterative_ray.failure_code != RayFailureCode::AimNotConverged) {
        mark_ray_failure(
            iterative_ray,
            RayFailureCode::AimNotConverged,
            static_cast<int>(tracer_.stop_surface_index()),
            std::isfinite(iterative_ray.aim_residual)
                ? iterative_ray.aim_residual / std::max(1.0e-9, iterative_ray.aim_target_radius)
                : 1.0,
            true);
    }
    if (has_iterative_ray &&
        (!std::isfinite(best_residual) ||
         (std::isfinite(iterative_ray.aim_residual) && iterative_ray.aim_residual <= best_residual))) {
        best_ray = iterative_ray;
        has_best_ray = true;
    }
    if (has_best_ray) {
        return best_ray;
    }
    return make_failed_ray(wave, pupil, best_residual);
}

Ray AimingSession::generate_adaptive_pilot(
    std::size_t field_index,
    std::size_t wave_index,
    std::size_t pupil_index,
    const Field& field,
    const Wavelength& wave,
    const PupilPoint& pupil) {
    Ray original = generate_adaptive_pilot_candidate(
        field_index, wave_index, pupil_index, field, wave, pupil);
    if (!system_.ray_aiming.adaptive_pilot_grid.enabled) {
        return original;
    }
    Ray traced = original;
    tracer_.trace_ray(traced);
    bool recovered = false;
    return recover_adaptive_pilot_trace(
        field_index, wave_index, pupil_index, field, wave, pupil, original, traced, recovered);
}

Ray AimingSession::generate_for_trace(
    std::size_t field_index, std::size_t wave_index, std::size_t pupil_index,
    const Field& field, const Wavelength& wave, const PupilPoint& pupil) {
    if (system_.ray_aiming.mode == RayAimingMode::AdaptivePilotGrid) {
        return generate_adaptive_pilot_candidate(field_index, wave_index, pupil_index, field, wave, pupil);
    }
    return generate(field_index, wave_index, pupil_index, field, wave, pupil);
}

bool AimingSession::finish_trace(
    std::size_t field_index, std::size_t wave_index, std::size_t pupil_index,
    const Field& field, const Wavelength& wave, const PupilPoint& pupil,
    Ray& initial, Ray& traced) {
    if (system_.ray_aiming.mode != RayAimingMode::AdaptivePilotGrid) {
        return false;
    }
    bool recovered = false;
    initial = recover_adaptive_pilot_trace(
        field_index, wave_index, pupil_index, field, wave, pupil, initial, traced, recovered);
    return recovered;
}

Ray AimingSession::recover_adaptive_pilot_trace(
    std::size_t field_index, std::size_t wave_index, std::size_t pupil_index,
    const Field& field, const Wavelength& wave, const PupilPoint& pupil,
    const Ray& original, Ray& physical, bool& recovered) {
    const Ray original_physical = physical;
    if (!system_.ray_aiming.adaptive_pilot_grid.enabled) {
        return original;
    }
    const FieldWaveKey key{field_index, wave_index};
    auto transmitted = [&](const Ray& initial) {
        if (!is_strictly_aimed(initial, field)) {
            return false;
        }
        Ray traced = initial;
        if (!tracer_.trace_ray(traced) || !traced.valid || traced.backward_propagation) {
            return false;
        }
        physical = traced;
        return true;
    };
    auto remember_transmitted = [&](const Ray& ray) {
        const auto previous = solved_seeds_.find(FieldWavePupilKey{field_index, wave_index, pupil_index});
        const FirstSurfaceSeed seed = !recovered && previous != solved_seeds_.end()
                                          ? previous->second : seed_from_ray(ray);
        if (!seed.valid) {
            return;
        }
        if (recovered) {
            solved_seeds_[FieldWavePupilKey{field_index, wave_index, pupil_index}] = seed;
            // Pupil index zero can also denote the first ring sample.
            if (pupil.px == 0.0 && pupil.py == 0.0) {
                ChiefAnchor anchor;
                anchor.seed = seed;
                anchor.ray = ray;
                anchor.valid = true;
                chief_anchors_[key] = anchor;
                pilot_caches_.erase(key);
            }
        }
        auto& bank = transmitted_apg_seeds_[key];
        for (auto& entry : bank) {
            if (entry.pupil.px == pupil.px && entry.pupil.py == pupil.py) {
                entry.seed = seed;
                return;
            }
        }
        if (bank.size() == 32) {
            bank.erase(bank.begin());
        }
        bank.push_back({pupil, seed});
    };
    if (is_strictly_aimed(original, field) && physical.valid &&
        physical.intensity > 0.0 && !physical.backward_propagation) {
        remember_transmitted(original);
        return original;
    }

    // Stop convergence alone does not identify a transmitted inverse branch.
    // Retry only after physical failure, without changing any aperture or target.
    // Failed candidates must not become the next ray's preferred seed.
    solved_seeds_.erase(FieldWavePupilKey{field_index, wave_index, pupil_index});
    std::vector<FirstSurfaceSeed> seeds;
    std::vector<TransmittedApgSeed> neighbors;
    const auto bank = transmitted_apg_seeds_.find(key);
    if (bank != transmitted_apg_seeds_.end()) {
        neighbors = bank->second;
        const auto distance = [&](const TransmittedApgSeed& entry) {
            return std::hypot((entry.pupil.px - pupil.px) * (1.0 - field.fvdx),
                              (entry.pupil.py - pupil.py) * (1.0 - field.fvdy));
        };
        std::partial_sort(neighbors.begin(), neighbors.begin() + std::min<std::size_t>(4, neighbors.size()),
                          neighbors.end(), [&](const auto& a, const auto& b) { return distance(a) < distance(b); });
        if (neighbors.size() > 4) {
            neighbors.resize(4);
        }
        for (const auto& entry : neighbors) {
            append_seed(seeds, entry.seed, wave.value_um);
        }
    }
    append_reuse_seeds(seeds, field_index, wave_index, pupil_index);
    const FirstSurfaceSeed rejected = seed_from_ray(original);
    if (rejected.valid) {
        // Bootstrap a different basin when the first ray of a field has no
        // transmitted neighbor yet. These are seeds, never aperture limits.
        for (double scale : {0.875, 0.75, 0.5, 0.25, 0.0, 1.25, 1.5}) {
            append_seed(seeds, {rejected.x * scale, rejected.y * scale, true}, wave.value_um);
        }
    }
    append_seed(seeds, {0.0, 0.0, true}, wave.value_um);
    const auto cache = pilot_caches_.find(key);
    if (cache != pilot_caches_.end()) {
        append_pilot_nearest_seeds(seeds, cache->second, field, pupil, wave.value_um);
    }
    const std::size_t attempts = std::min<std::size_t>(16, seeds.size());
    for (std::size_t i = 0; i < attempts; ++i) {
        Ray solved = solve_from_seed(field, wave, pupil, seeds[i]);
        if (transmitted(solved)) {
            recovered = true;
            remember_transmitted(solved);
            return solved;
        }
    }
    // A direct Newton step may cross a fold. Walk from a verified neighbor
    // through nearby stop targets, preserving strict convergence at each step.
    for (const auto& neighbor : neighbors) {
        const double distance = std::hypot(
            (neighbor.pupil.px - pupil.px) * (1.0 - field.fvdx),
            (neighbor.pupil.py - pupil.py) * (1.0 - field.fvdy));
        const int steps = std::clamp(static_cast<int>(std::ceil(distance * 16.0)), 2, 32);
        FirstSurfaceSeed seed = neighbor.seed;
        Ray solved;
        bool success = true;
        for (int step = 1; step <= steps; ++step) {
            const double fraction = static_cast<double>(step) / steps;
            const PupilPoint target{
                neighbor.pupil.px + fraction * (pupil.px - neighbor.pupil.px),
                neighbor.pupil.py + fraction * (pupil.py - neighbor.pupil.py), pupil.weight};
            solved = solve_from_seed(field, wave, target, seed);
            if (!transmitted(solved)) {
                success = false;
                break;
            }
            seed = seed_from_ray(solved);
        }
        if (success) {
            recovered = true;
            remember_transmitted(solved);
            return solved;
        }
    }
    // A genuinely blocked ray stays blocked; never relax the aiming tolerance
    // or manufacture successful coverage when no physical branch was found.
    physical = original_physical;
    return original;
}

} // namespace optiland
