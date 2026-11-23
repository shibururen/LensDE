#pragma once
#include "../detail.hpp"

namespace optiland::raytrace_detail {

struct BudgetChiefSolveBudgetChiefFixedOrSecant {
    using FirstSurfaceSeed = AimingSession::FirstSurfaceSeed;

    template <
        class Type_ChiefTargetParams,
        class Type_BudgetChiefSolveResult,
        class Capture_budget_chief_ignore_clip,
        class Capture_abcd_a,
        class Capture_convergence_limit,
        class Capture_seed_in_first_surface_bounds,
        class Capture_generate_chief_from_seed,
        class Capture_stamp_budget_solver_ray
    >
    static auto run(
        AimingSession& session,
        const Wavelength& wave,
        const int& iterations,
        Capture_budget_chief_ignore_clip& budget_chief_ignore_clip,
        Capture_abcd_a& abcd_a,
        Capture_convergence_limit& convergence_limit,
        Capture_seed_in_first_surface_bounds& seed_in_first_surface_bounds,
        Capture_generate_chief_from_seed& generate_chief_from_seed,
        Capture_stamp_budget_solver_ray& stamp_budget_solver_ray,
        const Field& solve_field,
        const Type_ChiefTargetParams& solve_target,
        const FirstSurfaceSeed& start_seed,
        bool use_secant
    ) {
        FirstSurfaceSeed current = start_seed;
        FirstSurfaceSeed previous = current;
        double previous_ex = 0.0;
        double previous_ey = 0.0;
        bool have_previous = false;
        double h11 = 1.0 / abcd_a;
        double h12 = 0.0;
        double h21 = 0.0;
        double h22 = 1.0 / abcd_a;
        int used_iterations = 0;
        double final_residual = std::numeric_limits<double>::infinity();
        bool converged = false;
        double final_sx = std::numeric_limits<double>::quiet_NaN();
        double final_sy = std::numeric_limits<double>::quiet_NaN();
        Ray final_ray;
        bool have_final_ray = false;
        for (int iter = 0; iter < iterations; ++iter) {
            if (!seed_in_first_surface_bounds(current)) {
                break;
            }
            double sx = 0.0;
            double sy = 0.0;
            Ray pilot_ray;
            if (!session.sample_stop_from_seed(
                    solve_field,
                    wave,
                    current,
                    &sx,
                    &sy,
                    &pilot_ray,
                    budget_chief_ignore_clip,
                    budget_chief_ignore_clip)) {
                break;
            }
            used_iterations = iter + 1;
            final_sx = sx;
            final_sy = sy;
            final_ray = pilot_ray;
            have_final_ray = true;
            const double ex = sx - solve_target.x;
            const double ey = sy - solve_target.y;
            final_residual = std::hypot(ex, ey);
            if (std::isfinite(final_residual) && final_residual <= convergence_limit) {
                converged = true;
                break;
            }
            if (use_secant && have_previous) {
                const double dx = current.x - previous.x;
                const double dy = current.y - previous.y;
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
            previous = current;
            previous_ex = ex;
            previous_ey = ey;
            have_previous = true;
            const double step_x = h11 * ex + h12 * ey;
            const double step_y = h21 * ex + h22 * ey;
            if (!std::isfinite(step_x) || !std::isfinite(step_y)) {
                break;
            }
            current.x -= step_x;
            current.y -= step_y;
        }
        Type_BudgetChiefSolveResult result;
        result.ray = stamp_budget_solver_ray(
            have_final_ray ? final_ray : generate_chief_from_seed(solve_field, current),
            final_residual,
            used_iterations,
            converged);
        if (std::isfinite(final_sx) && std::isfinite(final_sy)) {
            result.stop_valid = true;
            result.stop_x = final_sx;
            result.stop_y = final_sy;
        }
        return result;
    }
};

} // namespace optiland::raytrace_detail
