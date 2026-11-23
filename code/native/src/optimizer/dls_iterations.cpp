/*
 * Drive DlsSession iterations through residual checks, linear-model updates and trial selection.
 * The session owns cached Jacobian state, damping/trust radius, plateau history and progress reporting.
 * Preserve stopping precedence, cache invalidation and callback timing relative to accepted steps.
 */
#include "dls_session.hpp"

namespace optiland { namespace optimizer_detail {

void DlsSession::iterate() {
for (int iter = 1; iter <= options.max_iterations; ++iter) {
        plateau_best_history.push_back(best_merit_score.merit);
        const int completed_iterations = iter - 1;
        if (plateau_early_stop_enabled &&
            completed_iterations >= plateau_min_iterations &&
            static_cast<int>(plateau_best_history.size()) > plateau_window) {
            const double window_start = plateau_best_history[
                plateau_best_history.size() - 1U -
                static_cast<std::size_t>(plateau_window)];
            const double window_end = plateau_best_history.back();
            if (std::isfinite(window_start) && std::isfinite(window_end)) {
                const double absolute_improvement =
                    std::max(0.0, window_start - window_end);
                const double relative_improvement =
                    absolute_improvement / std::max(1.0, std::abs(window_start));
                const bool absolute_small =
                    plateau_min_abs_improvement <= 0.0 ||
                    absolute_improvement <= plateau_min_abs_improvement;
                const bool relative_small =
                    plateau_min_rel_improvement <= 0.0 ||
                    relative_improvement <= plateau_min_rel_improvement;
                if (absolute_small && relative_small) {
                    std::cout << "DLS plateau early stop: iterations="
                              << completed_iterations
                              << " window=" << plateau_window
                              << " merit_start=" << window_start
                              << " merit_end=" << window_end
                              << " abs_improvement=" << absolute_improvement
                              << " rel_improvement=" << relative_improvement
                              << " abs_threshold="
                              << plateau_min_abs_improvement
                              << " rel_threshold="
                              << plateau_min_rel_improvement
                              << " saved_iterations="
                              << std::max(
                                     0,
                                     options.max_iterations - completed_iterations)
                              << "\n";
                    break;
                }
            }
        }
        ++polish_timing.dls_iterations;
        completed_polish_iterations = iter;
        const int residual_count = static_cast<int>(best_residuals.size());
        if (residual_count <= 0) {
            break;
        }
        update_linear_model(iter, residual_count);
        const std::vector<double>& jtj = cached_jtj;
        const std::vector<double>& gradient = cached_gradient;
        write_linear_model_diagnostics(iter, residual_count);

        std::vector<double> step;
        const auto linear_solve_start = Clock::now();
        int linear_model_solve_dim = dim;
        if (!solve_dls_linear_model(
                jtj,
                gradient,
                x,
                variables_,
                internal_mode,
                options,
                lambda,
                dim,
                &step,
                &linear_model_solve_dim,
                &cached_jacobian,
                &best_residuals,
                residual_count)) {
            polish_timing.dls_linear_solve_seconds += seconds_since(linear_solve_start);
            ++polish_timing.dls_rejected_steps;
            lambda *= 4.0;
            if (trust_region_polish) {
                trust_radius = std::max(trust_min_radius, 0.25 * trust_radius);
            }
            ++consecutive_rejections;
            if (max_rejected_steps > 0 && consecutive_rejections >= max_rejected_steps) {
                break;
            }
            continue;
        }
        polish_timing.dls_linear_solve_seconds += seconds_since(linear_solve_start);
        if (options.finite_difference_diagnostics && iter <= 5 &&
            (options.column_equilibration || options.active_set_bounds)) {
            std::cerr << "DLS linear solve diagnostics iter " << iter
                      << ": solve_dim=" << linear_model_solve_dim
                      << " full_dim=" << dim
                      << " column_equilibration="
                      << (options.column_equilibration ? "yes" : "no")
                      << " active_set="
                      << (options.active_set_bounds ? "yes" : "no")
                      << " projected_active_set="
                      << (options.projected_active_set_bounds ? "yes" : "no")
                      << " qr_solver="
                      << (options.qr_linear_solver ? "yes" : "no")
                      << "\n";
        }
        if (trust_region_polish) {
            const double raw_step_norm = vector_norm(step);
            if (raw_step_norm > trust_radius && raw_step_norm > 0.0) {
                const double scale = trust_radius / raw_step_norm;
                for (double& value : step) {
                    value *= scale;
                }
            }
        }

        std::vector<double> trial = x;
        std::vector<double> actual_step(static_cast<std::size_t>(dim), 0.0);
        double step_norm = 0.0;
        for (int j = 0; j < dim; ++j) {
            const auto [lo, hi] =
                internal_bounds(variables_[static_cast<std::size_t>(j)], internal_mode);
            if (is_frozen_polish_variable(
                    variables_[static_cast<std::size_t>(j)], options)) {
                trial[static_cast<std::size_t>(j)] = x[static_cast<std::size_t>(j)];
                continue;
            }
            trial[static_cast<std::size_t>(j)] = maybe_clip_continuous(
                variables_[static_cast<std::size_t>(j)],
                x[static_cast<std::size_t>(j)] + step[static_cast<std::size_t>(j)],
                lo,
                hi);
            const double actual_step_j =
                trial[static_cast<std::size_t>(j)] - x[static_cast<std::size_t>(j)];
            actual_step[static_cast<std::size_t>(j)] = actual_step_j;
            step_norm += actual_step_j * actual_step_j;
        }
        const double actual_step_norm = std::sqrt(step_norm);
        if (options.finite_difference_diagnostics && iter <= 5) {
            int clipped = 0;
            double intended_norm_sq = 0.0;
            for (int j = 0; j < dim; ++j) {
                const double intended = step[static_cast<std::size_t>(j)];
                const double actual = actual_step[static_cast<std::size_t>(j)];
                intended_norm_sq += intended * intended;
                if (std::abs(intended - actual) >
                    1.0e-10 * std::max(1.0, std::abs(intended))) {
                    ++clipped;
                }
            }
            std::cerr << "DLS step diagnostics iter " << iter
                      << ": lambda=" << lambda
                      << " trust_radius=" << trust_radius
                      << " intended_norm=" << std::sqrt(intended_norm_sq)
                      << " actual_norm=" << actual_step_norm
                      << " clipped=" << clipped
                      << "\n";
        }
        if (trust_region_polish && actual_step_norm < options.min_step) {
            ++consecutive_rejections;
            ++polish_timing.dls_rejected_steps;
            lambda = std::max(1.0e-12, 0.25 * lambda);
            trust_radius = std::clamp(
                std::max(4.0 * trust_radius, 16.0 * options.min_step),
                trust_min_radius,
                trust_max_radius);
            if (max_rejected_steps > 0 && consecutive_rejections >= max_rejected_steps) {
                break;
            }
            continue;
        }

        const double previous_best_merit = best_merit_score.merit;
        const double merit_tolerance =
            1.0e-12 * std::max(1.0, std::abs(previous_best_merit));
        const double previous_sum = best_sum;

        TrialEvaluation chosen_trial;
        const bool have_chosen_trial = select_trial(step, trial, actual_step, actual_step_norm, previous_best_merit, merit_tolerance, previous_sum, chosen_trial);

        bool accepted_step = false;
        double accepted_step_norm = have_chosen_trial
                                        ? chosen_trial.actual_step_norm
                                        : actual_step_norm;
        if (have_chosen_trial) {
            x = chosen_trial.trial;
            best = chosen_trial.score;
            best_residuals = std::move(chosen_trial.residuals);
            best_sum = chosen_trial.trial_sum;
            cached_linear_model_valid = false;
            if (trust_region_polish && chosen_trial.merit_accept_step) {
                trust_radius = std::max(trust_min_radius, 0.5 * trust_radius);
                lambda = std::min(1.0e12, lambda * 2.0);
            } else if (trust_region_polish) {
                if (chosen_trial.rho < 0.25) {
                    trust_radius = std::max(trust_min_radius, 0.25 * trust_radius);
                    lambda = std::min(1.0e12, lambda * 2.0);
                } else {
                    if (chosen_trial.rho > 0.75 &&
                        chosen_trial.actual_step_norm >= 0.8 * trust_radius) {
                        trust_radius = std::min(trust_max_radius, 2.0 * trust_radius);
                    }
                    lambda = std::max(
                        1.0e-12,
                        lambda * (chosen_trial.rho > 0.75 ? 0.35 : 0.7));
                }
            } else {
                lambda = std::max(
                    1.0e-12,
                    lambda * (chosen_trial.merit_accept_step ? 0.7 : 0.35));
            }
            accepted_step = true;
            consecutive_rejections = 0;
            ++polish_timing.dls_accepted_steps;
        } else {
            lambda *= 4.0;
            if (trust_region_polish) {
                trust_radius = std::max(trust_min_radius, 0.25 * trust_radius);
            }
            ++consecutive_rejections;
            ++polish_timing.dls_rejected_steps;
        }

        if (progress) {
            progress(iter, best_merit_score, scaled_to_physical(variables_, best_merit_x, internal_mode));
        }
        if (!accepted_step) {
            if (max_rejected_steps > 0 && consecutive_rejections >= max_rejected_steps) {
                break;
            }
            continue;
        }
        if (accepted_step_norm < options.min_step) {
            break;
        }
        const double residual_change = std::abs(previous_sum - best_sum);
        const double merit_change =
            (std::isfinite(previous_best_merit) && std::isfinite(best_merit_score.merit))
                ? std::abs(previous_best_merit - best_merit_score.merit)
                : std::numeric_limits<double>::infinity();
        if (residual_change <= options.tolerance * std::max(1.0, previous_sum) &&
            merit_change <=
                options.tolerance *
                    std::max(1.0, std::abs(std::isfinite(previous_best_merit)
                                                ? previous_best_merit
                                                : best_merit_score.merit))) {
            ++consecutive_small_accepted_steps;
            if (consecutive_small_accepted_steps >= 5) {
                break;
            }
        } else {
            consecutive_small_accepted_steps = 0;
        }
    }
}

} }  // namespace optiland::optimizer_detail
