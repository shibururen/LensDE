/*
 * Tests physical aperture feasibility and continues/recovers an aimed pupil branch.
 * The vignetting grid and axis solvers use these scalar and multi-wavelength probes.
 * Every selected wavelength must accept the same pupil point; continuation seeds and
 * exact fallback retain the limiting-surface and maximum-boundary-ratio diagnostics.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
AutomaticVignettingFieldSolver::Sample AutomaticVignettingFieldSolver::evaluate(double pupil_x, double pupil_y) {
        Sample out = evaluate_single(pupil_x, pupil_y);
        for (const auto& solver : additional_wave_solvers_) {
            const Sample sample = solver->evaluate_single(pupil_x, pupil_y);
            out.aimed = out.aimed && sample.aimed;
            if (!sample.feasible ||
                sample.max_boundary_ratio > out.max_boundary_ratio) {
                out.max_boundary_ratio = sample.max_boundary_ratio;
                out.limiting_surface = sample.limiting_surface;
            }
            out.feasible = out.feasible && sample.feasible;
        }
        return out;
    }

AutomaticVignettingFieldSolver::Sample AutomaticVignettingFieldSolver::continue_branch(const BranchSeed& seed, double x, double y) {
        Ray ray = seed.ray;
        const double distance = std::hypot(x - seed.x, y - seed.y);
        const int steps = std::max(1, static_cast<int>(std::ceil(
            distance * std::max(16, system_.ray_aiming.continuation_steps))));
        if (steps > 1) {
            // Usually one local corrector reaches the requested target. Keep
            // continuation as a fallback rather than paying for every small
            // step in the healthy bulk of the pupil grid.
            Vec3 first;
            if (reference_tracer_.first_surface_point(ray, &first)) {
                const PupilPoint pupil{(x - field_.fvcx) / (1.0 - field_.fvdx),
                                       (y - field_.fvcy) / (1.0 - field_.fvdy), 1.0};
                const Ray direct = reference_tracer_.solve_stop_aim_from_initial(
                    field_, pupil, wave_.value_um, ray, true,
                    std::max(40, system_.ray_aiming.max_iter), true,
                    true, first.x, first.y);
                const Sample sample = check_branch_ray(direct, x, y);
                if (sample.feasible) return sample;
            }
        }
        // For a polychromatic measurement the common grid stores the primary
        // ray. First correct that seed at this solver's own wavelength.
        const int first_step = ray.wavelength_um == wave_.value_um ? 1 : 0;
        for (int step = first_step; step <= steps; ++step) {
            const double fraction = static_cast<double>(step) / steps;
            const double px = seed.x + (x - seed.x) * fraction;
            const double py = seed.y + (y - seed.y) * fraction;
            Vec3 first;
            if (!reference_tracer_.first_surface_point(ray, &first)) return {};
            const PupilPoint pupil{
                (px - field_.fvcx) / (1.0 - field_.fvdx),
                (py - field_.fvcy) / (1.0 - field_.fvdy), 1.0};
            ray = reference_tracer_.solve_stop_aim_from_initial(
                field_, pupil, wave_.value_um, ray, true,
                std::max(40, system_.ray_aiming.max_iter), true,
                true, first.x, first.y);
            if (!strictly_aimed(ray)) return {};
        }
        // The fixed first-surface coordinates preserve the inverse branch.
        // Only the requested endpoint needs an aperture-envelope trace; grid
        // connectivity and inside/outside bisection determine its component.
        return check_branch_ray(ray, x, y);
    }

AutomaticVignettingFieldSolver::Sample AutomaticVignettingFieldSolver::recover_branch(double x, double y) {
        // Fixed-size nearest selection avoids allocating/sorting the grid for
        // every boundary probe. The bank is frozen for scan-order independence.
        std::array<std::pair<double, std::size_t>, 4> nearest;
        nearest.fill({std::numeric_limits<double>::infinity(), 0});
        for (std::size_t i = 0; i < branch_seeds_.size(); ++i) {
            const double dx = x - branch_seeds_[i].x;
            const double dy = y - branch_seeds_[i].y;
            std::pair<double, std::size_t> candidate{dx * dx + dy * dy, i};
            for (auto& slot : nearest) {
                if (candidate < slot) std::swap(candidate, slot);
            }
        }
        Sample last;
        for (const auto& candidate : nearest) {
            if (!std::isfinite(candidate.first)) break;
            last = continue_branch(branch_seeds_[candidate.second], x, y);
            if (last.feasible) return last;
        }
        return last;
    }

AutomaticVignettingFieldSolver::Sample AutomaticVignettingFieldSolver::recover_physical_seed(double x, double y) {
        // The aperture-free aiming reference can converge strictly to a
        // blocked inverse solution. Check a rejected component seed against
        // the delivered system before declaring that component unreachable.
        // This bounded bootstrap runs only after the ordinary seed fails.
        AimingSession physical_aiming(system_);
        const PupilPoint pupil{(x - field_.fvcx) / (1.0 - field_.fvdx),
                               (y - field_.fvcy) / (1.0 - field_.fvdy), 1.0};
        const Ray ray = physical_aiming.generate(
            field_index_, wave_index_, 0, field_, wave_, pupil);
        ++pupil_evaluations_;
        Sample sample = check_branch_ray(ray, x, y);
        if (sample.feasible) return sample;

        // An APG chief/pilot failure can leave the physical session without
        // any branch at all. Bootstrap only this failed SV seed using bounded
        // first-surface starts; accept only the original strict physical test.
        const std::size_t first = system_.first_ray_aiming_surface(wave_.value_um);
        if (first >= system_.surfaces.size()) return sample;
        const auto& surface = system_.surfaces[first];
        const double radius = surface.aperture_radius > 0.0
                                  ? surface.aperture_radius
                                  : (surface.clear_aperture_fixed
                                         ? surface.semi_aperture : 0.0);
        if (!(radius > 0.0) || !std::isfinite(radius)) return sample;
        const double field_norm = std::hypot(field_.hx, field_.hy);
        const double ux = field_norm > 1.0e-12 ? field_.hx / field_norm : 0.0;
        const double uy = field_norm > 1.0e-12 ? field_.hy / field_norm : 1.0;
        for (int index = -32; index <= 32; ++index) {
            const double along = radius * index / 32.0;
            for (double fraction : {0.0, 0.125, -0.125, 0.375, -0.375}) {
                const double across = radius * fraction;
                if (std::hypot(along, across) > radius) continue;
                const double sx = along * ux - across * uy;
                const double sy = along * uy + across * ux;
                const Ray seed = tracer_.generate_ray_through_first_surface_point(
                    field_, wave_.value_um, sx, sy);
                const Ray recovered = tracer_.solve_stop_aim_from_initial(
                    field_, pupil, wave_.value_um, seed, true,
                    std::max(40, system_.ray_aiming.max_iter), true,
                    true, sx, sy);
                ++pupil_evaluations_;
                sample = check_branch_ray(recovered, x, y);
                if (sample.feasible) return sample;
            }
        }
        return sample;
    }

bool AutomaticVignettingFieldSolver::recover_axis_seed(double x, double y) {
        ++branch_reseed_attempts_;
        Sample primary = recover_physical_seed(x, y);
        if (!primary.feasible) return false;

        std::vector<Sample> additional;
        additional.reserve(additional_wave_solvers_.size());
        for (const auto& solver : additional_wave_solvers_) {
            Sample recovered = solver->recover_physical_seed(x, y);
            if (!recovered.feasible) return false;
            additional.push_back(std::move(recovered));
        }

        branch_seeds_.push_back({x, y, primary.ray});
        branch_recovery_enabled_ = true;
        batch_apg_worker_pool_.clear();
        for (std::size_t index = 0; index < additional_wave_solvers_.size(); ++index) {
            auto& solver = additional_wave_solvers_[index];
            solver->branch_seeds_.push_back({x, y, additional[index].ray});
            solver->branch_recovery_enabled_ = true;
            solver->batch_apg_worker_pool_.clear();
        }
        ++branch_reseed_successes_;
        return true;
    }

AutomaticVignettingFieldSolver::Sample AutomaticVignettingFieldSolver::evaluate_from_inside(double x, double y, const BranchSeed& inside) {
        if (!branch_recovery_enabled_) return evaluate(x, y);
        Sample sample = continue_branch(inside, x, y);
        for (const auto& solver : additional_wave_solvers_) {
            const Sample other = solver->continue_branch(inside, x, y);
            sample.feasible = sample.feasible && other.feasible;
            sample.aimed = sample.aimed && other.aimed;
            if (!other.feasible || other.max_boundary_ratio > sample.max_boundary_ratio) {
                sample.max_boundary_ratio = other.max_boundary_ratio;
                sample.limiting_surface = other.limiting_surface;
            }
        }
        return sample;
    }

AutomaticVignettingFieldSolver::Sample AutomaticVignettingFieldSolver::check_branch_ray(const Ray& ray, double pupil_x, double pupil_y) {
        Sample out;
        out.max_boundary_ratio = std::hypot(pupil_x, pupil_y);
        const std::size_t stop_index = tracer_.stop_surface_index();
        out.limiting_surface = stop_index < system_.surfaces.size()
                                   ? static_cast<int>(stop_index) : -1;
        if ((config_.aiming_mode == RayAimingMode::AdaptivePilotGrid &&
             !strictly_aimed(ray)) ||
            out.max_boundary_ratio > 1.0 + std::max(0.0, config_.pupil_tolerance)) {
            return out;
        }
        out.aimed = true;
        out.ray = ray;

        const double aperture_tolerance =
            std::max(0.0, config_.aperture_tolerance_fraction);
        if (!hard_apertures_.empty()) {
            const auto envelope_started = std::chrono::steady_clock::now();
            ++envelope_trace_calls_;
            tracer_.trace_unclipped_to_surface_history(
                ray,
                hard_apertures_.back().first,
                &hard_aperture_history_);
            envelope_trace_seconds_ += std::chrono::duration<double>(
                std::chrono::steady_clock::now() - envelope_started).count();
        }
        for (const auto& hard_aperture : hard_apertures_) {
            ++hard_aperture_surface_tests_;
            const std::size_t surface_index = hard_aperture.first;
            const double hard_radius = hard_aperture.second;
            if (surface_index >= hard_aperture_history_.size()) {
                out.limiting_surface = static_cast<int>(surface_index);
                return out;
            }
            const TracePoint& hit = hard_aperture_history_[surface_index];
            if (!hit.valid || !std::isfinite(hit.x) || !std::isfinite(hit.y)) {
                out.limiting_surface = static_cast<int>(surface_index);
                return out;
            }
            const double ratio = std::hypot(hit.x, hit.y) / hard_radius;
            if (ratio > out.max_boundary_ratio) {
                out.max_boundary_ratio = ratio;
                out.limiting_surface = static_cast<int>(surface_index);
            }
            if (ratio > 1.0 + aperture_tolerance) {
                return out;
            }
        }

        if (config_.require_full_trace) {
            Ray full_trace = ray;
            if (!tracer_.trace_ray(full_trace)) {
                out.limiting_surface = full_trace.failure_surface;
                return out;
            }
        }
        out.feasible = true;
        return out;
    }

AutomaticVignettingFieldSolver::Sample AutomaticVignettingFieldSolver::evaluate_single(double pupil_x, double pupil_y) {
        if (branch_recovery_enabled_) {
            ++pupil_evaluations_;
            return recover_branch(pupil_x, pupil_y);
        }
        Sample out;
        ++pupil_evaluations_;
        if (!std::isfinite(pupil_x) || !std::isfinite(pupil_y)) {
            return out;
        }
        const double pupil_radius = std::hypot(pupil_x, pupil_y);
        const std::size_t stop_index = tracer_.stop_surface_index();
        out.max_boundary_ratio = pupil_radius;
        out.limiting_surface = stop_index < system_.surfaces.size()
                                   ? static_cast<int>(stop_index)
                                   : -1;
        if (pupil_radius > 1.0 + std::max(0.0, config_.pupil_tolerance)) {
            return out;
        }

        Ray ray;
        const auto aiming_started = std::chrono::steady_clock::now();
        const bool aimed = aim_target(pupil_x, pupil_y, &ray);
        aiming_seconds_ += std::chrono::duration<double>(
            std::chrono::steady_clock::now() - aiming_started).count();
        if (!aimed) {
            out.limiting_surface = -2;
            return out;
        }
        return check_branch_ray(ray, pupil_x, pupil_y);
    }
}  // namespace analysis_detail

}  // namespace optiland
