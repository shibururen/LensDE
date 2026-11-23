/*
 * Initialize one DlsSession from physical values, merit options and the selected internal coordinates.
 * DlsSession::run establishes baseline residuals, incumbent merit, damping and trust-region state here.
 * Preserve projection/scoring order and initial guard thresholds before the first progress callback.
 */
#include "dls_session.hpp"

namespace optiland { namespace optimizer_detail {

void DlsSession::initialize() {
    polish_start = Clock::now();
    dim = static_cast<int>(variables_.size());
    initial = initial_values;
    if (static_cast<int>(initial.size()) != dim) {
        throw std::invalid_argument("polish initial vector has wrong dimension");
    }
    for (int i = 0; i < dim; ++i) {
        initial[static_cast<std::size_t>(i)] = maybe_clip_continuous(
            variables_[static_cast<std::size_t>(i)],
            initial[static_cast<std::size_t>(i)],
            variables_[static_cast<std::size_t>(i)].lower,
            variables_[static_cast<std::size_t>(i)].upper);
    }
    internal_mode = options.internal_scaling;
    x = physical_to_scaled(variables_, initial, internal_mode);
    batch_merit_polish = polish_uses_batch_merit(options);
    polish_used_cuda = false;
    polish_timing = {};
    trust_region_polish = polish_uses_trf(options) && !batch_merit_polish;
    trust_min_radius = std::max(1.0e-12, options.trf_min_radius);
    trust_max_radius = std::max(trust_min_radius, options.trf_max_radius);
    trust_radius = std::clamp(
        options.trf_initial_radius > 0.0 ? options.trf_initial_radius : 1.0,
        trust_min_radius,
        trust_max_radius);
    trust_acceptance_ratio = std::clamp(options.trf_acceptance_ratio, 0.0, 0.99);
    best = score_for_scaled(x);
    best_merit_x = x;
    best_merit_score = best;
    best_residuals = residuals_for_score(best, 0);
    best_sum = sum_squares(best_residuals);
    auto_constraint_gate_floor = options.auto_tune && std::isfinite(best.constraint_gate) && best.constraint_gate >= 0.95
            ? std::max(0.95, best.constraint_gate - 0.02)
            : -std::numeric_limits<double>::infinity();
    auto_first_order_gate_floor = options.auto_tune && std::isfinite(best.first_order_gate) && best.first_order_gate >= 0.95
            ? std::max(0.95, best.first_order_gate - 0.02)
            : -std::numeric_limits<double>::infinity();
    auto_spot_ratio_ceiling = auto_ratio_ceiling(best.max_single_ray_spot_ratio);
    auto_wavefront_ratio_ceiling = auto_ratio_ceiling(best.max_single_ray_wavefront_ratio);
    auto_spot_absolute_ceiling = auto_absolute_ceiling(best.max_single_ray_spot);
    auto_wavefront_absolute_ceiling = auto_absolute_ceiling(best.max_single_ray_wavefront);
    if (options.auto_tune) {
        std::cout << "DLS auto tune: fd=" << options.finite_difference
                  << " lambda=" << options.initial_lambda
                  << " trust_radius=" << trust_radius
                  << " step_search=" << options.step_search_levels
                  << " column_equilibration=yes qr_solver=yes"
                  << " constraint_gate_floor=" << auto_constraint_gate_floor
                  << " spot_ratio_ceiling=" << auto_spot_ratio_ceiling
                  << " wavefront_ratio_ceiling=" << auto_wavefront_ratio_ceiling
                  << " spot_absolute_ceiling=" << auto_spot_absolute_ceiling
                  << " wavefront_absolute_ceiling=" << auto_wavefront_absolute_ceiling
                  << "\n";
    }
    if (trust_region_polish &&
        (!std::isfinite(best.merit) ||
         best.merit >= 0.5 * std::max(1.0, merit_config_.fail_merit))) {
        trust_radius = std::min(trust_radius, 1.0);
    }
    lambda = options.initial_lambda;
    consecutive_rejections = 0;
    max_rejected_steps = std::max(0, options.max_rejected_steps);
    cached_jacobian = {};
    cached_jtj = {};
    cached_gradient = {};
    cached_residual_count = 0;
    cached_linear_model_valid = false;
    consecutive_small_accepted_steps = 0;
    plateau_best_history = {};
    plateau_best_history.reserve(
        static_cast<std::size_t>(std::max(0, options.plateau_window)) + 1U);
    plateau_window = std::max(0, options.plateau_window);
    plateau_min_iterations = std::max(
        plateau_window,
        std::max(0, options.plateau_min_iterations));
    plateau_min_abs_improvement = std::max(0.0, options.plateau_min_abs_improvement);
    plateau_min_rel_improvement = std::max(0.0, options.plateau_min_rel_improvement);
    plateau_early_stop_enabled = plateau_window > 0 &&
        (plateau_min_abs_improvement > 0.0 ||
         plateau_min_rel_improvement > 0.0);
    completed_polish_iterations = 0;
    if (progress) {
        progress(0, best_merit_score, scaled_to_physical(variables_, best_merit_x, internal_mode));
    }
}

} }  // namespace optiland::optimizer_detail
