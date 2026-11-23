#pragma once
#include "support_types.hpp"

namespace optiland { namespace optimizer_detail {

double sum_squares(const std::vector<double>& residuals);

double jacobian_residual_sample(
    double candidate,
    double base,
    const MeritConfig& config);

bool solve_linear_system(
    std::vector<double> matrix,
    std::vector<double> rhs,
    int n,
    std::vector<double>* solution);

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
    int* solve_dim = nullptr,
    const std::vector<double>* jacobian = nullptr,
    const std::vector<double>* residuals = nullptr,
    int residual_count = 0);

} }  // namespace optiland::optimizer_detail
