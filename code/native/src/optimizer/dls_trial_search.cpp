/*
 * Project and evaluate proposed DlsSession steps under damping or trust-region acceptance.
 * The iteration loop supplies the cached linear model; trials update session incumbents and rejection state.
 * Preserve trial-scale order and separate residual acceptance from best-merit publication decisions.
 */
#include "dls_session.hpp"

namespace optiland { namespace optimizer_detail {

double DlsSession::project_trial(double alpha, const std::vector<double>& step,
                                        std::vector<double>* trial_out,
                                        std::vector<double>* actual_step_out) {
            trial_out->assign(x.begin(), x.end());
            actual_step_out->assign(static_cast<std::size_t>(dim), 0.0);
            double norm_sq = 0.0;
            for (int j = 0; j < dim; ++j) {
                const auto [lo, hi] =
                    internal_bounds(variables_[static_cast<std::size_t>(j)], internal_mode);
                if (is_frozen_polish_variable(
                        variables_[static_cast<std::size_t>(j)], options)) {
                    continue;
                }
                (*trial_out)[static_cast<std::size_t>(j)] = maybe_clip_continuous(
                    variables_[static_cast<std::size_t>(j)],
                    x[static_cast<std::size_t>(j)] +
                        alpha * step[static_cast<std::size_t>(j)],
                    lo,
                    hi);
                const double actual =
                    (*trial_out)[static_cast<std::size_t>(j)] -
                    x[static_cast<std::size_t>(j)];
                (*actual_step_out)[static_cast<std::size_t>(j)] = actual;
                norm_sq += actual * actual;
            }
            return std::sqrt(norm_sq);
        }

bool DlsSession::select_trial(const std::vector<double>& step, const std::vector<double>& trial, const std::vector<double>& actual_step, double actual_step_norm, double previous_best_merit, double merit_tolerance, double previous_sum, TrialEvaluation& chosen_trial) {
    const auto& jtj = cached_jtj;
    const auto& gradient = cached_gradient;
std::vector<double> alphas{1.0};
        const int step_search_levels = std::max(0, options.step_search_levels);
        const double step_search_shrink =
            std::clamp(options.step_search_shrink, 1.0e-3, 0.99);
        double alpha = 1.0;
        for (int i = 0; i < step_search_levels; ++i) {
            alpha *= step_search_shrink;
            alphas.push_back(alpha);
        }

        
        bool have_chosen_trial = false;
        for (std::size_t ai = 0; ai < alphas.size(); ++ai) {
            TrialEvaluation current;
            if (ai == 0) {
                current.trial = trial;
                current.actual_step = actual_step;
                current.actual_step_norm = actual_step_norm;
            } else {
                current.actual_step_norm =
                    project_trial(alphas[ai], step, &current.trial, &current.actual_step);
                if (current.actual_step_norm < options.min_step) {
                    continue;
                }
            }

            const auto trial_score_start = Clock::now();
            current.score = score_for_scaled(current.trial);
            polish_timing.dls_trial_score_seconds += seconds_since(trial_score_start);
            ++polish_timing.dls_trial_evaluations;
            const bool trace_acceptable =
                polish_score_trace_acceptable(current.score, merit_config_, options);
            auto auto_floor_ok = [](double value, double floor) {
                return !std::isfinite(floor) ||
                       (std::isfinite(value) && value >= floor);
            };
            auto auto_ceiling_ok = [](double value, double ceiling) {
                return !std::isfinite(ceiling) ||
                       (std::isfinite(value) && value <= ceiling);
            };
            const bool auto_health_acceptable =
                !options.auto_tune ||
                (auto_floor_ok(current.score.constraint_gate, auto_constraint_gate_floor) &&
                 auto_floor_ok(current.score.first_order_gate, auto_first_order_gate_floor) &&
                 auto_ceiling_ok(
                     current.score.max_single_ray_spot_ratio,
                     auto_spot_ratio_ceiling) &&
                 auto_ceiling_ok(
                     current.score.max_single_ray_wavefront_ratio,
                     auto_wavefront_ratio_ceiling) &&
                 auto_ceiling_ok(
                     current.score.max_single_ray_spot,
                     auto_spot_absolute_ceiling) &&
                 auto_ceiling_ok(
                     current.score.max_single_ray_wavefront,
                     auto_wavefront_absolute_ceiling));
            const bool trial_merit_not_worse =
                trace_acceptable && auto_health_acceptable &&
                std::isfinite(current.score.merit) &&
                (!std::isfinite(previous_best_merit) ||
                 current.score.merit <= previous_best_merit + merit_tolerance);
            current.trial_merit_improved =
                trace_acceptable && auto_health_acceptable &&
                std::isfinite(current.score.merit) &&
                (!std::isfinite(previous_best_merit) ||
                 current.score.merit + merit_tolerance < previous_best_merit);
            if (trial_merit_not_worse &&
                (!std::isfinite(best_merit_score.merit) ||
                 current.score.merit <= best_merit_score.merit + merit_tolerance)) {
                best_merit_x = current.trial;
                best_merit_score = current.score;
            }

            const auto trial_residual_start = Clock::now();
            current.residuals =
                residuals_for_score(current.score, best_residuals.size());
            if (current.residuals.size() != best_residuals.size()) {
                current.residuals =
                    residuals_for_scaled(current.trial, best_residuals.size());
                ++polish_timing.dls_trial_evaluations;
            }
            polish_timing.dls_trial_residual_seconds += seconds_since(trial_residual_start);
            current.trial_sum = sum_squares(current.residuals);
            current.actual_reduction = previous_sum - current.trial_sum;
            current.predicted =
                trust_region_polish
                    ? predicted_reduction(current.actual_step, gradient, jtj)
                    : std::numeric_limits<double>::infinity();
            current.rho =
                (trust_region_polish &&
                 current.predicted > 0.0 &&
                 std::isfinite(current.predicted))
                    ? current.actual_reduction / current.predicted
                    : -std::numeric_limits<double>::infinity();
            current.residual_accept_step =
                trust_region_polish
                    ? (trace_acceptable &&
                       auto_health_acceptable &&
                       std::isfinite(current.trial_sum) &&
                       std::isfinite(current.actual_reduction) &&
                       current.actual_reduction > 0.0 &&
                       current.predicted > 0.0 &&
                       std::isfinite(current.rho) &&
                       current.rho >= trust_acceptance_ratio)
                    : (trace_acceptable &&
                       auto_health_acceptable &&
                       current.trial_sum < best_sum &&
                       std::isfinite(current.trial_sum));
            current.merit_accept_step =
                !current.residual_accept_step &&
                current.trial_merit_improved &&
                std::isfinite(current.trial_sum);
            if (!current.residual_accept_step && !current.merit_accept_step) {
                continue;
            }

            bool prefer_current = !have_chosen_trial;
            if (have_chosen_trial &&
                current.trial_merit_improved != chosen_trial.trial_merit_improved) {
                prefer_current = current.trial_merit_improved;
            } else if (have_chosen_trial && current.trial_merit_improved) {
                prefer_current = current.score.merit < chosen_trial.score.merit;
            } else if (have_chosen_trial) {
                prefer_current = current.trial_sum < chosen_trial.trial_sum;
            }
            if (prefer_current) {
                chosen_trial = std::move(current);
                have_chosen_trial = true;
            }
        }
    return have_chosen_trial;
}

} }  // namespace optiland::optimizer_detail
