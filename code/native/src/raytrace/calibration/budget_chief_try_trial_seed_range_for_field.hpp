#pragma once
#include "../diagnostics_inline.hpp"
#include "../detail.hpp"

namespace optiland::raytrace_detail {

struct BudgetChiefTryTrialSeedRangeForField {
    using ChiefAnchor = AimingSession::ChiefAnchor;
    using FirstSurfaceSeed = AimingSession::FirstSurfaceSeed;

    template <
        class Type_ChiefTargetParams,
        class Type_BudgetChiefSolveResult,
        class Type_BudgetChiefRejectReason,
        class Capture_chief,
        class Capture_abcd_seed_unusable,
        class Capture_budget_chief_ignore_clip,
        class Capture_limit,
        class Capture_fixed_budget_solver,
        class Capture_secant_budget_solver,
        class Capture_has_abcd_inverse,
        class Capture_stop_radius,
        class Capture_convergence_limit,
        class Capture_solve_budget_chief_fixed_or_secant
    >
    static auto run(
        AimingSession& session,
        const Wavelength& wave,
        RayAimingDiagnosticsState& diag,
        const CalibratedParaxialConfig& cfg,
        Capture_chief& chief,
        Capture_abcd_seed_unusable& abcd_seed_unusable,
        const int& iterations,
        Capture_budget_chief_ignore_clip& budget_chief_ignore_clip,
        Capture_limit& limit,
        Capture_fixed_budget_solver& fixed_budget_solver,
        Capture_secant_budget_solver& secant_budget_solver,
        Capture_has_abcd_inverse& has_abcd_inverse,
        Capture_stop_radius& stop_radius,
        Capture_convergence_limit& convergence_limit,
        Capture_solve_budget_chief_fixed_or_secant& solve_budget_chief_fixed_or_secant,
        const Field& solve_field,
        const Type_ChiefTargetParams& solve_target,
        const std::vector<FirstSurfaceSeed>& seeds,
        std::size_t begin,
        std::size_t end,
        Type_BudgetChiefRejectReason* reason,
        ChiefAnchor* solved_anchor
    ) {
        if (begin >= end) {
            if (reason) {
                *reason = Type_BudgetChiefRejectReason::EmptySeeds;
            }
            return false;
        }
        bool saw_traceable = false;
        bool saw_stop_oob = false;
        bool saw_nonfinite_residual = false;
        bool saw_residual_reject = false;
        for (std::size_t i = begin; i < end; ++i) {
            const FirstSurfaceSeed& trial_seed = seeds[i];
            add_diag(diag.calibrated_budget_chief_trial_rays);
            // Fixed/secant solving constructs and returns its own launch ray.
            // Generate the external initial ray only for paths that consume it.
            const bool uses_seed_corrector =
                iterations > 0 && has_abcd_inverse &&
                (fixed_budget_solver || secant_budget_solver);
            Ray ray;
            if (!uses_seed_corrector) {
                ray = session.tracer_.generate_ray_through_first_surface_point(
                    solve_field, wave.value_um, trial_seed.x, trial_seed.y, 1.0);
            }
            bool solved_stop_valid = false;
            double solved_stop_x = 0.0;
            double solved_stop_y = 0.0;
            if (iterations > 0) {
                add_diag(diag.calibrated_budget_chief_correction_calls);
                if (has_abcd_inverse && (fixed_budget_solver || secant_budget_solver)) {
                    const Type_BudgetChiefSolveResult solved =
                        solve_budget_chief_fixed_or_secant(
                            solve_field,
                            solve_target,
                            trial_seed,
                            secant_budget_solver);
                    ray = solved.ray;
                    solved_stop_valid = solved.stop_valid;
                    solved_stop_x = solved.stop_x;
                    solved_stop_y = solved.stop_y;
                } else {
                    ray = session.tracer_.solve_stop_aim_from_initial(
                        solve_field,
                        chief,
                        wave.value_um,
                        ray,
                        false,
                        iterations,
                        budget_chief_ignore_clip);
                }
                add_diag(diag.calibrated_budget_chief_correction_iterations,
                         std::max(0, ray.aim_iterations));
            }

            double sx = 0.0;
            double sy = 0.0;
            add_diag(diag.calibrated_budget_chief_stop_probe_calls);
            bool stop_ok = solved_stop_valid;
            if (solved_stop_valid) {
                sx = solved_stop_x;
                sy = solved_stop_y;
            } else {
                {
                    ScopedDiagTimer probe_timer(diag.calibrated_budget_chief_stop_probe_ns);
                    stop_ok = session.sample_stop_from_ray(
                        ray,
                        &sx,
                        &sy,
                        budget_chief_ignore_clip,
                        budget_chief_ignore_clip);
                }
            }
            if (!stop_ok) {
                continue;
            }
            add_diag(diag.calibrated_budget_chief_stop_probe_successes);
            saw_traceable = true;
            const double residual = std::hypot(sx - solve_target.x, sy - solve_target.y);
            if (!std::isfinite(residual)) {
                saw_nonfinite_residual = true;
                continue;
            }
            if (abcd_seed_unusable && residual > convergence_limit) {
                saw_residual_reject = true;
                continue;
            }
            if (residual > limit) {
                // Keep the default contract identical to CUDA: a
                // non-converged budget chief must remain within the configured
                // residual limit. Only explicitly named physical-stop modes
                // opt into the legacy inside-stop fallback. In particular,
                // historical "loose_stop" inputs use bounded residuals.
                const bool accept_inside_stop =
                    cfg.budget_chief_acceptance == "inside_stop" ||
                    cfg.budget_chief_acceptance == "stop_inside" ||
                    cfg.budget_chief_acceptance == "physical_stop";
                if (!accept_inside_stop) {
                    saw_residual_reject = true;
                    continue;
                }

                const double radial = std::hypot(sx, sy);
                if (!std::isfinite(radial) || radial > stop_radius() * (1.0 + 1.0e-10)) {
                    saw_stop_oob = true;
                    continue;
                }
                add_diag(diag.calibrated_budget_chief_inside_stop_accepted);
            }

            ChiefAnchor local_anchor;
            local_anchor.seed = session.seed_from_ray(ray);
            local_anchor.ray = ray;
            local_anchor.valid = local_anchor.seed.valid;
            if (!local_anchor.valid) {
                add_diag(diag.calibrated_budget_chief_seed_extract_fail);
                continue;
            }
            local_anchor.stop_valid = true;
            local_anchor.stop_x = sx;
            local_anchor.stop_y = sy;
            local_anchor.stop_residual = residual;
            add_diag_residual(
                diag.calibrated_budget_chief_residual_count,
                diag.calibrated_budget_chief_residual_sum_scaled,
                diag.calibrated_budget_chief_residual_max_scaled,
                residual);
            add_diag(diag.calibrated_budget_chief_accepted);
            if (solved_anchor) {
                *solved_anchor = local_anchor;
            }
            return true;
        }

        if (reason) {
            if (!saw_traceable) {
                *reason = Type_BudgetChiefRejectReason::TraceFail;
            } else if (saw_stop_oob) {
                *reason = Type_BudgetChiefRejectReason::StopOutOfBounds;
            } else if (saw_nonfinite_residual || saw_residual_reject) {
                *reason = Type_BudgetChiefRejectReason::ResidualReject;
            } else {
                *reason = Type_BudgetChiefRejectReason::SeedExtractFail;
            }
        }
        return false;
    }
};

} // namespace optiland::raytrace_detail
