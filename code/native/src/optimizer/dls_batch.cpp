/*
 * Run batched micro-DLS updates for multiple scaled candidates using shared population evaluation.
 * Adaptive polish supplies candidate rows/options and receives improvements in matching batch positions.
 * Preserve finite-difference column order, per-candidate damping and result-to-input row correspondence.
 */
#include "support.hpp"

namespace optiland {
using namespace optimizer_detail;

namespace optimizer_detail {

std::vector<PolishWorkResult> run_batched_micro_dls_scaled(
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const MeritConfig& merit_config,
    const std::vector<MicroDlsInput>& inputs,
    const PolishOptions& options,
    InternalScalingMode internal_mode,
    bool rollback,
    const ScoreBatchEvaluator& evaluate_scores) {
    struct State {
        int index = -1;
        double before_merit = std::numeric_limits<double>::infinity();
        std::vector<double> x;
        std::vector<double> best_merit_x;
        Score best_merit_score;
        std::vector<double> best_residuals;
        double best_sum = std::numeric_limits<double>::infinity();
        double lambda = 1.0e-2;
        int consecutive_rejections = 0;
        bool active = true;
    };
    struct DiffTag {
        std::size_t state = 0;
        int column = 0;
        double denominator = 1.0;
    };
    struct TrialTag {
        std::size_t state = 0;
        std::vector<double> trial;
        double previous_sum = std::numeric_limits<double>::infinity();
        double step_norm = 0.0;
    };

    std::vector<PolishWorkResult> out;
    out.reserve(inputs.size());
    const int dim = static_cast<int>(variables.size());
    if (inputs.empty() || dim <= 0) {
        return out;
    }

    auto scalar_merit_residual = [&](double merit) {
        if (std::isfinite(merit)) {
            return std::min(std::abs(merit), std::max(1.0, merit_config.fail_merit));
        }
        return std::max(1.0, merit_config.fail_merit);
    };
    const bool scalar_residual = polish_uses_batch_merit(options);
    auto residuals_for_score = [&](const Score& score, std::size_t expected) {
        std::vector<double> residuals;
        if (scalar_residual) {
            residuals.push_back(scalar_merit_residual(score.merit));
        } else {
            residuals = residuals_from_score(score, merit_config, options.residual_mode);
        }
        if (expected > 0 && residuals.size() != expected) {
            const double value = std::sqrt(
                std::max(1.0, merit_config.fail_merit) / static_cast<double>(expected));
            residuals.assign(expected, value);
        }
        return residuals;
    };

    std::vector<double> initial_rows;
    initial_rows.reserve(inputs.size() * static_cast<std::size_t>(dim));
    for (const auto& input : inputs) {
        std::vector<double> x = input.scaled;
        if (static_cast<int>(x.size()) != dim) {
            x.assign(static_cast<std::size_t>(dim), 0.0);
        }
        for (int j = 0; j < dim; ++j) {
            const auto& var = variables[static_cast<std::size_t>(j)];
            const auto [lo, hi] = internal_bounds(var, internal_mode);
            x[static_cast<std::size_t>(j)] =
                maybe_clip_continuous(var, x[static_cast<std::size_t>(j)], lo, hi);
        }
        initial_rows.insert(initial_rows.end(), x.begin(), x.end());
    }

    bool any_used_cuda = false;
    bool batch_used_cuda = false;
    const std::vector<Score> initial_scores =
        evaluate_scores(initial_rows, static_cast<int>(inputs.size()), &batch_used_cuda);
    any_used_cuda = any_used_cuda || batch_used_cuda;

    std::vector<State> states;
    states.reserve(inputs.size());
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        State state;
        state.index = inputs[i].index;
        state.before_merit = inputs[i].before_merit;
        state.x.assign(static_cast<std::size_t>(dim), 0.0);
        for (int j = 0; j < dim; ++j) {
            state.x[static_cast<std::size_t>(j)] =
                initial_rows[i * static_cast<std::size_t>(dim) + j];
        }
        state.best_merit_x = state.x;
        state.best_merit_score =
            i < initial_scores.size() ? initial_scores[i] : Score{};
        if (!std::isfinite(state.best_merit_score.merit)) {
            state.best_merit_score.merit = merit_config.fail_merit;
        }
        state.best_residuals = residuals_for_score(state.best_merit_score, 0);
        state.best_sum = sum_squares(state.best_residuals);
        state.lambda = options.initial_lambda;
        state.active = !state.best_residuals.empty();
        states.push_back(std::move(state));
    }

