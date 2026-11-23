#pragma once
#include "../diagnostics_inline.hpp"
#include "../detail.hpp"

namespace optiland::raytrace_detail {

struct CalibrationSolveSecantEdge {
    using CalibratedParaxialModel = AimingSession::CalibratedParaxialModel;
    using FirstSurfaceSeed = AimingSession::FirstSurfaceSeed;

    template <
        class Capture_seed_in_first_surface_bounds,
        class Capture_edge,
        class Capture_target_x,
        class Capture_target_y,
        class Capture_linear_seed
    >
    static auto run(
        AimingSession& session,
        const Wavelength& wave,
        RayAimingDiagnosticsState& diag,
        CalibratedParaxialModel& model,
        const Field& field,
        Capture_seed_in_first_surface_bounds& seed_in_first_surface_bounds,
        const double& residual_limit,
        const int& max_iterations,
        Capture_edge& edge,
        Capture_target_x& target_x,
        Capture_target_y& target_y,
        Capture_linear_seed& linear_seed
    ) {
        add_diag(diag.calibrated_edge_correction_calls);
        FirstSurfaceSeed seed = linear_seed;
        FirstSurfaceSeed previous_seed = seed;
        double previous_ex = 0.0;
        double previous_ey = 0.0;
        bool have_previous = false;
        double h11 = model.b11;
        double h12 = model.b12;
        double h21 = model.b21;
        double h22 = model.b22;
        {
            ScopedDiagTimer edge_correction_timer(diag.calibrated_edge_correction_ns);
            for (int iter = 0; iter < max_iterations; ++iter) {
                double sx = 0.0;
                double sy = 0.0;
                if (!session.sample_stop_from_seed(field, wave, seed, &sx, &sy, nullptr, true, true)) {
                    break;
                }
                add_diag(diag.calibrated_edge_correction_iterations);
                const double ex = sx - target_x;
                const double ey = sy - target_y;
                const double residual = std::hypot(ex, ey);
                if (std::isfinite(residual) && residual <= residual_limit) {
                    break;
                }
                if (have_previous) {
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
            }
        }
        return session.tracer_.generate_ray_through_first_surface_point(
            field,
            wave.value_um,
            seed.x,
            seed.y,
            edge.weight);
    }
};

} // namespace optiland::raytrace_detail
