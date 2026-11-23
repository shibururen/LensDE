#pragma once
#include "../diagnostics_inline.hpp"
#include "../detail.hpp"

namespace optiland::raytrace_detail {

struct CalibrationSolveSeedSpaceEdge {
    using CalibratedParaxialModel = AimingSession::CalibratedParaxialModel;
    using FirstSurfaceSeed = AimingSession::FirstSurfaceSeed;

    template <
        class Type_EdgeStopSample,
        class Capture_seed_in_first_surface_bounds,
        class Capture_accept_edge_stop_sample,
        class Capture_accept_edge_seed
    >
    static auto run(
        AimingSession& session,
        const Wavelength& wave,
        RayAimingDiagnosticsState& diag,
        CalibratedParaxialModel& model,
        const Field& field,
        const double& rs,
        Capture_seed_in_first_surface_bounds& seed_in_first_surface_bounds,
        const double& residual_limit,
        const int& max_iterations,
        Capture_accept_edge_stop_sample& accept_edge_stop_sample,
        Capture_accept_edge_seed& accept_edge_seed,
        CalibratedParaxialModel::EdgeAnchor& anchor,
        const PupilPoint& edge,
        FirstSurfaceSeed seed,
        bool use_secant,
        const Type_EdgeStopSample* initial_sample,
        const Type_EdgeStopSample* previous_sample,
        const std::array<double, 4>* initial_inverse,
        double step_cap,
        int failure_backtrack_trials
    ) {
        if (!seed_in_first_surface_bounds(seed)) {
            return false;
        }
        const double target_x = (field.fvcx + edge.px * (1.0 - field.fvdx)) * rs;
        const double target_y = (field.fvcy + edge.py * (1.0 - field.fvdy)) * rs;
        const double target_dx = edge.px * (1.0 - field.fvdx) * rs;
        const double target_dy = edge.py * (1.0 - field.fvdy) * rs;
        const double target_dr = std::max(1.0e-9, std::hypot(target_dx, target_dy));
        const double seed_dr = std::max(
            1.0e-9,
            std::hypot(seed.x - model.chief_seed.x, seed.y - model.chief_seed.y));
        const double initial_scale = seed_dr / target_dr;
        double h11 = initial_scale;
        double h12 = 0.0;
        double h21 = 0.0;
        double h22 = initial_scale;
        if (initial_inverse) {
            h11 = (*initial_inverse)[0];
            h12 = (*initial_inverse)[1];
            h21 = (*initial_inverse)[2];
            h22 = (*initial_inverse)[3];
        }
        FirstSurfaceSeed previous_seed = seed;
        double previous_ex = 0.0;
        double previous_ey = 0.0;
        bool have_previous = false;
        bool saw_valid = false;
        FirstSurfaceSeed last_sampled_seed;
        double last_sx = 0.0;
        double last_sy = 0.0;
        bool have_last_sample = false;
        bool use_initial_sample =
            initial_sample &&
            initial_sample->valid &&
            initial_sample->seed.valid &&
            initial_sample->seed.x == seed.x &&
            initial_sample->seed.y == seed.y &&
            std::isfinite(initial_sample->sx) &&
            std::isfinite(initial_sample->sy);
        if (use_secant &&
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
        add_diag(diag.calibrated_edge_correction_calls);
        {
            ScopedDiagTimer edge_correction_timer(diag.calibrated_edge_correction_ns);
            for (int iter = 0; iter < max_iterations; ++iter) {
                double sx = 0.0;
                double sy = 0.0;
                if (use_initial_sample) {
                    sx = initial_sample->sx;
                    sy = initial_sample->sy;
                    use_initial_sample = false;
                } else {
                    auto sample_candidate = [&](const FirstSurfaceSeed& candidate,
                                                double* out_sx,
                                                double* out_sy) {
                        return seed_in_first_surface_bounds(candidate) &&
                               session.sample_stop_from_seed(
                                   field,
                                   wave,
                                   candidate,
                                   out_sx,
                                   out_sy,
                                   nullptr,
                                   true,
                                   true);
                    };
                    bool sampled = sample_candidate(seed, &sx, &sy);
                    if (!sampled && have_previous) {
                        const FirstSurfaceSeed rejected_seed = seed;
                        double fraction = 0.5;
                        for (int trial = 0;
                             trial < std::max(0, failure_backtrack_trials) && !sampled;
                             ++trial, fraction *= 0.5) {
                            FirstSurfaceSeed backtracked;
                            backtracked.x = previous_seed.x +
                                            fraction *
                                                (rejected_seed.x - previous_seed.x);
                            backtracked.y = previous_seed.y +
                                            fraction *
                                                (rejected_seed.y - previous_seed.y);
                            backtracked.valid =
                                std::isfinite(backtracked.x) &&
                                std::isfinite(backtracked.y);
                            if (sample_candidate(backtracked, &sx, &sy)) {
                                seed = backtracked;
                                sampled = true;
                            }
                        }
                    }
                    if (!sampled) {
                        break;
                    }
                }
                add_diag(diag.calibrated_edge_correction_iterations);
                last_sampled_seed = seed;
                last_sx = sx;
                last_sy = sy;
                have_last_sample = true;
                const double ex = sx - target_x;
                const double ey = sy - target_y;
                const double residual = std::hypot(ex, ey);
                saw_valid = std::isfinite(residual);
                if (saw_valid && residual <= residual_limit) {
                    break;
                }
                if (use_secant && have_previous) {
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
                const double bounded_step_x = std::isfinite(step_cap)
                                                  ? std::clamp(step_x, -step_cap, step_cap)
                                                  : step_x;
                const double bounded_step_y = std::isfinite(step_cap)
                                                  ? std::clamp(step_y, -step_cap, step_cap)
                                                  : step_y;
                seed.x -= bounded_step_x;
                seed.y -= bounded_step_y;
                if (!use_secant) {
                    have_previous = false;
                }
            }
        }
        if (!saw_valid) {
            return false;
        }
        if (have_last_sample && seed.valid == last_sampled_seed.valid &&
            seed.x == last_sampled_seed.x && seed.y == last_sampled_seed.y) {
            return accept_edge_stop_sample(anchor, edge, seed, last_sx, last_sy);
        }
        return accept_edge_seed(anchor, edge, seed);
    }
};

} // namespace optiland::raytrace_detail
