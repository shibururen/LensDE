#pragma once
#include "../diagnostics_inline.hpp"
#include "../detail.hpp"
#include "three_step_solve_seed_space_edge.hpp"
#include "three_step_try_scaled_edge_anchor.hpp"

namespace optiland::raytrace_detail {

struct ThreeStepSolveEdge {
    using ThreeStepModel = AimingSession::ThreeStepModel;
    using FirstSurfaceSeed = AimingSession::FirstSurfaceSeed;

    template <
        class Capture_edge_radius,
        class Capture_edge_passes,
        class Capture_edge_residual_limit,
        class Capture_seed_in_first_surface_bounds,
        class Capture_fixed_jacobian_edge_solver,
        class Capture_secant_edge_solver,
        class Capture_legacy_iterative_edge_solver,
        class Capture_axis_scale_hints,
        class Capture_remember_scale_hint
    >
    static auto run(
        AimingSession& session,
        const Field& field,
        const Wavelength& wave,
        RayAimingDiagnosticsState& diag,
        ThreeStepModel& model,
        Capture_edge_radius& edge_radius,
        const double& rs,
        Capture_edge_passes& edge_passes,
        Capture_edge_residual_limit& edge_residual_limit,
        Capture_seed_in_first_surface_bounds& seed_in_first_surface_bounds,
        Capture_fixed_jacobian_edge_solver& fixed_jacobian_edge_solver,
        Capture_secant_edge_solver& secant_edge_solver,
        Capture_legacy_iterative_edge_solver& legacy_iterative_edge_solver,
        double& max_residual,
        Capture_axis_scale_hints& axis_scale_hints,
        double& common_scale_hint,
        Capture_remember_scale_hint& remember_scale_hint,
        double axis_x,
        double axis_y,
        double sign,
        int iterations,
        FirstSurfaceSeed* out_seed
    ) {
        add_diag(diag.three_step_edge_solve_calls);
        ScopedDiagTimer edge_timer(diag.three_step_edge_solve_ns);
        const PupilPoint edge_pupil{sign * axis_x, sign * axis_y, 1.0};
        FirstSurfaceSeed full_seed = session.shifted_chief_seed_by_transverse_radius(
            field,
            wave,
            model.center,
            edge_pupil,
            edge_radius);
        if (!full_seed.valid) {
            full_seed = {
                model.center.x + sign * edge_radius * axis_x,
                model.center.y + sign * edge_radius * axis_y,
                true};
        }
        const double target_x = (field.fvcx + edge_pupil.px * (1.0 - field.fvdx)) * rs;
        const double target_y = (field.fvcy + edge_pupil.py * (1.0 - field.fvdy)) * rs;
        const double target_dx = edge_pupil.px * (1.0 - field.fvdx) * rs;
        const double target_dy = edge_pupil.py * (1.0 - field.fvdy) * rs;
        const double target_dr = std::hypot(target_dx, target_dy);
        const double center_target_x = field.fvcx * rs;
        const double center_target_y = field.fvcy * rs;
        const std::size_t axis_index = std::abs(edge_pupil.px) >= std::abs(edge_pupil.py) ? 0 : 1;

        struct EdgeStopSample {
            bool valid = false;
            FirstSurfaceSeed seed;
            double sx = 0.0;
            double sy = 0.0;
            double residual = std::numeric_limits<double>::infinity();
        };
        struct EdgeBackoffResult {
            bool accepted = false;
            bool have_seed = false;
            FirstSurfaceSeed seed;
            EdgeStopSample sample;
            EdgeStopSample previous_sample;
            double residual = std::numeric_limits<double>::infinity();
        };

        auto accept_edge_stop_sample = [&](const FirstSurfaceSeed& seed,
                                           double sx,
                                           double sy) {
            if (!seed_in_first_surface_bounds(seed)) {
                return false;
            }
            const double residual = std::hypot(sx - target_x, sy - target_y);
            if (!std::isfinite(residual) || residual > edge_residual_limit) {
                return false;
            }
            max_residual = std::max(max_residual, residual);
            add_diag_residual(
                diag.three_step_edge_residual_count,
                diag.three_step_edge_residual_sum_scaled,
                diag.three_step_edge_residual_max_scaled,
                residual);
            if (out_seed) {
                *out_seed = seed;
            }
            remember_scale_hint(axis_index, full_seed, seed);
            return true;
        };

        auto accept_edge_seed = [&](const FirstSurfaceSeed& seed) {
            double sx = 0.0;
            double sy = 0.0;
            add_diag(diag.three_step_edge_initial_ray_calls);
            if (!session.sample_stop_from_seed(field, wave, seed, &sx, &sy, nullptr, true, true)) {
                return false;
            }
            return accept_edge_stop_sample(seed, sx, sy);
        };

        auto scaled_edge_seed = [&](double scale) {
            FirstSurfaceSeed seed;
            if (!full_seed.valid || !std::isfinite(scale) || scale <= 0.0) {
                return seed;
            }
            seed.x = model.center.x + scale * (full_seed.x - model.center.x);
            seed.y = model.center.y + scale * (full_seed.y - model.center.y);
            seed.valid = std::isfinite(seed.x) && std::isfinite(seed.y);
            return seed;
        };

        auto solve_seed_space_edge = [&](FirstSurfaceSeed seed,
                                         const EdgeStopSample* initial_sample,
                                         const EdgeStopSample* previous_sample) {
        return ::optiland::raytrace_detail::ThreeStepSolveSeedSpaceEdge::run<EdgeStopSample>(
            session,
            field,
            wave,
            diag,
            model,
            edge_residual_limit,
            seed_in_first_surface_bounds,
            fixed_jacobian_edge_solver,
            secant_edge_solver,
            iterations,
            target_x,
            target_y,
            target_dr,
            accept_edge_stop_sample,
            accept_edge_seed,
            seed,
            initial_sample,
            previous_sample
        );
    };

        auto try_scaled_edge_anchor = [&]() {
        return ::optiland::raytrace_detail::ThreeStepTryScaledEdgeAnchor::run<EdgeBackoffResult>(
            session,
            field,
            wave,
            diag,
            seed_in_first_surface_bounds,
            axis_scale_hints,
            common_scale_hint,
            out_seed,
            full_seed,
            target_x,
            target_y,
            target_dx,
            target_dy,
            target_dr,
            center_target_x,
            center_target_y,
            axis_index,
            accept_edge_stop_sample,
            scaled_edge_seed
        );
    };

        EdgeBackoffResult backoff = try_scaled_edge_anchor();
        if (backoff.accepted) {
            return true;
        }
        if ((fixed_jacobian_edge_solver || secant_edge_solver) &&
            iterations > 0 &&
            backoff.have_seed) {
            if (solve_seed_space_edge(
                    backoff.seed,
                    backoff.sample.valid ? &backoff.sample : nullptr,
                    backoff.previous_sample.valid ? &backoff.previous_sample : nullptr)) {
                return true;
            }
            if (secant_edge_solver && backoff.previous_sample.valid &&
                solve_seed_space_edge(
                    backoff.seed,
                    backoff.sample.valid ? &backoff.sample : nullptr,
                    nullptr)) {
                return true;
            }
            return false;
        }

        if (!(fixed_jacobian_edge_solver || secant_edge_solver)) {
            FirstSurfaceSeed seed = backoff.have_seed ? backoff.seed : full_seed;
            bool saw_valid = false;
            for (int pass = 0; pass < edge_passes; ++pass) {
                add_diag(diag.three_step_edge_initial_ray_calls);
                Ray ray = session.tracer_.generate_ray_through_first_surface_point(
                    field,
                    wave.value_um,
                    seed.x,
                    seed.y,
                    1.0);
                if (iterations > 0 || legacy_iterative_edge_solver) {
                    add_diag(diag.three_step_edge_correction_calls);
                    {
                        ScopedDiagTimer correction_timer(diag.three_step_edge_correction_ns);
                        ray = session.tracer_.solve_stop_aim_from_initial(
                            field,
                            edge_pupil,
                            wave.value_um,
                            ray,
                            false,
                            iterations);
                    }
                    add_diag(diag.three_step_edge_correction_iterations,
                             std::max(0, ray.aim_iterations));
                }
                const FirstSurfaceSeed next = session.seed_from_ray(ray);
                if (next.valid) {
                    seed = next;
                    saw_valid = true;
                }
            }
            if (!saw_valid) {
                return false;
            }
            double sx = 0.0;
            double sy = 0.0;
            add_diag(diag.three_step_model_stop_probe_calls);
            bool stop_ok = false;
            {
                ScopedDiagTimer stop_timer(diag.three_step_model_stop_probe_ns);
                stop_ok = session.sample_stop_from_seed(field, wave, seed, &sx, &sy, nullptr, true, true);
            }
            if (stop_ok) {
                add_diag(diag.three_step_model_stop_probe_successes);
                return accept_edge_stop_sample(seed, sx, sy);
            }
        }
        return false;
    }
};

} // namespace optiland::raytrace_detail
