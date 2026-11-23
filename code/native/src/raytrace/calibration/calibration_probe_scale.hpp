#pragma once
#include "../detail.hpp"

namespace optiland::raytrace_detail {

struct CalibrationProbeScale {
    using FirstSurfaceSeed = AimingSession::FirstSurfaceSeed;

    template <
        class Capture_seed_in_first_surface_bounds,
        class Capture_scaled_edge_seed,
        class Capture_full_seed,
        class Capture_result,
        class Capture_target_x,
        class Capture_target_y,
        class Capture_attempted_scales,
        class Capture_best_residual,
        class Capture_scale_was_attempted
    >
    static auto run(
        AimingSession& session,
        const Wavelength& wave,
        const Field& field,
        Capture_seed_in_first_surface_bounds& seed_in_first_surface_bounds,
        Capture_scaled_edge_seed& scaled_edge_seed,
        Capture_full_seed& full_seed,
        Capture_result& result,
        Capture_target_x& target_x,
        Capture_target_y& target_y,
        Capture_attempted_scales& attempted_scales,
        Capture_best_residual& best_residual,
        Capture_scale_was_attempted& scale_was_attempted,
        double scale,
        FirstSurfaceSeed* out_seed,
        double* out_sx,
        double* out_sy,
        double* out_residual
    ) {
        if (!std::isfinite(scale) || scale <= 0.0 || scale_was_attempted(scale)) {
            return false;
        }
        attempted_scales.push_back(scale);
        const FirstSurfaceSeed seed = scaled_edge_seed(full_seed, scale);
        if (!seed_in_first_surface_bounds(seed)) {
            return false;
        }
        double sx = 0.0;
        double sy = 0.0;
        if (!session.sample_stop_from_seed(field, wave, seed, &sx, &sy, nullptr, true, true)) {
            return false;
        }
        const double residual = std::hypot(sx - target_x, sy - target_y);
        if (!std::isfinite(residual)) {
            return false;
        }
        const bool replacing_best = residual < best_residual;
        if (replacing_best) {
            if (result.sample.valid &&
                std::hypot(result.sample.seed.x - seed.x,
                           result.sample.seed.y - seed.y) > 1.0e-12) {
                result.previous_sample = result.sample;
            }
            best_residual = residual;
            result.seed = seed;
            result.have_seed = true;
            result.residual = residual;
            result.sample = {true, seed, sx, sy, residual};
        } else if (result.sample.valid &&
                   std::hypot(result.sample.seed.x - seed.x,
                              result.sample.seed.y - seed.y) > 1.0e-12 &&
                   (!result.previous_sample.valid ||
                    residual < result.previous_sample.residual)) {
            result.previous_sample = {true, seed, sx, sy, residual};
        }
        if (out_sx) {
            *out_sx = sx;
        }
        if (out_sy) {
            *out_sy = sy;
        }
        if (out_seed) {
            *out_seed = seed;
        }
        if (out_residual) {
            *out_residual = residual;
        }
        return true;
    }
};

} // namespace optiland::raytrace_detail
