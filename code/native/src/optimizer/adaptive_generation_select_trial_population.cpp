/*
 * Select adaptive trial survivors under fitness, anchor, family and active-niche constraints.
 * The generation context carries ordered trials/sampled parameters; run_ owns incumbents and archives.
 * Preserve acceptance precedence and success-sample order used to update adaptive F/CR memories.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

void AdaptiveSession::select_trial_population(const SelectTrialPopulationContext& frame_) {
    auto& outer = frame_.outer;
    auto& epoch_pbest_stats = frame_.epoch_pbest_stats;
    auto& epoch_retired_trial_reject_count = frame_.epoch_retired_trial_reject_count;
    auto& deferred_niche_candidates = frame_.deferred_niche_candidates;
    auto& active_niche_state = frame_.active_niche_state;
    auto& active_niche_floor_blocked = frame_.active_niche_floor_blocked;
    auto& active_niche_family_cap_blocked = frame_.active_niche_family_cap_blocked;
    auto& trials = frame_.trials;
    auto& sampled_f = frame_.sampled_f;
    auto& sampled_cr = frame_.sampled_cr;
    auto& trial_fitness = frame_.trial_fitness;
    
    auto& success_f = frame_.success_f;
    auto& success_cr = frame_.success_cr;
    auto& success_delta = frame_.success_delta;
    auto& accepted = frame_.accepted;
    auto& selection_family_gate_seconds = frame_.selection_family_gate_seconds;
    auto& selection_compare_seconds = frame_.selection_compare_seconds;
    auto& selection_update_seconds = frame_.selection_update_seconds;
    auto& selection_basin_seconds = frame_.selection_basin_seconds;
    auto& audit_trial_same_topology_count = frame_.audit_trial_same_topology_count;
    auto& audit_trial_within_family_count = frame_.audit_trial_within_family_count;
    auto& audit_trial_cross_family_count = frame_.audit_trial_cross_family_count;
    auto& audit_trial_unknown_topology_count = frame_.audit_trial_unknown_topology_count;
    auto& audit_accepted_same_topology_count = frame_.audit_accepted_same_topology_count;
    auto& audit_accepted_within_family_count = frame_.audit_accepted_within_family_count;
    auto& audit_accepted_cross_family_count = frame_.audit_accepted_cross_family_count;
    auto& audit_accepted_unknown_topology_count = frame_.audit_accepted_unknown_topology_count;
    auto& exact_escape_generation_crossing_accepted = frame_.exact_escape_generation_crossing_accepted;
    auto& family_escape_generation_crossing_accepted = frame_.family_escape_generation_crossing_accepted;
    auto& family_escape_generation_novelty_accepted = frame_.family_escape_generation_novelty_accepted;

for (int i = 0; i < run_.pop_size; ++i) {
                const CandidateFitness& parent = run_.fitness[static_cast<std::size_t>(i)];
                if (run_.evaluation_certificate_mode == 2 && run_.evaluation_batch_certificates[i].reason > 0) {
                    const auto& proof = run_.evaluation_batch_certificates[i];
                    if (proof.reason==3 && (!proof.merit_eligible || !parent.trace_feasible ||
                        !(proof.merit_lower>parent.score.merit+std::max(0.0,parent.avoidance_penalty)+
                            1.0e-8*std::max(1.0,std::abs(proof.parent_adjusted_merit)))))
                        throw std::runtime_error("Conditional RMS certificate parent precondition was lost");
                    const bool trace_dominated = !parent.trace_feasible && proof.reason == 2 &&
                        std::isfinite(parent.trace_violation) &&
                        proof.trace_violation_lower > parent.trace_violation +
                            1.0e-8 * std::max(1.0, std::abs(parent.trace_violation));
                    if (parent.score.ray_path_structural_failure_fields != 0 ||
                        !(parent.trace_feasible || trace_dominated))
                        throw std::runtime_error("Certificate parent precondition was lost");
                    continue;
                }
                const CandidateFitness& trial = trial_fitness[static_cast<std::size_t>(i)];
                FamilyTransitionAuditEvent transition_audit_event;
                bool transition_audit_ready = false;
                bool transition_audit_unknown = false;
                if (options.family_transition_audit) {
                    transition_audit_ready = true;
                    transition_audit_event.event = "accepted_transition";
                    transition_audit_event.epoch = outer;
                    transition_audit_event.generation = run_.total_de_generations + 1;
                    transition_audit_event.population_index = i;
                    transition_audit_event.parent_topology_key = parent.topology_key;
                    transition_audit_event.trial_topology_key = trial.topology_key;
                    transition_audit_event.parent_semantic_key = parent.semantic_key;
                    transition_audit_event.trial_semantic_key = trial.semantic_key;
                    transition_audit_event.parent_descriptor_key = parent.descriptor_key;
                    transition_audit_event.trial_descriptor_key = trial.descriptor_key;
                    transition_audit_event.parent_merit = parent.score.merit;
                    transition_audit_event.trial_merit = trial.score.merit;
                    transition_audit_event.parent_feasible = parent.feasible;
                    transition_audit_event.trial_feasible = trial.feasible;
                    transition_audit_event.parent_trace_feasible = parent.trace_feasible;
                    transition_audit_event.trial_trace_feasible = trial.trace_feasible;
                    transition_audit_event.structural_key_changed =
                        parent.topology_key != trial.topology_key;
                    transition_audit_event.semantic_key_changed =
                        parent.semantic_key != trial.semantic_key;
                    if (parent.topology_key.empty() || trial.topology_key.empty()) {
                        transition_audit_unknown = true;
                        ++audit_trial_unknown_topology_count;
                    } else if (!transition_audit_event.structural_key_changed) {
                        ++audit_trial_same_topology_count;
                    } else {
                        transition_audit_event.family_similarity =
                            weighted_structural_family_similarity(
                                parent.topology_key,
                                trial.topology_key);
                        transition_audit_event.shape_difference =
                            structural_topology_shape_diff_count(
                                parent.topology_key,
                                trial.topology_key);
                        transition_audit_event.within_family =
                            transition_audit_event.family_similarity >=
                                options.family_transition_similarity_threshold &&
                            (options.family_transition_max_shape_diff < 0 ||
                             transition_audit_event.shape_difference <=
                                 options.family_transition_max_shape_diff);
                        if (transition_audit_event.within_family) {
                            ++audit_trial_within_family_count;
                        } else {
                            ++audit_trial_cross_family_count;
                        }
                    }
                }
                std::vector<double> trial_scaled_for_retired_filter;
                if (this->active_plateau_retire_filter_enabled()) {
                    trial_scaled_for_retired_filter = population_row(trials, i, run_.dim);
                    if (this->active_plateau_fitness_matches_retired(
                            trial_scaled_for_retired_filter,
                            trial)) {
                        ++epoch_retired_trial_reject_count;
                        continue;
                    }
                }
                if (this->active_slot_uses_strict_frozen_family(i)) {
                    const auto family_gate_start = Clock::now();
                    const std::string frozen_key =
                        this->active_slot_frozen_family_key_for_index(i);
                    const std::vector<double> trial_scaled =
                        trial_scaled_for_retired_filter.empty()
                            ? population_row(trials, i, run_.dim)
                            : trial_scaled_for_retired_filter;
                    const std::string trial_key = this->fitness_diversity_key(trial);
                    const bool family_match = this->active_family_match_population_to_key(
                            trial_key,
                            trial_scaled,
                            frozen_key,
                            nullptr,
                            this->active_mating_allow_similarity());
                    selection_family_gate_seconds += seconds_since(family_gate_start);
                    if (!family_match) {
                        continue;
                    }
                }
                bool exact_escape_crossing_trial = false;
                bool family_escape_crossing_trial = false;
                double family_escape_parent_similarity = 0.0;
                double family_escape_trial_similarity = 0.0;
                if (run_.geometry_memory_exact_escape_active) {
                    const std::string parent_history_key =
                        this->fitness_diversity_key(parent);
                    const std::string trial_history_key =
                        this->fitness_diversity_key(trial);
                    exact_escape_crossing_trial =
                        this->geometry_memory_key_is_exact(parent_history_key) &&
                        !trial_history_key.empty() &&
                        !this->geometry_memory_key_is_exact(trial_history_key);
                    if (exact_escape_crossing_trial) {
                        family_escape_parent_similarity = 1.0;
                        family_escape_trial_similarity =
                            this->geometry_memory_key_max_similarity(trial_history_key);
                        ++epoch_pbest_stats
                              .geometry_memory_exact_escape_crossing_trial;
                    }
                }
                if (run_.geometry_memory_family_escape_active) {
                    const std::string parent_history_key =
                        this->fitness_diversity_key(parent);
                    const std::string trial_history_key =
                        this->fitness_diversity_key(trial);
                    family_escape_parent_similarity =
                        this->geometry_memory_key_max_similarity(parent_history_key);
                    family_escape_trial_similarity =
                        this->geometry_memory_key_max_similarity(trial_history_key);
                    family_escape_crossing_trial =
                        !parent_history_key.empty() &&
                        !trial_history_key.empty() &&
                        family_escape_parent_similarity + 1.0e-12 >=
                            options.geometry_memory_difference_key_similarity_threshold &&
                        family_escape_trial_similarity + 1.0e-12 <
                            options.geometry_memory_difference_key_similarity_threshold;
                    if (family_escape_crossing_trial) {
                        ++epoch_pbest_stats.geometry_memory_family_escape_crossing_trial;
                    }
                }
                const auto compare_start = Clock::now();
                const EflSignPriorityDecision efl_sign_decision =
                    efl_sign_priority_decision(
                        trial,
                        parent,
                        options,
                        owner.merit_config_.target_efl);
                const bool blocked_by_efl_sign_priority =
                    efl_sign_decision == EflSignPriorityDecision::PreferParent;
                const bool blocked_by_cra_worsening_guard =
                    efl_sign_decision == EflSignPriorityDecision::NoPreference &&
                    cra_worsening_guard_blocks_trial(trial, parent, options);
                const bool accepted_by_normal_compare =
                    efl_sign_decision == EflSignPriorityDecision::PreferTrial ||
                    (efl_sign_decision == EflSignPriorityDecision::NoPreference &&
                     candidate_better(trial, parent, options));
                const JointConstraintAnchorDecision joint_anchor_decision =
                    efl_sign_decision == EflSignPriorityDecision::NoPreference &&
                            accepted_by_normal_compare &&
                            !blocked_by_cra_worsening_guard &&
                            static_cast<std::size_t>(i) <
                                run_.joint_constraint_anchors.size()
                        ? joint_constraint_anchor_decision(
                              trial,
                              parent,
                              run_.joint_constraint_anchors[static_cast<std::size_t>(i)],
                              options)
                        : JointConstraintAnchorDecision{};
                const bool blocked_by_joint_constraint_anchor =
                    joint_anchor_decision.blocked();
                const bool accepted_by_compare =
                    accepted_by_normal_compare &&
                    !blocked_by_cra_worsening_guard &&
                    !blocked_by_joint_constraint_anchor;
                if (efl_sign_decision == EflSignPriorityDecision::PreferTrial) {
                    ++run_.epoch_efl_sign_priority_promoted_trial_count;
                } else if (blocked_by_efl_sign_priority) {
                    ++run_.epoch_efl_sign_priority_blocked_trial_count;
                }
                if (blocked_by_cra_worsening_guard) {
                    ++run_.epoch_cra_worsening_guard_blocked_trial_count;
                }
                if (blocked_by_joint_constraint_anchor) {
                    if (joint_anchor_decision.block_cra &&
                        joint_anchor_decision.block_distortion) {
                        ++run_.epoch_joint_constraint_anchor_blocked_both_count;
                    } else if (joint_anchor_decision.block_cra) {
                        ++run_.epoch_joint_constraint_anchor_blocked_cra_count;
                    } else {
                        ++run_.epoch_joint_constraint_anchor_blocked_distortion_count;
                    }
                }
                bool accepted_by_annealing = false;
                
                bool accepted_by_novelty = false;
                if (!accepted_by_compare && !accepted_by_annealing &&
                    !blocked_by_efl_sign_priority &&
                    !blocked_by_cra_worsening_guard &&
                    !blocked_by_joint_constraint_anchor &&
                    options.geometry_memory_family_escape_novelty_acceptance &&
                    (exact_escape_crossing_trial ||
                     family_escape_crossing_trial)) {
                    const bool feasibility_not_worse =
                        trial.trace_feasible &&
                        trial.score.trace_ok &&
                        (!parent.feasible || trial.feasible) &&
                        (!std::isfinite(parent.total_violation) ||
                         (std::isfinite(trial.total_violation) &&
                          trial.total_violation <=
                              parent.total_violation + 1.0e-12));
                    const bool merit_bounded =
                        std::isfinite(parent.score.merit) &&
                        std::isfinite(trial.score.merit) &&
                        trial.score.merit <=
                            std::max(1.0e-12, parent.score.merit) *
                                std::max(
                                    1.0,
                                    options.geometry_memory_family_escape_novelty_max_merit_ratio);
                    const bool similarity_drop_sufficient =
                        family_escape_parent_similarity -
                            family_escape_trial_similarity + 1.0e-12 >=
                        std::clamp(
                            options.geometry_memory_family_escape_novelty_min_similarity_drop,
                            0.0,
                            1.0);
                    if (feasibility_not_worse && merit_bounded &&
                        similarity_drop_sufficient) {
                        if (exact_escape_crossing_trial) {
                            ++epoch_pbest_stats
                                  .geometry_memory_exact_escape_novelty_eligible;
                        }
                        if (family_escape_crossing_trial) {
                            ++epoch_pbest_stats
                                  .geometry_memory_family_escape_novelty_eligible;
                        }
                        if (family_escape_generation_novelty_accepted <
                            std::max(
                                1,
                                options.geometry_memory_family_escape_novelty_max_per_generation)) {
                            accepted_by_novelty = true;
                        }
                    }
                }
                selection_compare_seconds += seconds_since(compare_start);
                if (blocked_by_efl_sign_priority ||
                    blocked_by_cra_worsening_guard ||
                    blocked_by_joint_constraint_anchor ||
                    (!accepted_by_compare && !accepted_by_annealing &&
                     !accepted_by_novelty)) {
                    continue;
                }
                int parent_niche = -1;
                int trial_niche = -1;
                if (active_niche_state.survival_enabled &&
                    active_niche_state.configured &&
                    run_.population_niche_id.size() == static_cast<std::size_t>(run_.pop_size)) {
                    parent_niche = run_.population_niche_id[static_cast<std::size_t>(i)];
                    trial_niche =
                        this->active_niche_index_for_fitness(trial, active_niche_state);
                    if (parent_niche != trial_niche) {
                        bool blocked_by_niche = false;
                        if (parent_niche >= 0 &&
                            parent_niche <
                                static_cast<int>(
                                    active_niche_state.member_counts.size()) &&
                            active_niche_state.member_counts[
                                static_cast<std::size_t>(parent_niche)] <=
                                active_niche_state.min_quota) {
                            blocked_by_niche = true;
                            ++active_niche_floor_blocked;
                        }
                        if (!blocked_by_niche &&
                            trial_niche >= 0 &&
                            trial_niche <
                                static_cast<int>(
                                    active_niche_state.member_counts.size()) &&
                            active_niche_state.member_counts[
                                static_cast<std::size_t>(trial_niche)] >=
                                this->active_niche_family_cap_for(
                                    active_niche_state,
                                    trial_niche)) {
                            blocked_by_niche = true;
                            ++active_niche_family_cap_blocked;
                        }
                        if (blocked_by_niche) {
                            DeferredNicheCandidate deferred;
                            deferred.source_index = i;
                            deferred.niche_id = trial_niche;
                            deferred.scaled =
                                trial_scaled_for_retired_filter.empty()
                                    ? population_row(trials, i, run_.dim)
                                    : trial_scaled_for_retired_filter;
                            deferred.fitness = trial;
                            deferred.sampled_f = sampled_f[static_cast<std::size_t>(i)];
                            deferred.sampled_cr = sampled_cr[static_cast<std::size_t>(i)];
                            deferred_niche_candidates.push_back(std::move(deferred));
                            continue;
                        }
                    }
                }
                const auto update_start = Clock::now();
                if (accepted_by_novelty) {
                    ++family_escape_generation_novelty_accepted;
                    if (exact_escape_crossing_trial) {
                        ++epoch_pbest_stats
                              .geometry_memory_exact_escape_novelty_accepted;
                    }
                    if (family_escape_crossing_trial) {
                        ++epoch_pbest_stats
                              .geometry_memory_family_escape_novelty_accepted;
                    }
                }
                if (run_.geometry_memory_exact_escape_active &&
                    exact_escape_crossing_trial) {
                    ++exact_escape_generation_crossing_accepted;
                }
                if (run_.geometry_memory_family_escape_active &&
                    family_escape_crossing_trial) {
                    ++family_escape_generation_crossing_accepted;
                }
                if (transition_audit_ready) {
                    transition_audit_event.accepted_by_compare = accepted_by_compare;
                    transition_audit_event.accepted_by_annealing = accepted_by_annealing;
                    if (transition_audit_unknown) {
                        ++audit_accepted_unknown_topology_count;
                    } else if (!transition_audit_event.structural_key_changed) {
                        ++audit_accepted_same_topology_count;
                    } else if (transition_audit_event.within_family) {
                        ++audit_accepted_within_family_count;
                    } else {
                        ++audit_accepted_cross_family_count;
                    }
                    if (transition_audit_event.structural_key_changed &&
                        !transition_audit_unknown) {
                        const std::vector<double> parent_scaled =
                            population_row(run_.population, i, run_.dim);
                        const std::vector<double> trial_scaled =
                            population_row(trials, i, run_.dim);
                        transition_audit_event.structure_distance =
                            scaled_vector_distance(
                                owner.variables_,
                                parent_scaled,
                                trial_scaled,
                                DistanceMetric::StructureBoundNormalized,
                                run_.internal_mode,
                                &owner.base_,
                                options.basins.air_thickness_distance_cap);
                        transition_audit_event.full_distance =
                            scaled_vector_distance(
                                owner.variables_,
                                parent_scaled,
                                trial_scaled,
                                DistanceMetric::BoundNormalized,
                                run_.internal_mode,
                                &owner.base_,
                                options.basins.air_thickness_distance_cap);
                        options.family_transition_audit(transition_audit_event);
                    }
                }
                this->stage_traceable_refresh_archive(
                    population_row(run_.population, i, run_.dim),
                    parent,
                    "replaced_parent");
                if (run_.variant_uses_archive) {
                    run_.external_archive.push_back(population_row(run_.population, i, run_.dim));
                    run_.external_archive_keys.push_back(this->fitness_diversity_key(parent));
                }
                for (int j = 0; j < run_.dim; ++j) {
                    run_.population[static_cast<std::size_t>(i) * run_.dim + j] =
                        trials[static_cast<std::size_t>(i) * run_.dim + j];
                }
                run_.fitness[static_cast<std::size_t>(i)] = trial;
                run_.fitness[static_cast<std::size_t>(i)].population_index = i;
                if (options.joint_constraint_anchor_guard_enabled &&
                    static_cast<std::size_t>(i) <
                        run_.joint_constraint_anchors.size()) {
                    JointConstraintAnchorState& anchor =
                        run_.joint_constraint_anchors[static_cast<std::size_t>(i)];
                    const bool was_active = anchor.active;
                    const bool changed =
                        latch_or_improve_joint_constraint_anchor(
                            anchor,
                            trial,
                            options);
                    if (changed) {
                        if (was_active) {
                            ++run_.epoch_joint_constraint_anchor_updated_count;
                        } else {
                            ++run_.epoch_joint_constraint_anchor_activated_count;
                        }
                    }
                }
                if (active_niche_state.configured) {
                    trial_niche =
                        this->active_niche_index_for_fitness(trial, active_niche_state);
                    this->apply_active_niche_membership_change(
                        i,
                        trial_niche,
                        active_niche_state);
                }
                const double old_merit = parent.score.merit;
                const double new_merit = trial.score.merit;
                double improvement = (std::isfinite(old_merit) && std::isfinite(new_merit))
                                         ? std::abs(old_merit - new_merit)
                                         : 1.0;
                if (parent.feasible != trial.feasible ||
                    parent.trace_feasible != trial.trace_feasible) {
                    improvement = std::max(improvement, 1.0);
                }
                improvement = std::max(1.0e-12, improvement);
                if (run_.variant_uses_de && !accepted_by_novelty) {
                    success_f.push_back(sampled_f[static_cast<std::size_t>(i)]);
                    success_cr.push_back(sampled_cr[static_cast<std::size_t>(i)]);
                    success_delta.push_back(improvement);
                }
                
                selection_update_seconds += seconds_since(update_start);
                const auto basin_start = Clock::now();
                this->consider_basin(population_row(run_.population, i, run_.dim), run_.fitness[static_cast<std::size_t>(i)],
                               "trial", false);
                this->stage_traceable_refresh_archive(
                    population_row(run_.population, i, run_.dim),
                    run_.fitness[static_cast<std::size_t>(i)],
                    "accepted_trial");
                selection_basin_seconds += seconds_since(basin_start);
                ++accepted;
            }
}

} } // namespace optiland::optimizer_detail
