#pragma once
#include "optiland/optimizer/scaling.hpp"
#include <string>

namespace optiland {
struct PolishOptions {
    bool auto_tune = false;
    int max_iterations = 40;
    int max_rejected_steps = 6;
    double initial_lambda = 1.0e-2;
    double finite_difference = 1.0e-4;
    double min_step = 1.0e-8;
    double tolerance = 1.0e-4;
    int plateau_window = 40;
    int plateau_min_iterations = 60;
    double plateau_min_abs_improvement = 1.0e-5;
    double plateau_min_rel_improvement = 2.0e-6;
    double damping_diagonal_floor = 1.0;
    int step_search_levels = 0;
    double step_search_shrink = 0.5;
    bool column_equilibration = false;
    bool active_set_bounds = false;
    bool projected_active_set_bounds = false;
    double active_set_bound_tolerance = 1.0e-7;
    bool qr_linear_solver = false;
    std::string method = "trf";
    std::string residual_mode = "expanded";
    double trf_initial_radius = 10.0;
    double trf_min_radius = 1.0e-8;
    double trf_max_radius = 30.0;
    double trf_acceptance_ratio = 1.0e-4;
    bool use_cuda = true;
    bool finite_difference_diagnostics = false;
    bool freeze_vignetting_variables = false;
    InternalScalingMode internal_scaling = InternalScalingMode::LegacyOptical;
    bool trace_acceptance_enabled = false;
    bool require_trace_feasible = true;
    bool allow_near_trace_infeasible = false;
    double trace_coverage_epsilon = 1.0e-9;
};






}  // namespace optiland
