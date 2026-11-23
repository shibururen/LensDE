#pragma once
#include "support_types.hpp"

namespace optiland { namespace optimizer_detail {

bool efl_sign_matches_target(double efl, double target_efl);

EflSignPriorityDecision efl_sign_priority_decision(
    const CandidateFitness& trial,
    const CandidateFitness& parent,
    const AdaptiveHybridDeDlsOptions& options,
    double target_efl);

bool cra_worsening_guard_blocks_trial(
    const CandidateFitness& trial,
    const CandidateFitness& parent,
    const AdaptiveHybridDeDlsOptions& options);

double distortion_magnitude(const CandidateFitness& fitness);

bool joint_constraint_anchor_candidate_qualifies(
    const CandidateFitness& candidate,
    const AdaptiveHybridDeDlsOptions& options);

bool latch_or_improve_joint_constraint_anchor(
    JointConstraintAnchorState& anchor,
    const CandidateFitness& candidate,
    const AdaptiveHybridDeDlsOptions& options);

JointConstraintAnchorDecision joint_constraint_anchor_decision(
    const CandidateFitness& trial,
    const CandidateFitness& parent,
    const JointConstraintAnchorState& anchor,
    const AdaptiveHybridDeDlsOptions& options);

std::vector<int> sorted_fitness_indices(
    const std::vector<CandidateFitness>& fitness,
    const AdaptiveHybridDeDlsOptions& options);

} }  // namespace optiland::optimizer_detail
