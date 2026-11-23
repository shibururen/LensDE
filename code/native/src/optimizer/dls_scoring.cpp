/*
 * Evaluate DlsSession trial coordinates and map their scores into the selected residual representation.
 * Initialization, Jacobian and trial search share solver inputs, merit caches and predicted-reduction rules.
 * Preserve coordinate projection, residual sizing and guard-ceiling interpretation for every trial.
 */
#include "dls_session.hpp"

namespace optiland { namespace optimizer_detail {

Score DlsSession::score_for_physical(const std::vector<double>& values) {
        return evaluate_merit(
            prepare_for_merit(apply_mixed_variables(base_, variables_, values, glass_search_), merit_config_),
            merit_config_);
    }

Score DlsSession::score_for_scaled(const std::vector<double>& scaled_values) {
        return score_for_physical(scaled_to_physical(variables_, scaled_values, internal_mode));
    }

double DlsSession::scalar_merit_residual(double merit) {
        if (std::isfinite(merit)) {
            return std::min(std::abs(merit), std::max(1.0, merit_config_.fail_merit));
        }
        return std::max(1.0, merit_config_.fail_merit);
    }

std::vector<double> DlsSession::residuals_for_score(const Score& score, std::size_t expected) {
        std::vector<double> residuals;
        if (batch_merit_polish) {
            residuals.push_back(scalar_merit_residual(score.merit));
            return residuals;
        }
        residuals = residuals_from_score(score, merit_config_, options.residual_mode);
        if (expected > 0 && residuals.size() != expected) {
            if (options.finite_difference_diagnostics) {
                std::cerr << "DLS residual count mismatch: expected=" << expected
                          << " got=" << residuals.size()
                          << " merit=" << score.merit
                          << " trace_ok=" << (score.trace_ok ? 1 : 0)
                          << " message=" << score.message
                          << "\n";
            }
            const double value = std::sqrt(
                std::max(1.0, merit_config_.fail_merit) / static_cast<double>(expected));
            residuals.assign(expected, value);
        }
        return residuals;
    }

std::vector<double> DlsSession::residuals_for_scaled(const std::vector<double>& scaled_values, std::size_t expected) {
        const Score score = score_for_scaled(scaled_values);
        return residuals_for_score(score, expected);
    }

double DlsSession::vector_norm(const std::vector<double>& values) {
        double sum = 0.0;
        for (double value : values) {
            sum += value * value;
        }
        return std::sqrt(sum);
    }

double DlsSession::predicted_reduction(const std::vector<double>& actual_step,
                                   const std::vector<double>& gradient,
                                   const std::vector<double>& jtj) {
        double linear = 0.0;
        double quadratic = 0.0;
        for (int a = 0; a < dim; ++a) {
            const double sa = actual_step[static_cast<std::size_t>(a)];
            linear += gradient[static_cast<std::size_t>(a)] * sa;
            for (int b = 0; b < dim; ++b) {
                quadratic +=
                    sa * jtj[static_cast<std::size_t>(a) * dim + b] *
                    actual_step[static_cast<std::size_t>(b)];
            }
        }
        return -(2.0 * linear + quadratic);
    }

double DlsSession::auto_ratio_ceiling(double initial_ratio) {
        return options.auto_tune && std::isfinite(initial_ratio) && initial_ratio > 0.0
                   ? std::max(1.5 * initial_ratio, initial_ratio + 1.0)
                   : std::numeric_limits<double>::infinity();
    }

double DlsSession::auto_absolute_ceiling(double initial_value) {
        return options.auto_tune && std::isfinite(initial_value) && initial_value > 0.0
                   ? 1.25 * initial_value
                   : std::numeric_limits<double>::infinity();
    }

} }  // namespace optiland::optimizer_detail
