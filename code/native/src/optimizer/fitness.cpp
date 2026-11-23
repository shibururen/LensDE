/*
 * Construct CandidateFitness and compare merit, feasibility, violation and polish-improvement outcomes.
 * Standard DE, hybrid and adaptive selection share these rankings and CUDA-summary interpretation.
 * Preserve comparison precedence, nonfinite handling and deterministic ties across all callers.
 */
#include "support.hpp"

namespace optiland {
using namespace optimizer_detail;

namespace optimizer_detail {

CandidateFitness make_candidate_fitness(
    const Score& score,
    const MeritConfig& config,
    const AdaptiveHybridDeDlsOptions& options) {
    CandidateFitness fitness;
    fitness.score = score;

    const double coverage_tol = std::max(1.0e-12, config.unified_coverage_tol);
    const double progress_tol = std::max(1.0e-12, config.unified_progress_tol);
    const double failed_tol = std::max(1.0e-12, config.unified_failed_weight_tol);
    const double system_coverage = finite_or(score.trace_coverage, 0.0);
    const double field_coverage = finite_or(score.min_field_coverage, 0.0);
    const double trace_progress = finite_or(score.min_trace_progress, 0.0);
    const double chief_progress = finite_or(score.min_chief_progress, 0.0);

    fitness.trace_violation += positive_square(
        (config.min_system_coverage - system_coverage) / coverage_tol);
    fitness.trace_violation += positive_square(
        (config.min_field_coverage - field_coverage) / coverage_tol);
    fitness.trace_violation += positive_square((1.0 - trace_progress) / progress_tol);
    if (config.require_chief_ray) {
        fitness.trace_violation += positive_square((1.0 - chief_progress) / progress_tol);
    }
    fitness.trace_violation += positive_square(finite_or(score.failed_ray_weight, 0.0) / failed_tol);
    if (score.chief_ray_failed_fields > 0) {
        fitness.trace_violation += static_cast<double>(score.chief_ray_failed_fields);
    }
    if (score.zero_coverage_off_axis_fields > 0) {
        fitness.trace_violation += static_cast<double>(score.zero_coverage_off_axis_fields);
    }
    if (score.distortion_sign_flip_fields > 0) {
        fitness.trace_violation +=
            1.0e6 * static_cast<double>(score.distortion_sign_flip_fields);
    }
    const bool unified_trace_gate = unified_trace_gate_enabled(config);
    if (unified_trace_gate) {
        fitness.trace_violation += std::max(0.0, finite_or(score.trace_group_error, 0.0));
    }

    const bool coverage_ok =
        system_coverage + options.trace_coverage_epsilon >= config.min_system_coverage &&
        field_coverage + options.trace_coverage_epsilon >= config.min_field_coverage;
    const bool chief_ok =
        !config.require_chief_ray ||
        (score.chief_ray_ok && score.chief_ray_failed_fields == 0);
    const bool off_axis_ok =
        !config.require_off_axis_field_coverage ||
        score.zero_coverage_off_axis_fields == 0;
    const bool trace_ok =
        score.trace_ok &&
        coverage_ok &&
        chief_ok &&
        off_axis_ok;
    const bool near_trace_ok =
        coverage_ok &&
        chief_ok &&
        off_axis_ok;
    fitness.trace_feasible =
        score.distortion_sign_flip_fields <= 0 &&
        (trace_ok ||
         (options.allow_near_trace_infeasible && near_trace_ok) ||
         !options.require_trace_feasible);

    fitness.constraint_violation =
        std::max(0.0, finite_or(score.constraint_group_error, 0.0));
    fitness.first_order_violation =
        std::max(0.0, finite_or(score.first_order_group_error, 0.0));
    for (const auto& contribution : score.contributions) {
        const double violation = normalized_contribution_violation(contribution, config);
        const double penalty = std::isfinite(contribution.penalty) ? contribution.penalty : 0.0;
        const bool first_order_target =
            is_first_order_like_contribution(contribution) &&
            !contribution_mode_is_bound_like(contribution);
        if (first_order_target) {
            if (penalty > 0.0 || violation > 1.0e-12) {
                fitness.first_order_violation += violation * violation;
            }
        } else if (!unified_trace_gate && is_constraint_like_contribution(contribution)) {
            if (penalty > 0.0 || violation > 1.0e-12) {
                fitness.constraint_violation += violation * violation;
            }
        }
    }
    fitness.constraint_feasible =
        fitness.constraint_violation <=
        std::max(options.feasibility_epsilon, options.constraint_epsilon);
    fitness.first_order_feasible =
        fitness.first_order_violation <=
        std::max(options.feasibility_epsilon, options.first_order_epsilon);
    fitness.feasible =
        fitness.trace_feasible &&
        fitness.constraint_feasible &&
        (!options.treat_first_order_as_feasibility || fitness.first_order_feasible);

    const bool prefer_wavefront_objective = merit_prefers_wavefront_objective(config);
    if (score.distortion_sign_flip_fields > 0) {
        fitness.image_objective = finite_or(score.merit, config.fail_merit);
    } else if (unified_trace_gate_enabled(config) && std::isfinite(score.image_group_error)) {
        fitness.image_objective = score.image_group_error;
    } else if (unified_trace_gate_enabled(config) && std::isfinite(score.image_penalty)) {
        fitness.image_objective = score.image_penalty;
    } else if (prefer_wavefront_objective && std::isfinite(score.mean_wavefront_rms)) {
        fitness.image_objective = score.mean_wavefront_rms;
    } else if (std::isfinite(score.mean_spot)) {
        fitness.image_objective = score.mean_spot;
    } else if (std::isfinite(score.mean_wavefront_rms)) {
        fitness.image_objective = score.mean_wavefront_rms;
    } else {
        fitness.image_objective = finite_or(score.merit, config.fail_merit);
    }
    fitness.objective = fitness.image_objective;
    if (!std::isfinite(fitness.objective)) {
        fitness.objective = finite_or(score.merit, config.fail_merit);
    }
    fitness.total_violation =
        10.0 * fitness.trace_violation +
        5.0 * fitness.constraint_violation +
        fitness.first_order_violation;
    return fitness;
}

Score score_from_cuda_summary(const CudaScoreSummary& summary, const MeritConfig& config) {
    Score score;
    score.merit_model = config.merit_model;
    score.merit = finite_or(summary.merit, config.fail_merit);
    score.uncapped_merit = summary.uncapped_merit;
    score.merit_sum = summary.merit_sum;
    score.merit_weight_sum = summary.merit_weight_sum;
    score.efl = summary.efl;
    score.ttl = summary.ttl;
    score.bfl = summary.bfl;
    score.working_f_number = summary.working_f_number;
    score.image_space_numerical_aperture =
        summary.image_space_numerical_aperture;
    score.working_f_number_valid_marginal_rays =
        summary.working_f_number_valid_marginal_rays;
    score.mean_spot = summary.mean_spot;
    score.max_spot = summary.max_spot;
    score.max_single_ray_spot = summary.max_single_ray_spot;
    score.max_single_ray_spot_ratio = summary.max_single_ray_spot_ratio;
    score.mean_wavefront_rms = summary.mean_wavefront_rms;
    score.max_wavefront_rms = summary.max_wavefront_rms;
    score.max_single_ray_wavefront = summary.max_single_ray_wavefront;
    score.max_single_ray_wavefront_ratio = summary.max_single_ray_wavefront_ratio;
    score.max_distortion = summary.max_distortion;
    score.distortion_model = config.distortion_model;
    score.distortion_sign_flip_fields = summary.distortion_sign_flip_fields;
    score.max_chief_ray_angle = summary.max_chief_ray_angle;
    score.lateral_color = summary.lateral_color;
    score.paraxial_axial_color_focus_shift_mm =
        summary.paraxial_axial_color_focus_shift_mm;
    score.min_edge = summary.min_edge;
    score.trace_coverage = summary.trace_coverage;
    score.min_field_coverage = summary.min_field_coverage;
    score.trace_progress = summary.trace_progress;
    score.min_trace_progress = summary.min_trace_progress;
    score.min_chief_progress = summary.min_chief_progress;
    score.min_relative_illumination_proxy =
        summary.min_relative_illumination_proxy;
    score.trace_priority_metric = summary.trace_priority_metric;
    score.trace_priority_penalty = summary.trace_priority_penalty;
    score.trace_group_error = summary.trace_group_error;
    score.constraint_group_error = summary.constraint_group_error;
    score.first_order_group_error = summary.first_order_group_error;
    score.image_group_error = summary.image_group_error;
    score.trace_gate = summary.trace_gate;
    score.constraint_gate = summary.constraint_gate;
    score.first_order_gate = summary.first_order_gate;
    score.image_gate = summary.image_gate;
    score.trace_penalty = summary.trace_penalty;
    score.constraint_penalty = summary.constraint_penalty;
    score.first_order_penalty = summary.first_order_penalty;
    score.image_penalty = summary.image_penalty;
    score.other_penalty = summary.other_penalty;
    score.failed_ray_weight = summary.failed_ray_weight;
    score.chief_ray_ok = summary.chief_ray_ok != 0;
    score.chief_ray_failed_fields = summary.chief_ray_failed_fields;
    score.zero_coverage_off_axis_fields = summary.zero_coverage_off_axis_fields;
    score.ray_path_axis_cross_fields = summary.ray_path_axis_cross_fields;
    score.center_early_focus_fields = summary.center_early_focus_fields;
    score.chief_entry_side_failed_fields = summary.chief_entry_side_failed_fields;
    score.ray_path_structural_failure_fields = summary.ray_path_structural_failure_fields;
    score.max_axis_cross_count = summary.max_axis_cross_count;
    score.min_pupil_order_jacobian = summary.min_pupil_order_jacobian;
    score.pupil_order_worst_surface_progress =
        summary.pupil_order_worst_surface_progress;
    score.pupil_order_prior_penalty = summary.pupil_order_prior_penalty;
    score.pupil_order_fold_fields = summary.pupil_order_fold_fields;
    score.pupil_order_hard_fold_fields =
        summary.pupil_order_hard_fold_fields;
    score.trace_ok = summary.trace_ok != 0;
    if (score.distortion_sign_flip_fields > 0) {
        score.message = "actual image height is opposite to ideal image height";
    } else if (score.ray_path_structural_failure_fields > 0) {
        score.message = "ray path structural failure";
    } else if (!score.trace_ok) {
        score.message = "CUDA score summary trace not fully valid";
    }
    return score;
}

bool finite_less(double a, double b, double tolerance ) {
    const bool fa = std::isfinite(a);
    const bool fb = std::isfinite(b);
    if (fa != fb) {
        return fa;
    }
    if (!fa && !fb) {
        return false;
    }
    return a + tolerance < b;
}

void classify_polish_merit_delta(
    double before_merit,
    double after_merit,
    int& strict_improvements,
    int& neutral_results,
    int& worse_results) {
    if (!std::isfinite(after_merit)) {
        ++worse_results;
        return;
    }
    if (!std::isfinite(before_merit)) {
        ++strict_improvements;
        return;
    }
    const double tolerance = 1.0e-12 * std::max(1.0, std::abs(before_merit));
    if (after_merit + tolerance < before_merit) {
        ++strict_improvements;
    } else if (after_merit <= before_merit + tolerance) {
        ++neutral_results;
    } else {
        ++worse_results;
    }
}

bool unified_scalar_merit_score(const Score& score) {
    const std::string model = normalized_token(score.merit_model);
    return model == "unified_trace_gate" ||
           model == "trace_guarded_adaptive" ||
           model == "unified_adaptive" ||
           model == "trace_guarded";
}

bool merit_less_with_tiebreaks(const CandidateFitness& a, const CandidateFitness& b) {
    auto adjusted_merit = [](const CandidateFitness& fitness) {
        if (!std::isfinite(fitness.score.merit)) {
            return fitness.score.merit;
        }
        return fitness.score.merit + std::max(0.0, fitness.avoidance_penalty);
    };
    auto adjusted_objective = [](const CandidateFitness& fitness) {
        if (!std::isfinite(fitness.objective)) {
            return fitness.objective;
        }
        return fitness.objective + std::max(0.0, fitness.avoidance_penalty);
    };
    const double a_merit = adjusted_merit(a);
    const double b_merit = adjusted_merit(b);
    if (finite_less(a_merit, b_merit) ||
        finite_less(b_merit, a_merit)) {
        return finite_less(a_merit, b_merit);
    }
    if (finite_less(a.total_violation, b.total_violation) ||
        finite_less(b.total_violation, a.total_violation)) {
        return finite_less(a.total_violation, b.total_violation);
    }
    return finite_less(adjusted_objective(a), adjusted_objective(b));
}

bool polish_merit_not_worse(const CandidateFitness& polished, const CandidateFitness& source) {
    if (!std::isfinite(polished.score.merit)) {
        return false;
    }
    if (!std::isfinite(source.score.merit)) {
        return true;
    }
    const double tolerance = 1.0e-12 * std::max(1.0, std::abs(source.score.merit));
    return polished.score.merit <= source.score.merit + tolerance;
}

bool candidate_better(
    const CandidateFitness& a,
    const CandidateFitness& b,
    const AdaptiveHybridDeDlsOptions& options) {
    if (options.hard_reject_structural_failures) {
        const bool a_structural_failure =
            a.score.ray_path_structural_failure_fields > 0;
        const bool b_structural_failure =
            b.score.ray_path_structural_failure_fields > 0;
        if (a_structural_failure != b_structural_failure) {
            return !a_structural_failure;
        }
    }
    const bool scalar_merit_order =
        options.constraint_handling == ConstraintHandlingMode::ScalarMerit ||
        unified_scalar_merit_score(a.score) ||
        unified_scalar_merit_score(b.score);
    if (scalar_merit_order) {
        if (a.trace_feasible != b.trace_feasible) {
            return a.trace_feasible;
        }
        if (!a.trace_feasible &&
            (finite_less(a.trace_violation, b.trace_violation) ||
             finite_less(b.trace_violation, a.trace_violation))) {
            return finite_less(a.trace_violation, b.trace_violation);
        }
        return merit_less_with_tiebreaks(a, b);
    }
    if (a.trace_feasible != b.trace_feasible) {
        return a.trace_feasible;
    }
    if (a.feasible != b.feasible) {
        return a.feasible;
    }
    if (a.feasible && b.feasible) {
        const double a_objective =
            std::isfinite(a.objective) ? a.objective + std::max(0.0, a.avoidance_penalty)
                                       : a.objective;
        const double b_objective =
            std::isfinite(b.objective) ? b.objective + std::max(0.0, b.avoidance_penalty)
                                       : b.objective;
        if (finite_less(a_objective, b_objective) ||
            finite_less(b_objective, a_objective)) {
            return finite_less(a_objective, b_objective);
        }
        return merit_less_with_tiebreaks(a, b);
    }
    if (finite_less(a.trace_violation, b.trace_violation) ||
        finite_less(b.trace_violation, a.trace_violation)) {
        return finite_less(a.trace_violation, b.trace_violation);
    }
    if (finite_less(a.constraint_violation, b.constraint_violation) ||
        finite_less(b.constraint_violation, a.constraint_violation)) {
        return finite_less(a.constraint_violation, b.constraint_violation);
    }
    if (finite_less(a.first_order_violation, b.first_order_violation) ||
        finite_less(b.first_order_violation, a.first_order_violation)) {
        return finite_less(a.first_order_violation, b.first_order_violation);
    }
    const double a_objective =
        std::isfinite(a.objective) ? a.objective + std::max(0.0, a.avoidance_penalty)
                                   : a.objective;
    const double b_objective =
        std::isfinite(b.objective) ? b.objective + std::max(0.0, b.avoidance_penalty)
                                   : b.objective;
    if (finite_less(a_objective, b_objective) ||
        finite_less(b_objective, a_objective)) {
        return finite_less(a_objective, b_objective);
    }
    return merit_less_with_tiebreaks(a, b);
}

}  // namespace optimizer_detail

}  // namespace optiland
