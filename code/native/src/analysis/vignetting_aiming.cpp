/*
 * Generates or corrects rays aimed at requested stop-pupil coordinates for field solving.
 * AutomaticVignettingFieldSolver branch and batch probes use these aiming entry points.
 * Reference-pupil transforms, shared-session indices and exact-fallback rules retain
 * the correspondence between a requested boundary point and its physical stop hit.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
bool AutomaticVignettingFieldSolver::aim_target(double pupil_x, double pupil_y, Ray* ray_out) {
        if (!ray_out) {
            return false;
        }
        const double scale_x = 1.0 - field_.fvdx;
        const double scale_y = 1.0 - field_.fvdy;
        if (!(scale_x > 1.0e-14) || !(scale_y > 1.0e-14)) {
            return false;
        }
        // Boundary searches use physical normalized stop coordinates. Convert
        // them back to this pass's pupil coordinates so adaptive aiming sees
        // the complete candidate vignetting map, not a synthetic chief field.
        const PupilPoint pupil{
            (pupil_x - field_.fvcx) / scale_x,
            (pupil_y - field_.fvcy) / scale_y,
            1.0};

        // Direct and Paraxial deliberately define a launch/entrance-pupil
        // reference rather than a physical stop target.  Iterative and Robust
        // define the latter without an AimingSession.  Generate those rays in
        // their own coordinate systems; the surrounding grid and bisection,
        // not an imposed adaptive aim, finds their exact hard-aperture edge.
        if (!ray_aiming_uses_session(config_.aiming_mode)) {
            Ray ray = reference_tracer_.generate_ray_with_mode(
                field_, pupil, wave_.value_um, config_.aiming_mode);
            const bool unaimed_reference =
                config_.aiming_mode == RayAimingMode::Direct ||
                config_.aiming_mode == RayAimingMode::Paraxial;
            const bool usable = unaimed_reference
                                    ? ray.valid && ray.intensity > 0.0
                                    : strictly_aimed(ray);
            if (!usable) {
                return false;
            }
            *ray_out = ray;
            return true;
        }

        if (independent_apg_targets_) {
            Ray ray = generate_session_ray(
                pupil, independent_pupil_index_++);
            if (!strictly_aimed(ray)) {
                return false;
            }
            *ray_out = ray;
            return true;
        }


        if (config_.aiming_mode == RayAimingMode::CalibratedParaxial ||
            config_.aiming_mode == RayAimingMode::ThreeStep) {
            // These modes deliberately generate their calibrated/modelled ray
            // rather than an exactly corrected stop ray.  Preserve that
            // reference-pupil mapping.  The aperture-free reference system
            // above prevents the aperture under measurement from invalidating
            // the model itself; adaptive recovery is used only if the model
            // still cannot produce this target.
            Ray ray = generate_session_ray(
                pupil, independent_pupil_index_++);
            if (!ray.valid || ray.intensity <= 0.0 || !ray.aim_converged) {
                return false;
            }
            *ray_out = ray;
            return true;
        }

        if (!chief_ray_attempted_) {
            chief_ray_attempted_ = true;
            const PupilPoint chief{
                -field_.fvcx / scale_x,
                -field_.fvcy / scale_y,
                1.0};
            const Ray chief_ray = generate_session_ray(chief, 0);
            if (strictly_aimed(chief_ray)) {
                anchor_ray_ = chief_ray;
                anchor_pupil_ = chief;
                anchor_ready_ = true;
            }
        }
        if (!anchor_ready_) {
            // A vignetted off-axis field can have a valid transmitted pupil
            // that does not contain the physical stop center.  Requiring the
            // zero-pupil chief ray to converge would incorrectly reject the
            // whole field.  Solve sampled targets independently until the
            // first reachable point provides a branch-stable continuation
            // anchor for the remaining grid and boundary probes.
            Ray independent = generate_session_ray(
                pupil, independent_pupil_index_++);
            if (!strictly_aimed(independent)) {
                return false;
            }
            anchor_ray_ = independent;
            anchor_pupil_ = pupil;
            anchor_ready_ = true;
            *ray_out = independent;
            return true;
        }

        // The pupil is sampled in an order that is unrelated to optical
        // continuity.  Always continue from the same reachable anchor branch
        // toward the requested stop coordinate, otherwise a nearby sample from
        // a different real-ray solution can make the result scan-order dependent.
        Ray ray = anchor_ray_;
        const double delta_x = pupil.px - anchor_pupil_.px;
        const double delta_y = pupil.py - anchor_pupil_.py;
        const int configured_steps =
            std::max(1, system_.ray_aiming.continuation_steps);
        const int continuation_steps = std::max(
            1,
            static_cast<int>(std::ceil(
                std::hypot(delta_x, delta_y) *
                static_cast<double>(configured_steps))));
        for (int step = 1; step <= continuation_steps; ++step) {
            const double fraction =
                static_cast<double>(step) /
                static_cast<double>(continuation_steps);
            const PupilPoint intermediate{
                anchor_pupil_.px + delta_x * fraction,
                anchor_pupil_.py + delta_y * fraction,
                1.0};
            ray = reference_tracer_.solve_stop_aim_from_initial(
                field_,
                intermediate,
                wave_.value_um,
                ray,
                true,
                std::max(40, system_.ray_aiming.max_iter),
                true);
            if (!strictly_aimed(ray)) {
                // Continuation can cross an unreachable branch even when the
                // destination has its own valid real-ray solution.  An
                // independent exact solve is therefore a recovery path, not a
                // declaration that the point is physically vignetted.
                Ray independent = generate_session_ray(
                    pupil, independent_pupil_index_++);
                if (!strictly_aimed(independent)) {
                    return false;
                }
                *ray_out = independent;
                return true;
            }
        }
        if (!strictly_aimed(ray)) {
            return false;
        }
        *ray_out = ray;
        return true;
    }

Ray AutomaticVignettingFieldSolver::generate_session_ray(
        const PupilPoint& pupil,
        std::size_t pupil_index) {
        AimingSession& selected_aiming =
            shared_aiming_ ? *shared_aiming_ : aiming_;
        // This solver owns the physical aperture-envelope validation and its
        // connected-branch recovery. Do not validate the aperture-free aiming
        // reference as though it were the delivered optical system.
        Ray ray = selected_aiming.generate_for_trace(
            field_index_, wave_index_, pupil_index, field_, wave_, pupil);
        const bool model_mode =
            config_.aiming_mode == RayAimingMode::CalibratedParaxial ||
            config_.aiming_mode == RayAimingMode::ThreeStep;
        if ((model_mode && ray.valid && ray.intensity > 0.0 &&
             ray.aim_converged) ||
            (!model_mode && strictly_aimed(ray))) {
            return ray;
        }
        if (!config_.allow_aiming_fallback) {
            return ray;
        }
        // CalibratedParaxial and ThreeStep can require a traceable stop-centre
        // chief before constructing their fast model.  A shifted transmitted
        // pupil may not contain that point.  AdaptivePilotGrid is a numerical
        // recovery for the same physical stop target, so it preserves these
        // modes' reference-pupil coordinates while avoiding an all-field
        // failure caused solely by the unavailable model anchor.
        return exact_fallback_aiming_.generate_for_trace(
            field_index_, wave_index_, pupil_index, field_, wave_, pupil);
    }
}  // namespace analysis_detail

}  // namespace optiland
