#pragma once
#include "../diagnostics_inline.hpp"
#include "../detail.hpp"

namespace optiland::raytrace_detail {

struct ThreeStepSolveSeedSpaceEdge {
    using ThreeStepModel = AimingSession::ThreeStepModel;
    using FirstSurfaceSeed = AimingSession::FirstSurfaceSeed;

    template <
        class Type_EdgeStopSample,
        class Capture_edge_residual_limit,
        class Capture_seed_in_first_surface_bounds,
        class Capture_fixed_jacobian_edge_solver,
        class Capture_secant_edge_solver,
        class Capture_target_x,
        class Capture_target_y,
        class Capture_target_dr,
        class Capture_accept_edge_stop_sample,
        class Capture_accept_edge_seed
    >
    static auto run(
        AimingSession& session,
        const Field& field,
        const Wavelength& wave,
        RayAimingDiagnosticsState& diag,
        ThreeStepModel& model,
        Capture_edge_residual_limit& edge_residual_limit,
        Capture_seed_in_first_surface_bounds& seed_in_first_surface_bounds,
        Capture_fixed_jacobian_edge_solver& fixed_jacobian_edge_solver,
        Capture_secant_edge_solver& secant_edge_solver,
        const int& iterations,
        Capture_target_x& target_x,
        Capture_target_y& target_y,
        Capture_target_dr& target_dr,
        Capture_accept_edge_stop_sample& accept_edge_stop_sample,
        Capture_accept_edge_seed& accept_edge_seed,
        FirstSurfaceSeed seed,
        const Type_EdgeStopSample* initial_sample,
        const Type_EdgeStopSample* previous_sample
    ) {
        if (!seed_in_first_surface_bounds(seed)) {
            return false;
        }
        const double safe_target_dr = std::max(1.0e-9, target_dr);
        const double seed_dr = std::max(
            1.0e-9,
            std::hypot(seed.x - model.center.x, seed.y - model.center.y));
        const double initial_scale = seed_dr / safe_target_dr;
        double h11 = initial_scale;
        double h12 = 0.0;
        double h21 = 0.0;
        double h22 = initial_scale;
        FirstSurfaceSeed previous_seed = seed;
        double previous_ex = 0.0;
        double previous_ey = 0.0;
        bool have_previous = false;
        FirstSurfaceSeed last_sampled_seed;
        double last_sx = 0.0;
        double last_sy = 0.0;
        bool have_last_sample = false;
        bool saw_valid = false;
        bool use_initial_sample =
            initial_sample &&
            initial_sample->valid &&
            initial_sample->seed.valid &&
            initial_sample->seed.x == seed.x &&
            initial_sample->seed.y == seed.y &&
            std::isfinite(initial_sample->sx) &&
            std::isfinite(initial_sample->sy);
        if (secant_edge_solver &&
            previous_sample &&
            previous_sample->valid &&
            previous_sample->seed.valid &&
            std::isfinite(previous_sample->sx) &&
            std::isfinite(previous_sample->sy) &&
            std::hypot(previous_sample->seed.x - seed.x,
                       previous_sample->seed.y - seed.y) > 1.0e-12) {
            previous_seed = previous_sample->seed;
            previous_ex = previous_sample->sx - target_x;
            previous_ey = previous_sample->sy - target_y;
            have_previous = std::isfinite(previous_ex) && std::isfinite(previous_ey);
        }
        add_diag(diag.three_step_edge_correction_calls);
        {
            ScopedDiagTimer correction_timer(diag.three_step_edge_correction_ns);
            for (int iter = 0; iter < std::max(0, iterations); ++iter) {
                double sx = 0.0;
                double sy = 0.0;
                if (use_initial_sample) {
                    sx = initial_sample->sx;
                    sy = initial_sample->sy;
                    use_initial_sample = false;
                } else {
                    if (!session.sample_stop_from_seed(field, wave, seed, &sx, &sy, nullptr, true, true)) {
                        break;
                    }
                }
                add_diag(diag.three_step_edge_correction_iterations);
                last_sampled_seed = seed;
                last_sx = sx;
                last_sy = sy;
                have_last_sample = true;
                const double ex = sx - target_x;
                const double ey = sy - target_y;
                const double residual = std::hypot(ex, ey);
                saw_valid = std::isfinite(residual);
                if (saw_valid && residual <= edge_residual_limit) {
                    return accept_edge_stop_sample(seed, sx, sy);
                }
                if (secant_edge_solver && have_previous) {
                    const double dx = seed.x - previous_seed.x;
                    const double dy = seed.y - previous_seed.y;
                    const double yx = ex - previous_ex;
                    const double yy = ey - previous_ey;
                    const double hyx = h11 * yx + h12 * yy;
                    const double hyy = h21 * yx + h22 * yy;
                    const double ux = dx - hyx;
                    const double uy = dy - hyy;
                    const double denom = yx * yx + yy * yy;
                    if (std::isfinite(denom) && denom > 1.0e-24) {
                        const double scale = 1.0 / denom;
                        h11 += ux * yx * scale;
                        h12 += ux * yy * scale;
                        h21 += uy * yx * scale;
                        h22 += uy * yy * scale;
                    }
                }
                previous_seed = seed;
                previous_ex = ex;
                previous_ey = ey;
                have_previous = true;
                const double step_x = h11 * ex + h12 * ey;
                const double step_y = h21 * ex + h22 * ey;
                if (!std::isfinite(step_x) || !std::isfinite(step_y)) {
                    break;
                }
                seed.x -= step_x;
                seed.y -= step_y;
                if (!seed_in_first_surface_bounds(seed)) {
                    break;
                }
                if (fixed_jacobian_edge_solver) {
                    have_previous = false;
                }
            }
        }
        if (!saw_valid) {
            return false;
        }
        if (have_last_sample &&
            seed.valid == last_sampled_seed.valid &&
            seed.x == last_sampled_seed.x &&
            seed.y == last_sampled_seed.y) {
            return accept_edge_stop_sample(seed, last_sx, last_sy);
        }
        return accept_edge_seed(seed);
    }
};

} // namespace optiland::raytrace_detail
