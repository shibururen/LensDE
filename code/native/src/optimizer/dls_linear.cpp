/*
 * Solve the dense DLS linear model and provide residual/Jacobian sampling arithmetic.
 * Scalar and batched polish paths share these numerical kernels on caller-owned matrices and vectors.
 * Preserve pivot rules, summation order and damping arithmetic to retain accepted-step behavior.
 */
#include "support.hpp"

namespace optiland {
using namespace optimizer_detail;

namespace optimizer_detail {

double sum_squares(const std::vector<double>& residuals) {
    double sum = 0.0;
    for (double r : residuals) {
        sum += r * r;
    }
    return sum;
}

double jacobian_residual_sample(
    double candidate,
    double base,
    const MeritConfig& config) {
    if (!std::isfinite(candidate)) {
        return std::isfinite(base) ? base : 0.0;
    }
    if (!std::isfinite(base)) {
        return candidate;
    }
    const double fail_residual =
        0.1 * std::sqrt(std::max(1.0, std::abs(config.fail_merit)));
    if (std::abs(candidate) > fail_residual && std::abs(base) <= fail_residual) {
        return base;
    }
    return candidate;
}

bool solve_linear_system(
    std::vector<double> matrix,
    std::vector<double> rhs,
    int n,
    std::vector<double>* solution) {
    solution->assign(static_cast<std::size_t>(n), 0.0);
    for (int col = 0; col < n; ++col) {
        int pivot = col;
        double pivot_abs = std::abs(matrix[static_cast<std::size_t>(col) * n + col]);
        for (int row = col + 1; row < n; ++row) {
            const double candidate = std::abs(matrix[static_cast<std::size_t>(row) * n + col]);
            if (candidate > pivot_abs) {
                pivot = row;
                pivot_abs = candidate;
            }
        }
        if (pivot_abs < 1.0e-18 || !std::isfinite(pivot_abs)) {
            return false;
        }
        if (pivot != col) {
            for (int k = col; k < n; ++k) {
                std::swap(
                    matrix[static_cast<std::size_t>(col) * n + k],
                    matrix[static_cast<std::size_t>(pivot) * n + k]);
            }
            std::swap(rhs[static_cast<std::size_t>(col)], rhs[static_cast<std::size_t>(pivot)]);
        }
        const double diag = matrix[static_cast<std::size_t>(col) * n + col];
        for (int row = col + 1; row < n; ++row) {
            const double factor = matrix[static_cast<std::size_t>(row) * n + col] / diag;
            matrix[static_cast<std::size_t>(row) * n + col] = 0.0;
            for (int k = col + 1; k < n; ++k) {
                matrix[static_cast<std::size_t>(row) * n + k] -=
                    factor * matrix[static_cast<std::size_t>(col) * n + k];
            }
            rhs[static_cast<std::size_t>(row)] -= factor * rhs[static_cast<std::size_t>(col)];
        }
    }
    for (int row = n - 1; row >= 0; --row) {
        double value = rhs[static_cast<std::size_t>(row)];
        for (int col = row + 1; col < n; ++col) {
            value -= matrix[static_cast<std::size_t>(row) * n + col] *
                     (*solution)[static_cast<std::size_t>(col)];
        }
        const double diag = matrix[static_cast<std::size_t>(row) * n + row];
        if (std::abs(diag) < 1.0e-18 || !std::isfinite(diag)) {
            return false;
        }
        (*solution)[static_cast<std::size_t>(row)] = value / diag;
    }
    return true;
}

bool solve_dls_linear_model(
    const std::vector<double>& jtj,
    const std::vector<double>& gradient,
    const std::vector<double>& x,
    const std::vector<VariableSpec>& variables,
    InternalScalingMode internal_mode,
    const PolishOptions& options,
    double lambda,
    int dim,
    std::vector<double>* step,
    int* solve_dim ,
    const std::vector<double>* jacobian ,
    const std::vector<double>* residuals ,
    int residual_count ) {
    step->assign(static_cast<std::size_t>(dim), 0.0);
    std::vector<bool> frozen(static_cast<std::size_t>(dim), false);
    std::vector<double> fixed_step(static_cast<std::size_t>(dim), 0.0);
    const double active_tol = std::max(0.0, options.active_set_bound_tolerance);
    const bool active_bounds = false;
    for (int j = 0; j < dim; ++j) {
        const auto& var = variables[static_cast<std::size_t>(j)];
        if (is_frozen_polish_variable(var, options)) {
            frozen[static_cast<std::size_t>(j)] = true;
            continue;
        }
        if (active_bounds) {
            const auto [lo, hi] = internal_bounds(var, internal_mode);
            const double xj = x[static_cast<std::size_t>(j)];
            const double gj = gradient[static_cast<std::size_t>(j)];
            const bool at_lower = xj <= lo + active_tol;
            const bool at_upper = xj >= hi - active_tol;
            frozen[static_cast<std::size_t>(j)] =
                (at_lower && gj > 0.0) ||
                (at_upper && gj < 0.0) ||
                hi <= lo;
        }
    }

    const int max_active_iterations = 1;
    for (int active_iter = 0; active_iter < max_active_iterations; ++active_iter) {
        std::vector<int> columns;
        columns.reserve(static_cast<std::size_t>(dim));
        for (int j = 0; j < dim; ++j) {
            if (!frozen[static_cast<std::size_t>(j)]) {
                columns.push_back(j);
            }
        }
        const int n = static_cast<int>(columns.size());
        if (solve_dim) {
            *solve_dim = n;
        }
        if (n <= 0) {
            *step = fixed_step;
            return true;
        }

        std::vector<double> column_scale(static_cast<std::size_t>(n), 1.0);
        if (options.column_equilibration) {
            for (int i = 0; i < n; ++i) {
                const int j = columns[static_cast<std::size_t>(i)];
                const double diag = jtj[static_cast<std::size_t>(j) * dim + j];
                const double col_norm = std::sqrt(std::max(0.0, diag));
                if (std::isfinite(col_norm) && col_norm > 1.0e-12) {
                    column_scale[static_cast<std::size_t>(i)] = 1.0 / col_norm;
                }
            }
        }

        std::vector<double> candidate_step = fixed_step;
        if (options.qr_linear_solver &&
            jacobian != nullptr &&
            residuals != nullptr &&
            residual_count > 0 &&
            static_cast<int>(residuals->size()) >= residual_count &&
            static_cast<int>(jacobian->size()) >= residual_count * dim) {
            const int rows = residual_count + n;
            std::vector<double> a(static_cast<std::size_t>(rows) * n, 0.0);
            std::vector<double> b(static_cast<std::size_t>(rows), 0.0);
            for (int k = 0; k < residual_count; ++k) {
                double fixed_effect = 0.0;
                for (int f = 0; f < dim; ++f) {
                    const double fixed = fixed_step[static_cast<std::size_t>(f)];
                    if (fixed != 0.0) {
                        fixed_effect +=
                            (*jacobian)[static_cast<std::size_t>(k) * dim + f] * fixed;
                    }
                }
                b[static_cast<std::size_t>(k)] =
                    -(*residuals)[static_cast<std::size_t>(k)] - fixed_effect;
                for (int i = 0; i < n; ++i) {
                    const int j = columns[static_cast<std::size_t>(i)];
                    a[static_cast<std::size_t>(k) * n + i] =
                        (*jacobian)[static_cast<std::size_t>(k) * dim + j] *
                        column_scale[static_cast<std::size_t>(i)];
                }
            }
            for (int i = 0; i < n; ++i) {
                const int j = columns[static_cast<std::size_t>(i)];
                const double scaled_diag =
                    jtj[static_cast<std::size_t>(j) * dim + j] *
                    column_scale[static_cast<std::size_t>(i)] *
                    column_scale[static_cast<std::size_t>(i)];
                const double damping_diag = std::max(
                    std::abs(scaled_diag),
                    std::max(0.0, options.damping_diagonal_floor));
                a[static_cast<std::size_t>(residual_count + i) * n + i] =
                    std::sqrt(std::max(0.0, lambda * damping_diag));
            }

            std::vector<double> q(static_cast<std::size_t>(rows) * n, 0.0);
            std::vector<double> r(static_cast<std::size_t>(n) * n, 0.0);
            std::vector<double> v(static_cast<std::size_t>(rows), 0.0);
            for (int j = 0; j < n; ++j) {
                for (int row = 0; row < rows; ++row) {
                    v[static_cast<std::size_t>(row)] =
                        a[static_cast<std::size_t>(row) * n + j];
                }
                for (int pass = 0; pass < 2; ++pass) {
                    for (int i = 0; i < j; ++i) {
                        double rij = 0.0;
                        for (int row = 0; row < rows; ++row) {
                            rij += q[static_cast<std::size_t>(row) * n + i] *
                                   v[static_cast<std::size_t>(row)];
                        }
                        r[static_cast<std::size_t>(i) * n + j] += rij;
                        for (int row = 0; row < rows; ++row) {
                            v[static_cast<std::size_t>(row)] -=
                                rij * q[static_cast<std::size_t>(row) * n + i];
                        }
                    }
                }
                double norm_sq = 0.0;
                for (double value : v) {
                    norm_sq += value * value;
                }
                const double rjj = std::sqrt(norm_sq);
                if (!std::isfinite(rjj) || rjj < 1.0e-14) {
                    return false;
                }
                r[static_cast<std::size_t>(j) * n + j] = rjj;
                for (int row = 0; row < rows; ++row) {
                    q[static_cast<std::size_t>(row) * n + j] =
                        v[static_cast<std::size_t>(row)] / rjj;
                }
            }

            std::vector<double> qtb(static_cast<std::size_t>(n), 0.0);
            for (int j = 0; j < n; ++j) {
                double value = 0.0;
                for (int row = 0; row < rows; ++row) {
                    value += q[static_cast<std::size_t>(row) * n + j] *
                             b[static_cast<std::size_t>(row)];
                }
                qtb[static_cast<std::size_t>(j)] = value;
            }
            std::vector<double> reduced_step(static_cast<std::size_t>(n), 0.0);
            for (int row = n - 1; row >= 0; --row) {
                double value = qtb[static_cast<std::size_t>(row)];
                for (int col = row + 1; col < n; ++col) {
                    value -= r[static_cast<std::size_t>(row) * n + col] *
                             reduced_step[static_cast<std::size_t>(col)];
                }
                const double diag = r[static_cast<std::size_t>(row) * n + row];
                if (std::abs(diag) < 1.0e-18 || !std::isfinite(diag)) {
                    return false;
                }
                reduced_step[static_cast<std::size_t>(row)] = value / diag;
            }
            for (int i = 0; i < n; ++i) {
                const int j = columns[static_cast<std::size_t>(i)];
                candidate_step[static_cast<std::size_t>(j)] +=
                    reduced_step[static_cast<std::size_t>(i)] *
                    column_scale[static_cast<std::size_t>(i)];
            }
        } else {
            std::vector<double> system(static_cast<std::size_t>(n) * n, 0.0);
            std::vector<double> rhs(static_cast<std::size_t>(n), 0.0);
            for (int ia = 0; ia < n; ++ia) {
                const int a = columns[static_cast<std::size_t>(ia)];
                const double scale_a = column_scale[static_cast<std::size_t>(ia)];
                double fixed_coupling = 0.0;
                for (int f = 0; f < dim; ++f) {
                    const double fixed = fixed_step[static_cast<std::size_t>(f)];
                    if (fixed != 0.0) {
                        fixed_coupling +=
                            jtj[static_cast<std::size_t>(a) * dim + f] * fixed;
                    }
                }
                rhs[static_cast<std::size_t>(ia)] =
                    -(gradient[static_cast<std::size_t>(a)] + fixed_coupling) *
                    scale_a;
                for (int ib = 0; ib < n; ++ib) {
                    const int b = columns[static_cast<std::size_t>(ib)];
                    const double scale_b = column_scale[static_cast<std::size_t>(ib)];
                    system[static_cast<std::size_t>(ia) * n + ib] =
                        jtj[static_cast<std::size_t>(a) * dim + b] *
                        scale_a * scale_b;
                }
            }
            for (int i = 0; i < n; ++i) {
                const double diag = std::max(
                    std::abs(system[static_cast<std::size_t>(i) * n + i]),
                    std::max(0.0, options.damping_diagonal_floor));
                system[static_cast<std::size_t>(i) * n + i] += lambda * diag;
            }

            std::vector<double> reduced_step;
            if (!solve_linear_system(system, rhs, n, &reduced_step)) {
                return false;
            }
            for (int i = 0; i < n; ++i) {
                const int j = columns[static_cast<std::size_t>(i)];
                candidate_step[static_cast<std::size_t>(j)] +=
                    reduced_step[static_cast<std::size_t>(i)] *
                    column_scale[static_cast<std::size_t>(i)];
            }
        }

        *step = std::move(candidate_step);
        return true;
    }
    *step = fixed_step;
    return true;
}

}  // namespace optimizer_detail

}  // namespace optiland
