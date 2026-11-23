#pragma once
#include "../diagnostics_inline.hpp"
#include "../detail.hpp"

namespace optiland::raytrace_detail {

struct CalibrationAcceptChiefRay {
    using CalibratedParaxialModel = AimingSession::CalibratedParaxialModel;
    using ChiefAnchor = AimingSession::ChiefAnchor;

    template <
        class Capture_chief_debug
    >
    static auto run(
        AimingSession& session,
        std::size_t& field_index,
        std::size_t& wave_index,
        RayAimingDiagnosticsState& diag,
        CalibratedParaxialModel& model,
        const CalibratedParaxialConfig& cfg,
        const double& stop_scale,
        const double& chief_target_x,
        const double& chief_target_y,
        Capture_chief_debug& chief_debug,
        const Ray& ray,
        bool require_small_residual,
        const ChiefAnchor* cached_anchor = nullptr
    ) {
        double sx = 0.0;
        double sy = 0.0;
        bool stop_ok = false;
        if (cached_anchor && cached_anchor->stop_valid) {
            sx = cached_anchor->stop_x;
            sy = cached_anchor->stop_y;
            stop_ok = true;
        } else {
            add_diag(diag.calibrated_chief_stop_probe_calls);
            {
                ScopedDiagTimer probe_timer(diag.calibrated_chief_stop_probe_ns);
                stop_ok = session.sample_stop_from_ray(ray, &sx, &sy);
            }
        }
        if (!stop_ok) {
            return false;
        }
        if (!(cached_anchor && cached_anchor->stop_valid)) {
            add_diag(diag.calibrated_chief_stop_probe_successes);
        }
        const double residual = std::hypot(sx - chief_target_x, sy - chief_target_y);
        const double limit = std::max(
            session.system_.ray_aiming.tolerance,
            cfg.validation_residual_stop_fraction * stop_scale);
        if (chief_debug) {
            std::fprintf(
                stderr,
                "CPU calibrated chief field=%zu wave=%zu stage=%s stop=(%.17g,%.17g) "
                "target=(%.17g,%.17g) residual=%.17g limit=%.17g seed_valid=%d\n",
                field_index,
                wave_index,
                require_small_residual ? "paraxial" : "budget",
                sx,
                sy,
                chief_target_x,
                chief_target_y,
                residual,
                limit,
                session.seed_from_ray(ray).valid ? 1 : 0);
        }
        if (require_small_residual && residual > limit) {
            return false;
        }
        // The accepted anchor already extracted this same ray at the first surface.
        model.chief_seed = cached_anchor && cached_anchor->seed.valid
                               ? cached_anchor->seed : session.seed_from_ray(ray);
        if (!model.chief_seed.valid) {
            return false;
        }
        model.chief_stop_x = sx;
        model.chief_stop_y = sy;
        add_diag_residual(
            diag.calibrated_chief_residual_count,
            diag.calibrated_chief_residual_sum_scaled,
            diag.calibrated_chief_residual_max_scaled,
            residual);
        return true;
    }
};

} // namespace optiland::raytrace_detail