    const int max_rejected_steps = std::max(0, options.max_rejected_steps);
    for (int iter = 1; iter <= options.max_iterations; ++iter) {
        bool any_active = false;
        std::vector<double> diff_rows;
        std::vector<DiffTag> diff_tags;
        for (std::size_t s = 0; s < states.size(); ++s) {
            State& state = states[s];
            if (!state.active) {
                continue;
            }
            any_active = true;
            for (int j = 0; j < dim; ++j) {
                if (is_frozen_polish_variable(
                        variables[static_cast<std::size_t>(j)], options)) {
                    continue;
                }
                const auto [lo, hi] =
                    internal_bounds(variables[static_cast<std::size_t>(j)], internal_mode);
                double physical_h = 0.0;
                const double h = finite_difference_step_scaled(
                    base,
                    variables[static_cast<std::size_t>(j)],
                    state.x[static_cast<std::size_t>(j)],
                    lo,
                    hi,
                    options.finite_difference,
                    internal_mode,
                    &physical_h);
                if (options.finite_difference_diagnostics && iter == 1) {
                    const auto& var = variables[static_cast<std::size_t>(j)];
                    std::cerr << "batch DLS finite difference candidate " << state.index
                              << " var " << j
                              << " kind=" << variable_kind_name(var.kind)
                              << " physical="
                              << internal_to_physical(var, state.x[static_cast<std::size_t>(j)], internal_mode)
                              << " internal=" << state.x[static_cast<std::size_t>(j)]
                              << " physical_step=" << physical_h
                              << " internal_step=" << h << "\n";
                }
                const double xp_value = state.x[static_cast<std::size_t>(j)] + h;
                const double xm_value = state.x[static_cast<std::size_t>(j)] - h;
                if (xp_value == xm_value) {
                    continue;
                }
                const std::size_t row_offset = diff_rows.size();
                diff_rows.insert(diff_rows.end(), state.x.begin(), state.x.end());
                diff_rows.insert(diff_rows.end(), state.x.begin(), state.x.end());
                diff_rows[row_offset + static_cast<std::size_t>(j)] = xp_value;
                diff_rows[row_offset + static_cast<std::size_t>(dim + j)] = xm_value;
                diff_tags.push_back({s, j, xp_value - xm_value});
            }
        }
        if (!any_active || diff_tags.empty()) {
            break;
        }

        batch_used_cuda = false;
        const std::vector<Score> diff_scores =
            evaluate_scores(diff_rows, static_cast<int>(diff_tags.size() * 2), &batch_used_cuda);
        any_used_cuda = any_used_cuda || batch_used_cuda;

        std::vector<std::vector<double>> jacobians(states.size());
        for (std::size_t s = 0; s < states.size(); ++s) {
            const int residual_count = static_cast<int>(states[s].best_residuals.size());
            jacobians[s].assign(static_cast<std::size_t>(residual_count) * dim, 0.0);
        }
        for (std::size_t p = 0; p < diff_tags.size(); ++p) {
            const DiffTag& tag = diff_tags[p];
            if (tag.state >= states.size()) {
                continue;
            }
            State& state = states[tag.state];
            const int residual_count = static_cast<int>(state.best_residuals.size());
            if (residual_count <= 0 || 2 * p + 1 >= diff_scores.size()) {
                continue;
            }
            const auto rp = residuals_for_score(diff_scores[2 * p], state.best_residuals.size());
            const auto rm = residuals_for_score(diff_scores[2 * p + 1], state.best_residuals.size());
            for (int k = 0; k < residual_count; ++k) {
                const double base_r = state.best_residuals[static_cast<std::size_t>(k)];
                const double rp_value = jacobian_residual_sample(
                    rp[static_cast<std::size_t>(k)],
                    base_r,
                    merit_config);
                const double rm_value = jacobian_residual_sample(
                    rm[static_cast<std::size_t>(k)],
                    base_r,
                    merit_config);
                jacobians[tag.state][static_cast<std::size_t>(k) * dim + tag.column] =
                    (rp_value - rm_value) / tag.denominator;
            }
        }

        std::vector<double> trial_rows;
        std::vector<TrialTag> trial_tags;
        for (std::size_t s = 0; s < states.size(); ++s) {
            State& state = states[s];
            if (!state.active) {
                continue;
            }
            const int residual_count = static_cast<int>(state.best_residuals.size());
            if (residual_count <= 0) {
                state.active = false;
                continue;
            }
            std::vector<double> jtj(static_cast<std::size_t>(dim) * dim, 0.0);
            std::vector<double> gradient(static_cast<std::size_t>(dim), 0.0);
            const std::vector<double>& jacobian = jacobians[s];
            for (int k = 0; k < residual_count; ++k) {
                const double r = state.best_residuals[static_cast<std::size_t>(k)];
                for (int a = 0; a < dim; ++a) {
                    const double ja = jacobian[static_cast<std::size_t>(k) * dim + a];
                    gradient[static_cast<std::size_t>(a)] += ja * r;
                    for (int b = 0; b < dim; ++b) {
                        jtj[static_cast<std::size_t>(a) * dim + b] +=
                            ja * jacobian[static_cast<std::size_t>(k) * dim + b];
                    }
                }
            }

            std::vector<double> step;
            if (!solve_dls_linear_model(
                    jtj,
                    gradient,
                    state.x,
                    variables,
                    internal_mode,
                    options,
                    state.lambda,
                    dim,
                    &step,
                    nullptr,
                    &jacobian,
                    &state.best_residuals,
                    residual_count)) {
                state.lambda *= 4.0;
                ++state.consecutive_rejections;
                if (max_rejected_steps > 0 &&
                    state.consecutive_rejections >= max_rejected_steps) {
                    state.active = false;
                }
                continue;
            }

            std::vector<double> trial = state.x;
            double step_norm = 0.0;
            for (int j = 0; j < dim; ++j) {
                const auto [lo, hi] =
                    internal_bounds(variables[static_cast<std::size_t>(j)], internal_mode);
                if (is_frozen_polish_variable(
                        variables[static_cast<std::size_t>(j)], options)) {
                    trial[static_cast<std::size_t>(j)] = state.x[static_cast<std::size_t>(j)];
                    continue;
                }
                trial[static_cast<std::size_t>(j)] = maybe_clip_continuous(
                    variables[static_cast<std::size_t>(j)],
                    state.x[static_cast<std::size_t>(j)] + step[static_cast<std::size_t>(j)],
                    lo,
                    hi);
                const double actual_step =
                    trial[static_cast<std::size_t>(j)] - state.x[static_cast<std::size_t>(j)];
                step_norm += actual_step * actual_step;
            }
            trial_rows.insert(trial_rows.end(), trial.begin(), trial.end());
            trial_tags.push_back({s, std::move(trial), state.best_sum, step_norm});
        }
        if (trial_tags.empty()) {
            continue;
        }

        batch_used_cuda = false;
        const std::vector<Score> trial_scores =
            evaluate_scores(trial_rows, static_cast<int>(trial_tags.size()), &batch_used_cuda);
        any_used_cuda = any_used_cuda || batch_used_cuda;

        for (std::size_t t = 0; t < trial_tags.size(); ++t) {
            const TrialTag& tag = trial_tags[t];
            if (tag.state >= states.size() || t >= trial_scores.size()) {
                continue;
            }
            State& state = states[tag.state];
            const Score& trial_score = trial_scores[t];
            const bool trace_acceptable =
                polish_score_trace_acceptable(trial_score, merit_config, options);
            if (trace_acceptable &&
                std::isfinite(trial_score.merit) &&
                (!std::isfinite(state.best_merit_score.merit) ||
                 trial_score.merit <= state.best_merit_score.merit)) {
                state.best_merit_x = tag.trial;
                state.best_merit_score = trial_score;
            }
            std::vector<double> trial_residuals =
                residuals_for_score(trial_score, state.best_residuals.size());
            const double trial_sum = sum_squares(trial_residuals);
            if (trace_acceptable && std::isfinite(trial_sum) && trial_sum < state.best_sum) {
                state.x = tag.trial;
                state.best_residuals = std::move(trial_residuals);
                state.best_sum = trial_sum;
                state.lambda = std::max(1.0e-12, state.lambda * 0.35);
                state.consecutive_rejections = 0;
                if (std::sqrt(tag.step_norm) < options.min_step ||
                    std::abs(tag.previous_sum - state.best_sum) <=
                        options.tolerance * std::max(1.0, tag.previous_sum)) {
                    state.active = false;
                }
            } else {
                state.lambda *= 4.0;
                ++state.consecutive_rejections;
                if (max_rejected_steps > 0 &&
                    state.consecutive_rejections >= max_rejected_steps) {
                    state.active = false;
                }
            }
        }
    }

    for (const State& state : states) {
        PolishWorkResult result;
        result.index = state.index;
        result.before_merit = state.before_merit;
        result.polished_scaled = state.best_merit_x;
        result.score = state.best_merit_score;
        const double tolerance = 1.0e-12 * std::max(1.0, std::abs(state.before_merit));
        result.improved =
            std::isfinite(result.score.merit) &&
            (!rollback ||
             !std::isfinite(state.before_merit) ||
             result.score.merit <= state.before_merit + tolerance);
        result.used_cuda = any_used_cuda;
        out.push_back(std::move(result));
    }
    return out;
}

}  // namespace optimizer_detail

}  // namespace optiland
