/*
 * Construct or reuse the DlsSession finite-difference Jacobian and its normal-equation model.
 * The iteration controller supplies residual dimensions; scoring uses the same cached variable ordering.
 * Preserve perturbation, evaluation and matrix-accumulation order when updating the linear model.
 */
#include "dls_session.hpp"

namespace optiland { namespace optimizer_detail {

void DlsSession::update_linear_model(int iter, int residual_count) {
if (!cached_linear_model_valid || cached_residual_count != residual_count) {
            cached_jacobian.assign(static_cast<std::size_t>(residual_count) * dim, 0.0);
            std::vector<double>& jacobian = cached_jacobian;
            int diagnostic_large_jacobian_reports = 0;

            const auto jacobian_start = Clock::now();
            if (batch_merit_polish) {
                std::vector<double> diff_scaled_population;
                std::vector<int> diff_columns;
                std::vector<double> diff_denominators;
                diff_scaled_population.reserve(static_cast<std::size_t>(2 * dim) * dim);
                diff_columns.reserve(dim);
                diff_denominators.reserve(dim);
                for (int j = 0; j < dim; ++j) {
                    if (is_frozen_polish_variable(
                            variables_[static_cast<std::size_t>(j)], options)) {
                        continue;
                    }
                    const auto [lo, hi] =
                        internal_bounds(variables_[static_cast<std::size_t>(j)], internal_mode);
                    double physical_h = 0.0;
                    const double h = finite_difference_step_scaled(
                        base_,
                        variables_[static_cast<std::size_t>(j)],
                        x[static_cast<std::size_t>(j)],
                        lo,
                        hi,
                        options.finite_difference,
                        internal_mode,
                        &physical_h);
                    if (options.finite_difference_diagnostics && iter == 1) {
                        const auto& var = variables_[static_cast<std::size_t>(j)];
                        std::cerr << "DLS finite difference var " << j
                                  << " kind=" << variable_kind_name(var.kind)
                                  << " physical="
                                  << internal_to_physical(
                                         var,
                                         x[static_cast<std::size_t>(j)],
                                         internal_mode)
                                  << " internal=" << x[static_cast<std::size_t>(j)]
                                  << " physical_step=" << physical_h
                                  << " internal_step=" << h << "\n";
                    }
                    const double xp_value = x[static_cast<std::size_t>(j)] + h;
                    const double xm_value = x[static_cast<std::size_t>(j)] - h;
                    if (xp_value == xm_value) {
                        continue;
                    }
                    const std::size_t row_offset = diff_scaled_population.size();
                    diff_scaled_population.insert(
                        diff_scaled_population.end(),
                        x.begin(),
                        x.end());
                    diff_scaled_population.insert(
                        diff_scaled_population.end(),
                        x.begin(),
                        x.end());
                    diff_scaled_population[row_offset + static_cast<std::size_t>(j)] = xp_value;
                    diff_scaled_population[row_offset + static_cast<std::size_t>(dim + j)] =
                        xm_value;
                    diff_columns.push_back(j);
                    diff_denominators.push_back(xp_value - xm_value);
                }
                if (!diff_columns.empty()) {
                    const int candidate_count = static_cast<int>(diff_columns.size()) * 2;
                    polish_timing.dls_jacobian_candidates += candidate_count;
                    const std::vector<double> diff_physical_population =
                        scaled_population_to_physical_flat(
                            variables_,
                            diff_scaled_population,
                            candidate_count,
                            internal_mode);
                    bool jacobian_used_cuda = false;
                    const std::vector<double> merits = evaluate_population(
                        diff_physical_population,
                        candidate_count,
                        options.use_cuda,
                        &jacobian_used_cuda,
                        &polish_timing);
                    polish_used_cuda = polish_used_cuda || jacobian_used_cuda;
                    for (std::size_t p = 0; p < diff_columns.size(); ++p) {
                        const int j = diff_columns[p];
                        const double merit_p = merits[2 * p];
                        const double merit_m = merits[2 * p + 1];
                        const double rp = scalar_merit_residual(merit_p);
                        const double rm = scalar_merit_residual(merit_m);
                        jacobian[static_cast<std::size_t>(j)] =
                            (rp - rm) / diff_denominators[p];
                    }
                }
            } else {
                std::vector<double> diff_scaled_population;
                std::vector<int> diff_columns;
                std::vector<double> diff_denominators;
                diff_scaled_population.reserve(static_cast<std::size_t>(2 * dim) * dim);
                diff_columns.reserve(dim);
                diff_denominators.reserve(dim);
                for (int j = 0; j < dim; ++j) {
                    if (is_frozen_polish_variable(
                            variables_[static_cast<std::size_t>(j)], options)) {
                        continue;
                    }
                    const auto [lo, hi] =
                        internal_bounds(variables_[static_cast<std::size_t>(j)], internal_mode);
                    double physical_h = 0.0;
                    const double h = finite_difference_step_scaled(
                        base_,
                        variables_[static_cast<std::size_t>(j)],
                        x[static_cast<std::size_t>(j)],
                        lo,
                        hi,
                        options.finite_difference,
                        internal_mode,
                        &physical_h);
                    if (options.finite_difference_diagnostics && iter == 1) {
                        const auto& var = variables_[static_cast<std::size_t>(j)];
                        std::cerr << "DLS finite difference var " << j
                                  << " kind=" << variable_kind_name(var.kind)
                                  << " physical="
                                  << internal_to_physical(
                                         var,
                                         x[static_cast<std::size_t>(j)],
                                         internal_mode)
                                  << " internal=" << x[static_cast<std::size_t>(j)]
                                  << " physical_step=" << physical_h
                                  << " internal_step=" << h << "\n";
                    }
                    const double xp_value = x[static_cast<std::size_t>(j)] + h;
                    const double xm_value = x[static_cast<std::size_t>(j)] - h;
                    if (xp_value == xm_value) {
                        continue;
                    }
                    const std::size_t row_offset = diff_scaled_population.size();
                    diff_scaled_population.insert(
                        diff_scaled_population.end(),
                        x.begin(),
                        x.end());
                    diff_scaled_population.insert(
                        diff_scaled_population.end(),
                        x.begin(),
                        x.end());
                    diff_scaled_population[row_offset + static_cast<std::size_t>(j)] = xp_value;
                    diff_scaled_population[row_offset + static_cast<std::size_t>(dim + j)] =
                        xm_value;
                    diff_columns.push_back(j);
                    diff_denominators.push_back(xp_value - xm_value);
                }
                if (!diff_columns.empty()) {
                    polish_timing.dls_jacobian_candidates +=
                        static_cast<int>(diff_columns.size()) * 2;
                    bool jacobian_used_cuda = false;
                    const std::vector<Score> diff_scores =
                        evaluate_population_scores_scaled(
                            diff_scaled_population,
                            static_cast<int>(diff_columns.size()) * 2,
                            internal_mode,
                            options.use_cuda,
                            &jacobian_used_cuda,
                            &polish_timing);
                    polish_used_cuda = polish_used_cuda || jacobian_used_cuda;
                    for (std::size_t p = 0; p < diff_columns.size(); ++p) {
                        const int j = diff_columns[p];
                        std::vector<double> rp;
                        std::vector<double> rm;
                        bool plus_valid = true;
                        bool minus_valid = true;
                        if (options.auto_tune) {
                            rp = residuals_from_score(
                                diff_scores[2 * p], merit_config_, options.residual_mode);
                            rm = residuals_from_score(
                                diff_scores[2 * p + 1], merit_config_, options.residual_mode);
                            plus_valid =
                                polish_score_trace_acceptable(
                                    diff_scores[2 * p], merit_config_, options) &&
                                rp.size() == best_residuals.size();
                            minus_valid =
                                polish_score_trace_acceptable(
                                    diff_scores[2 * p + 1], merit_config_, options) &&
                                rm.size() == best_residuals.size();
                            if (!plus_valid) {
                                rp = best_residuals;
                            }
                            if (!minus_valid) {
                                rm = best_residuals;
                            }
                        } else {
                            rp = residuals_for_score(
                                diff_scores[2 * p], best_residuals.size());
                            rm = residuals_for_score(
                                diff_scores[2 * p + 1], best_residuals.size());
                        }
                        const double denom = diff_denominators[p];
                        const std::size_t plus_offset =
                            static_cast<std::size_t>(2 * p) * dim;
                        const std::size_t minus_offset = plus_offset + dim;
                        const double plus_denom =
                            diff_scaled_population[plus_offset + static_cast<std::size_t>(j)] -
                            x[static_cast<std::size_t>(j)];
                        const double minus_denom =
                            x[static_cast<std::size_t>(j)] -
                            diff_scaled_population[minus_offset + static_cast<std::size_t>(j)];
                        for (int k = 0; k < residual_count; ++k) {
                            const double base_r = best_residuals[static_cast<std::size_t>(k)];
                            const double rp_value = jacobian_residual_sample(
                                rp[static_cast<std::size_t>(k)],
                                base_r,
                                merit_config_);
                            const double rm_value = jacobian_residual_sample(
                                rm[static_cast<std::size_t>(k)],
                                base_r,
                                merit_config_);
                            double derivative = 0.0;
                            if (plus_valid && minus_valid) {
                                derivative = (rp_value - rm_value) / denom;
                            } else if (plus_valid && plus_denom > 0.0) {
                                derivative = (rp_value - base_r) / plus_denom;
                            } else if (minus_valid && minus_denom > 0.0) {
                                derivative = (base_r - rm_value) / minus_denom;
                            }
                            jacobian[static_cast<std::size_t>(k) * dim + j] = derivative;
                            if (options.finite_difference_diagnostics &&
                                iter == 1 &&
                                std::abs(derivative) > 1.0e8 &&
                                diagnostic_large_jacobian_reports < 24) {
                                const auto& var = variables_[static_cast<std::size_t>(j)];
                                std::cerr << "DLS large jacobian: var=" << j
                                          << " kind=" << variable_kind_name(var.kind)
                                          << " surface=" << var.index
                                          << " row=" << k
                                          << " rp=" << rp[static_cast<std::size_t>(k)]
                                          << " rm=" << rm[static_cast<std::size_t>(k)]
                                          << " rp_used=" << rp_value
                                          << " rm_used=" << rm_value
                                          << " denom=" << denom
                                          << " derivative=" << derivative
                                          << "\n";
                                ++diagnostic_large_jacobian_reports;
                            }
                        }
                    }
                }
            }
            polish_timing.dls_jacobian_seconds += seconds_since(jacobian_start);

            cached_jtj.assign(static_cast<std::size_t>(dim) * dim, 0.0);
            cached_gradient.assign(static_cast<std::size_t>(dim), 0.0);
            for (int k = 0; k < residual_count; ++k) {
                const double r = best_residuals[static_cast<std::size_t>(k)];
                for (int a = 0; a < dim; ++a) {
                    const double ja = jacobian[static_cast<std::size_t>(k) * dim + a];
                    cached_gradient[static_cast<std::size_t>(a)] += ja * r;
                    for (int b = 0; b < dim; ++b) {
                        cached_jtj[static_cast<std::size_t>(a) * dim + b] +=
                            ja * jacobian[static_cast<std::size_t>(k) * dim + b];
                    }
                }
            }
            cached_residual_count = residual_count;
            cached_linear_model_valid = true;
        }
}

} }  // namespace optiland::optimizer_detail
