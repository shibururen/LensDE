/*
 * Apply EFL-sign, CRA and joint-constraint anchor rules before optimizer replacement decisions.
 * DE and adaptive selection share these predicates and the caller-owned anchor state.
 * Preserve guard precedence, merit tie breaks and stable fitness ordering when changing acceptance policy.
 */
#include "support.hpp"

namespace optiland {
using namespace optimizer_detail;

namespace optimizer_detail {

bool efl_sign_matches_target(double efl, double target_efl) {
    return std::isfinite(efl) && std::isfinite(target_efl) &&
           std::abs(efl) > 1.0e-12 && std::abs(target_efl) > 1.0e-12 &&
           std::signbit(efl) == std::signbit(target_efl);
}

EflSignPriorityDecision efl_sign_priority_decision(
    const CandidateFitness& trial,
    const CandidateFitness& parent,
    const AdaptiveHybridDeDlsOptions& options,
    double target_efl) {
    if (!options.efl_sign_priority || !std::isfinite(target_efl) ||
        std::abs(target_efl) <= 1.0e-12) {
        return EflSignPriorityDecision::NoPreference;
    }

    // Keep the existing hard ray-path and trace-feasibility hierarchy. EFL
    // sign only decides between candidates that are comparable at that level.
    if (options.hard_reject_structural_failures) {
        const bool trial_structural_failure =
            trial.score.ray_path_structural_failure_fields > 0;
        const bool parent_structural_failure =
            parent.score.ray_path_structural_failure_fields > 0;
        if (trial_structural_failure != parent_structural_failure) {
            return EflSignPriorityDecision::NoPreference;
        }
    }
    if (trial.trace_feasible != parent.trace_feasible) {
        return EflSignPriorityDecision::NoPreference;
    }
    if (!trial.trace_feasible &&
        (finite_less(trial.trace_violation, parent.trace_violation) ||
         finite_less(parent.trace_violation, trial.trace_violation))) {
        return EflSignPriorityDecision::NoPreference;
    }

    const bool trial_matches =
        efl_sign_matches_target(trial.score.efl, target_efl);
    const bool parent_matches =
        efl_sign_matches_target(parent.score.efl, target_efl);
    if (trial_matches == parent_matches) {
        return EflSignPriorityDecision::NoPreference;
    }
    return trial_matches
               ? EflSignPriorityDecision::PreferTrial
               : EflSignPriorityDecision::PreferParent;
}

bool cra_worsening_guard_blocks_trial(
    const CandidateFitness& trial,
    const CandidateFitness& parent,
    const AdaptiveHybridDeDlsOptions& options) {
    if (!options.cra_worsening_guard_enabled ||
        !std::isfinite(options.cra_worsening_guard_target_deg) ||
        !std::isfinite(options.cra_worsening_guard_tolerance_deg) ||
        options.cra_worsening_guard_tolerance_deg < 0.0) {
        return false;
    }

    // Structural and trace-feasibility transitions retain their existing
    // priority. The CRA guard only vetoes otherwise comparable DE survival
    // decisions, just like the same-sign branch of EFL priority.
    if (options.hard_reject_structural_failures) {
        const bool trial_structural_failure =
            trial.score.ray_path_structural_failure_fields > 0;
        const bool parent_structural_failure =
            parent.score.ray_path_structural_failure_fields > 0;
        if (trial_structural_failure != parent_structural_failure) {
            return false;
        }
    }
    if (trial.trace_feasible != parent.trace_feasible) {
        return false;
    }
    if (!trial.trace_feasible &&
        (finite_less(trial.trace_violation, parent.trace_violation) ||
         finite_less(parent.trace_violation, trial.trace_violation))) {
        return false;
    }

    const double trial_cra = trial.score.max_chief_ray_angle;
    const double parent_cra = parent.score.max_chief_ray_angle;
    if (!std::isfinite(trial_cra) || !std::isfinite(parent_cra)) {
        return false;
    }
    const double upper = options.cra_worsening_guard_target_deg +
                         options.cra_worsening_guard_tolerance_deg;
    if (!std::isfinite(upper) || trial_cra <= upper) {
        return false;
    }
    if (parent_cra <= upper) {
        return true;
    }
    const double deadband =
        std::max(0.0, options.cra_worsening_guard_deadband_deg);
    return trial_cra > parent_cra + deadband;
}

double distortion_magnitude(const CandidateFitness& fitness) {
    return std::abs(fitness.score.max_distortion);
}

bool joint_constraint_anchor_candidate_qualifies(
    const CandidateFitness& candidate,
    const AdaptiveHybridDeDlsOptions& options) {
    if (!options.joint_constraint_anchor_guard_enabled) {
        return false;
    }
    const double cra = candidate.score.max_chief_ray_angle;
    const double distortion = distortion_magnitude(candidate);
    const double cra_upper = options.joint_constraint_anchor_cra_target_deg +
                             options.joint_constraint_anchor_cra_tolerance_deg;
    const double distortion_upper =
        options.joint_constraint_anchor_distortion_target +
        options.joint_constraint_anchor_distortion_tolerance;
    return candidate.trace_feasible && candidate.score.trace_ok &&
           std::isfinite(cra) && std::isfinite(distortion) &&
           std::isfinite(cra_upper) && std::isfinite(distortion_upper) &&
           cra <= cra_upper && distortion <= distortion_upper;
}

bool latch_or_improve_joint_constraint_anchor(
    JointConstraintAnchorState& anchor,
    const CandidateFitness& candidate,
    const AdaptiveHybridDeDlsOptions& options) {
    if (!joint_constraint_anchor_candidate_qualifies(candidate, options)) {
        return false;
    }
    const double cra = candidate.score.max_chief_ray_angle;
    const double distortion = distortion_magnitude(candidate);
    if (!anchor.active) {
        anchor.active = true;
        anchor.cra_deg = cra;
        anchor.distortion = distortion;
        return true;
    }
    const bool no_worse =
        cra <= anchor.cra_deg + 1.0e-12 &&
        distortion <= anchor.distortion + 1.0e-12;
    const bool strictly_better =
        cra < anchor.cra_deg - 1.0e-12 ||
        distortion < anchor.distortion - 1.0e-12;
    if (!no_worse || !strictly_better) {
        return false;
    }
    anchor.cra_deg = cra;
    anchor.distortion = distortion;
    return true;
}

JointConstraintAnchorDecision joint_constraint_anchor_decision(
    const CandidateFitness& trial,
    const CandidateFitness& parent,
    const JointConstraintAnchorState& anchor,
    const AdaptiveHybridDeDlsOptions& options) {
    JointConstraintAnchorDecision decision;
    if (!options.joint_constraint_anchor_guard_enabled || !anchor.active ||
        !std::isfinite(anchor.cra_deg) || !std::isfinite(anchor.distortion)) {
        return decision;
    }

    // Do not obstruct improvements in structural/trace safety. The joint
    // anchor only vetoes would-be Merit replacements comparable at that level.
    if (options.hard_reject_structural_failures) {
        const bool trial_structural_failure =
            trial.score.ray_path_structural_failure_fields > 0;
        const bool parent_structural_failure =
            parent.score.ray_path_structural_failure_fields > 0;
        if (trial_structural_failure != parent_structural_failure) {
            return decision;
        }
    }
    if (trial.trace_feasible != parent.trace_feasible) {
        return decision;
    }
    if (!trial.trace_feasible &&
        (finite_less(trial.trace_violation, parent.trace_violation) ||
         finite_less(parent.trace_violation, trial.trace_violation))) {
        return decision;
    }

    const double trial_cra = trial.score.max_chief_ray_angle;
    const double trial_distortion = distortion_magnitude(trial);
    if (!std::isfinite(trial_cra) || !std::isfinite(trial_distortion)) {
        return decision;
    }
    const double cra_target = options.joint_constraint_anchor_cra_target_deg;
    const double cra_activation_upper =
        cra_target + options.joint_constraint_anchor_cra_tolerance_deg;
    const double cra_limit = std::min(
        cra_activation_upper,
        std::max(
            cra_target,
            anchor.cra_deg +
                options.joint_constraint_anchor_cra_loss_budget_deg));
    const double distortion_target =
        options.joint_constraint_anchor_distortion_target;
    const double distortion_activation_upper =
        distortion_target +
        options.joint_constraint_anchor_distortion_tolerance;
    const double distortion_limit = std::min(
        distortion_activation_upper,
        std::max(
            distortion_target,
            anchor.distortion +
                options.joint_constraint_anchor_distortion_loss_budget));
    decision.block_cra = trial_cra > cra_limit + 1.0e-12;
    decision.block_distortion =
        trial_distortion > distortion_limit + 1.0e-12;
    return decision;
}

std::vector<int> sorted_fitness_indices(
    const std::vector<CandidateFitness>& fitness,
    const AdaptiveHybridDeDlsOptions& options) {
    std::vector<int> order(fitness.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&fitness, &options](int a, int b) {
        return candidate_better(
            fitness[static_cast<std::size_t>(a)],
            fitness[static_cast<std::size_t>(b)],
            options);
    });
    return order;
}

}  // namespace optimizer_detail

}  // namespace optiland
