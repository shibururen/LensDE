// Collect bounded reusable seeds and run STOP solves from a selected session seed.
// Chief, edge, and pilot strategies share these insertion, deduplication, and reuse rules.
// Candidate order and bound checks determine which successful solve is retained; do not reorder them.

#include "prepared_geometry_inline.hpp"
#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

void AimingSession::append_seed(std::vector<FirstSurfaceSeed>& seeds, FirstSurfaceSeed seed, double wavelength_um) const {
    if (!seed.valid || !std::isfinite(seed.x) || !std::isfinite(seed.y)) {
        return;
    }
    const double hard_radius = first_surface_hard_radius(wavelength_um);
    if (hard_radius > 0.0 &&
        aperture_clip_exceeded(std::hypot(seed.x, seed.y), hard_radius)) {
        return;
    }
    const double radius = first_surface_search_radius(wavelength_um);
    for (const auto& existing : seeds) {
        if (std::hypot(existing.x - seed.x, existing.y - seed.y) <= 1.0e-8 * std::max(1.0, radius)) {
            return;
        }
    }
    seeds.push_back(seed);
}

void AimingSession::append_seed_with_bounds(
    std::vector<FirstSurfaceSeed>& seeds, FirstSurfaceSeed seed,
    double hard_radius, double search_radius) const {
    if (!seed.valid || !std::isfinite(seed.x) || !std::isfinite(seed.y)) {
        return;
    }
    if (hard_radius > 0.0 &&
        aperture_clip_exceeded(std::hypot(seed.x, seed.y), hard_radius)) {
        return;
    }
    const double duplicate_limit = 1.0e-8 * std::max(1.0, search_radius);
    for (const auto& existing : seeds) {
        const double dx = existing.x - seed.x;
        const double dy = existing.y - seed.y;
        // A coordinate outside the distance tolerance cannot be a duplicate.
        // Retain the original hypot test near the boundary and for nonfinite differences.
        if (std::abs(dx) > duplicate_limit || std::abs(dy) > duplicate_limit) {
            continue;
        }
        if (std::hypot(dx, dy) <= duplicate_limit) {
            return;
        }
    }
    seeds.push_back(seed);
}

Ray AimingSession::solve_from_seed(
    const Field& field,
    const Wavelength& wave,
    const PupilPoint& pupil,
    const FirstSurfaceSeed& seed) const {
    if (!seed.valid) {
        return make_failed_ray(wave, pupil);
    }
    Ray initial = tracer_.generate_ray_through_first_surface_point(
        field,
        wave.value_um,
        seed.x,
        seed.y,
        pupil.weight);
    const bool strict =
        system_.ray_aiming.require_convergence ||
        system_.ray_aiming.adaptive_pilot_grid.require_final_convergence;
    return tracer_.solve_stop_aim_from_initial(
        field,
        pupil,
        wave.value_um,
        initial,
        strict,
        -1,
        false,
        true,
        seed.x,
        seed.y);
}

void AimingSession::append_reuse_seeds(
    std::vector<FirstSurfaceSeed>& seeds,
    std::size_t field_index,
    std::size_t wave_index,
    std::size_t pupil_index) const {
    const auto& cfg = system_.ray_aiming.adaptive_pilot_grid;
    if (cfg.use_previous_wave_seed && wave_index > 0) {
        auto it = solved_seeds_.find(FieldWavePupilKey{field_index, wave_index - 1, pupil_index});
        if (it != solved_seeds_.end()) {
            append_seed(
                seeds,
                it->second,
                wave_index < system_.wavelengths.size() ? system_.wavelengths[wave_index].value_um : system_.primary_wavelength()
            );
        }
    }
    if (cfg.use_previous_field_seed && field_index > 0) {
        auto it = solved_seeds_.find(FieldWavePupilKey{field_index - 1, wave_index, pupil_index});
        if (it != solved_seeds_.end()) {
            append_seed(
                seeds,
                it->second,
                wave_index < system_.wavelengths.size() ? system_.wavelengths[wave_index].value_um : system_.primary_wavelength()
            );
        }
        if (cfg.use_previous_field_extrapolation && field_index > 1) {
            auto it0 = solved_seeds_.find(FieldWavePupilKey{field_index - 1, wave_index, pupil_index});
            auto it1 = solved_seeds_.find(FieldWavePupilKey{field_index - 2, wave_index, pupil_index});
            auto f = fields_seen_.find(field_index);
            auto f0 = fields_seen_.find(field_index - 1);
            auto f1 = fields_seen_.find(field_index - 2);
            if (it0 != solved_seeds_.end() && it1 != solved_seeds_.end() &&
                f != fields_seen_.end() && f0 != fields_seen_.end() && f1 != fields_seen_.end()) {
                const double rho = std::hypot(f->second.hx, f->second.hy);
                const double rho0 = std::hypot(f0->second.hx, f0->second.hy);
                const double rho1 = std::hypot(f1->second.hx, f1->second.hy);
                double alpha = (rho - rho0) / std::max(1.0e-12, rho0 - rho1);
                alpha = std::clamp(alpha, 0.0, std::max(0.0, cfg.max_field_extrapolation_factor));
                append_seed(
                    seeds,
                    {it0->second.x + alpha * (it0->second.x - it1->second.x),
                     it0->second.y + alpha * (it0->second.y - it1->second.y),
                     true}, wave_index < system_.wavelengths.size() ? system_.wavelengths[wave_index].value_um : system_.primary_wavelength());
            }
        }
    }
    if (cfg.use_previous_pupil_seed && pupil_index > 0) {
        auto it = solved_seeds_.find(FieldWavePupilKey{field_index, wave_index, pupil_index - 1});
        if (it != solved_seeds_.end()) {
            append_seed(
                seeds,
                it->second,
                wave_index < system_.wavelengths.size() ? system_.wavelengths[wave_index].value_um : system_.primary_wavelength()
            );
        }
    }
}

} // namespace optiland
