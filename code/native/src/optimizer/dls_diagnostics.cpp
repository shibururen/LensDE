/*
 * Write optional DlsSession linear-model diagnostics for the current residual/Jacobian state.
 * The Jacobian phase passes the iteration and residual count after constructing its cached model.
 * Keep diagnostics observational and retain residual/variable indexing when formatting matrix data.
 */
#include "dls_session.hpp"

namespace optiland { namespace optimizer_detail {

void DlsSession::write_linear_model_diagnostics(int iter, int residual_count) {
    const auto& jtj = cached_jtj;
    const auto& gradient = cached_gradient;
if (options.finite_difference_diagnostics && iter == 1) {
            double gradient_norm_sq = 0.0;
            double max_abs_gradient = 0.0;
            std::vector<double> column_norms;
            std::vector<std::pair<double, int>> ranked_columns;
            column_norms.reserve(static_cast<std::size_t>(dim));
            ranked_columns.reserve(static_cast<std::size_t>(dim));
            int active_lower = 0;
            int active_upper = 0;
            int outward_active = 0;
            constexpr double kActiveBoundTol = 1.0e-7;
            for (int j = 0; j < dim; ++j) {
                const double gj = gradient[static_cast<std::size_t>(j)];
                gradient_norm_sq += gj * gj;
                max_abs_gradient = std::max(max_abs_gradient, std::abs(gj));
                const double col_norm = std::sqrt(std::max(
                    0.0,
                    jtj[static_cast<std::size_t>(j) * dim + j]));
                if (std::isfinite(col_norm)) {
                    column_norms.push_back(col_norm);
                    ranked_columns.push_back({col_norm, j});
                }
                const auto [lo, hi] =
                    internal_bounds(variables_[static_cast<std::size_t>(j)], internal_mode);
                const double xj = x[static_cast<std::size_t>(j)];
                const bool at_lower = xj <= lo + kActiveBoundTol;
                const bool at_upper = xj >= hi - kActiveBoundTol;
                active_lower += at_lower ? 1 : 0;
                active_upper += at_upper ? 1 : 0;
                if ((at_lower && gj > 0.0) || (at_upper && gj < 0.0)) {
                    ++outward_active;
                }
            }
            std::sort(column_norms.begin(), column_norms.end());
            const double col_min = column_norms.empty() ? 0.0 : column_norms.front();
            const double col_med = column_norms.empty()
                                       ? 0.0
                                       : column_norms[column_norms.size() / 2];
            const double col_max = column_norms.empty() ? 0.0 : column_norms.back();
            std::cerr << "DLS model diagnostics iter " << iter
                      << ": residuals=" << residual_count
                      << " dim=" << dim
                      << " residual_sum=" << best_sum
                      << " gradient_norm=" << std::sqrt(gradient_norm_sq)
                      << " max_abs_gradient=" << max_abs_gradient
                      << " col_norm_min=" << col_min
                      << " col_norm_med=" << col_med
                      << " col_norm_max=" << col_max
                      << " active_lower=" << active_lower
                      << " active_upper=" << active_upper
                      << " outward_active=" << outward_active
                      << "\n";
            std::sort(
                ranked_columns.begin(),
                ranked_columns.end(),
                [](const auto& a, const auto& b) { return a.first > b.first; });
            const int report_columns =
                std::min<int>(8, static_cast<int>(ranked_columns.size()));
            for (int rank = 0; rank < report_columns; ++rank) {
                const int j = ranked_columns[static_cast<std::size_t>(rank)].second;
                const auto& var = variables_[static_cast<std::size_t>(j)];
                const auto [lo, hi] = internal_bounds(var, internal_mode);
                double max_abs_jac = 0.0;
                int max_abs_jac_row = 0;
                for (int k = 0; k < residual_count; ++k) {
                    const double value =
                        cached_jacobian[static_cast<std::size_t>(k) * dim + j];
                    if (std::abs(value) > max_abs_jac) {
                        max_abs_jac = std::abs(value);
                        max_abs_jac_row = k;
                    }
                }
                std::cerr << "DLS column diagnostics rank " << rank
                          << ": var=" << j
                          << " kind=" << variable_kind_name(var.kind)
                          << " surface=" << var.index
                          << " physical="
                          << internal_to_physical(
                                 var,
                                 x[static_cast<std::size_t>(j)],
                                 internal_mode)
                          << " internal=" << x[static_cast<std::size_t>(j)]
                          << " bounds=[" << lo << "," << hi << "]"
                          << " col_norm=" << ranked_columns[static_cast<std::size_t>(rank)].first
                          << " gradient=" << gradient[static_cast<std::size_t>(j)]
                          << " max_abs_jac=" << max_abs_jac
                          << " max_abs_jac_row=" << max_abs_jac_row
                          << "\n";
            }
        }
}

} }  // namespace optiland::optimizer_detail
