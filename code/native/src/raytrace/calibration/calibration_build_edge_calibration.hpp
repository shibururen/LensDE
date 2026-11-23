#pragma once
#include "../diagnostics_inline.hpp"
#include "../detail.hpp"
#include "calibration_solve_secant_edge.hpp"

namespace optiland::raytrace_detail {

struct CalibrationBuildEdgeCalibration {
    using CalibratedParaxialModel = AimingSession::CalibratedParaxialModel;
    using FirstSurfaceSeed = AimingSession::FirstSurfaceSeed;

    template <
        class Capture_seed_in_first_surface_bounds,
        class Capture_linear_seed_for_pupil
    >
    static auto run(
        AimingSession& session,
        const Wavelength& wave,
        RayAimingDiagnosticsState& diag,
        CalibratedParaxialModel& model,
        const CalibratedParaxialConfig& cfg,
        const Field& field,
        const double& rs,
        const double& stop_scale,
        const double& radius,
        Capture_seed_in_first_surface_bounds& seed_in_first_surface_bounds,
        Capture_linear_seed_for_pupil& linear_seed_for_pupil
    ) {
        for (auto& anchor : model.edge_anchors) {
            anchor = {};
        }
        if (!cfg.edge_calibration_enabled || cfg.edge_calibration_iterations <= 0) {
            return;
        }
        const std::array<PupilPoint, 4> edge_pupils{{
            PupilPoint{1.0, 0.0, 1.0},
            PupilPoint{-1.0, 0.0, 1.0},
            PupilPoint{0.0, 1.0, 1.0},
            PupilPoint{0.0, -1.0, 1.0},
        }};
        const std::size_t si = session.tracer_.stop_surface_index();
        auto physical_stop_radius = [&]() {
            double radius = stop_scale;
            if (si < session.system_.surfaces.size()) {
                const Surface& stop_surface = session.system_.surfaces[si];
                if (stop_surface.aperture_radius > 0.0) {
                    radius = stop_surface.aperture_radius;
                } else if (stop_surface.semi_aperture > 0.0) {
                    radius = stop_surface.semi_aperture;
                }
            }
            return radius;
        };
        const double residual_limit = std::max(
            std::max(0.0, session.system_.ray_aiming.tolerance),
            std::max(0.0, cfg.edge_calibration_residual_stop_fraction) * physical_stop_radius());
        const int max_iterations = std::max(1, cfg.edge_calibration_iterations);
        const std::string edge_solver = cfg.edge_calibration_solver;
        const bool fixed_jacobian_edge_solver =
            edge_solver == "fixed_jacobian" ||
            edge_solver == "fixed_jacobian_only" ||
            edge_solver == "fixed" ||
            edge_solver == "linear" ||
            edge_solver == "one_step";
        const bool secant_edge_solver =
            edge_solver == "secant" ||
            edge_solver == "broyden" ||
            edge_solver == "secant_only" ||
            edge_solver == "broyden_only";
        const bool fixed_then_iterative_edge_solver =
            edge_solver == "fixed_jacobian_then_iterative" ||
            edge_solver == "fixed_then_iterative" ||
            edge_solver == "linear_then_iterative";
        const bool secant_then_iterative_edge_solver =
            edge_solver == "secant_then_iterative" ||
            edge_solver == "broyden_then_iterative";
        for (std::size_t i = 0; i < edge_pupils.size(); ++i) {
            const PupilPoint& edge = edge_pupils[i];
            auto& anchor = model.edge_anchors[i];
            add_diag(diag.calibrated_edge_solve_calls);
            anchor.px = edge.px;
            anchor.py = edge.py;
            const double target_x = (field.fvcx + edge.px * (1.0 - field.fvdx)) * rs;
            const double target_y = (field.fvcy + edge.py * (1.0 - field.fvdy)) * rs;
            const FirstSurfaceSeed linear_seed = linear_seed_for_pupil(edge);
            if (!seed_in_first_surface_bounds(linear_seed)) {
                continue;
            }
            Ray guess = session.tracer_.generate_ray_through_first_surface_point(
                field,
                wave.value_um,
                linear_seed.x,
                linear_seed.y,
                edge.weight);
            if (!guess.valid) {
                continue;
            }
            auto accept_solved_edge = [&](const Ray& solved) {
                TracePoint stop_hit;
                add_diag(diag.stop_residual_eval_calls);
                bool trace_ok = false;
                {
                    ScopedDiagTimer eval_timer(diag.stop_residual_eval_ns);
                    trace_ok = session.tracer_.trace_to_surface(solved, si, &stop_hit, true, true);
                }
                if (!trace_ok || !stop_hit.valid) {
                    return false;
                }
                add_diag(diag.stop_residual_eval_successes);
                const double residual = std::hypot(stop_hit.x - target_x, stop_hit.y - target_y);
                if (!std::isfinite(residual) || residual > residual_limit) {
                    return false;
                }
                const FirstSurfaceSeed solved_seed = session.seed_from_ray(solved);
                if (!seed_in_first_surface_bounds(solved_seed)) {
                    return false;
                }
                anchor.seed = solved_seed;
                anchor.correction_x = solved_seed.x - linear_seed.x;
                anchor.correction_y = solved_seed.y - linear_seed.y;
                anchor.valid = true;
                add_diag_residual(
                    diag.calibrated_edge_residual_count,
                    diag.calibrated_edge_residual_sum_scaled,
                    diag.calibrated_edge_residual_max_scaled,
                    residual);
                return true;
            };
            auto solve_fixed_jacobian_edge = [&]() {
                add_diag(diag.calibrated_edge_correction_calls);
                FirstSurfaceSeed fixed_seed = linear_seed;
                {
                    ScopedDiagTimer edge_correction_timer(diag.calibrated_edge_correction_ns);
                    for (int iter = 0; iter < max_iterations; ++iter) {
                        double sx = 0.0;
                        double sy = 0.0;
                        if (!session.sample_stop_from_seed(
                                field,
                                wave,
                                fixed_seed,
                                &sx,
                                &sy,
                                nullptr,
                                true,
                                true)) {
                            break;
                        }
                        add_diag(diag.calibrated_edge_correction_iterations);
                        const double ex = sx - target_x;
                        const double ey = sy - target_y;
                        const double residual = std::hypot(ex, ey);
                        if (std::isfinite(residual) && residual <= residual_limit) {
                            break;
                        }
                        fixed_seed.x -= model.b11 * ex + model.b12 * ey;
                        fixed_seed.y -= model.b21 * ex + model.b22 * ey;
                        if (!seed_in_first_surface_bounds(fixed_seed)) {
                            break;
                        }
                    }
                }
                return session.tracer_.generate_ray_through_first_surface_point(
                    field,
                    wave.value_um,
                    fixed_seed.x,
                    fixed_seed.y,
                    edge.weight);
            };
            auto solve_secant_edge = [&]() {
        return ::optiland::raytrace_detail::CalibrationSolveSecantEdge::run(
            session,
            wave,
            diag,
            model,
            field,
            seed_in_first_surface_bounds,
            residual_limit,
            max_iterations,
            edge,
            target_x,
            target_y,
            linear_seed
        );
    };
            if (fixed_jacobian_edge_solver || fixed_then_iterative_edge_solver) {
                Ray fixed = solve_fixed_jacobian_edge();
                if (accept_solved_edge(fixed) || fixed_jacobian_edge_solver) {
                    continue;
                }
            }
            if (secant_edge_solver || secant_then_iterative_edge_solver) {
                Ray secant = solve_secant_edge();
                if (accept_solved_edge(secant) || secant_edge_solver) {
                    continue;
                }
            }
            add_diag(diag.calibrated_edge_correction_calls);
            Ray solved;
            {
                ScopedDiagTimer edge_correction_timer(diag.calibrated_edge_correction_ns);
                solved = session.tracer_.solve_stop_aim_from_initial(
                    field,
                    edge,
                    wave.value_um,
                    guess,
                    false,
                    max_iterations,
                    true);
            }
            add_diag(diag.calibrated_edge_correction_iterations,
                     std::max(0, solved.aim_iterations));
            if (accept_solved_edge(solved)) {
                continue;
            }
            add_diag(diag.calibrated_edge_correction_calls);
            Ray iterative;
            {
                ScopedDiagTimer edge_correction_timer(diag.calibrated_edge_correction_ns);
                iterative = session.tracer_.generate_ray_with_mode(
                    field,
                    edge,
                    wave.value_um,
                    RayAimingMode::Iterative);
            }
            add_diag(diag.calibrated_edge_correction_iterations,
                     std::max(0, iterative.aim_iterations));
            accept_solved_edge(iterative);
        }
    }
};

} // namespace optiland::raytrace_detail
