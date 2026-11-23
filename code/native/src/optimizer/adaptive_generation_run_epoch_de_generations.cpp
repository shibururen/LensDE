/*
 * Run the inner adaptive generation loop and connect trial generation, evaluation and survival.
 * The outer epoch supplies reference contexts; run_ retains LPSR, archive, memory and convergence state.
 * Keep evaluation/selection, archive updates and observation hooks at their existing generation boundaries.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

void AdaptiveSession::run_epoch_de_generations(const RunEpochDeGenerationsContext& frame_) {
    auto& outer = frame_.outer;
    auto& epoch_pbest_stats = frame_.epoch_pbest_stats;
    auto& epoch_mating_stats = frame_.epoch_mating_stats;
    auto& epoch_de_generations = frame_.epoch_de_generations;
    auto& epoch_accepted_trials = frame_.epoch_accepted_trials;
    auto& epoch_retired_trial_reject_count = frame_.epoch_retired_trial_reject_count;
    auto& family_escape_configured = frame_.family_escape_configured;
    auto& exact_escape_configured = frame_.exact_escape_configured;
    auto& exact_escape_resident_generations = frame_.exact_escape_resident_generations;
    auto& exact_escape_pulse_remaining = frame_.exact_escape_pulse_remaining;
    auto& exact_escape_cooldown_remaining = frame_.exact_escape_cooldown_remaining;
    auto& exact_escape_resident_key = frame_.exact_escape_resident_key;
    auto& family_escape_stagnant_generations = frame_.family_escape_stagnant_generations;
    auto& family_escape_pulse_remaining = frame_.family_escape_pulse_remaining;
    auto& family_escape_cooldown_remaining = frame_.family_escape_cooldown_remaining;
    auto& family_escape_reference_merit = frame_.family_escape_reference_merit;
    auto& deferred_niche_candidates = frame_.deferred_niche_candidates;

for (int gen = 1; gen <= run_.inner_generations; ++gen) {
            if (options.max_evaluations > 0 &&
                run_.timing.evaluated_candidates >= options.max_evaluations) {
                break;
            }

            const int current_de_generation = run_.total_de_generations + 1;
            // Pair reuse is concentrated in a short generation window.  A
            // four-generation window captures cross-generation reuse without
            // retaining O(generations * pop^2) entries.
            constexpr int topology_pair_cache_generation_window = 4;
            if ((current_de_generation - 1) %
                    topology_pair_cache_generation_window ==
                0) {
                run_.topology_similarity_cache.clear();
                run_.topology_shape_diff_cache.clear();
            }
            if (options.avoided_topology_start_generation > 0 &&
                current_de_generation ==
                    options.avoided_topology_start_generation) {
                for (CandidateFitness& candidate : run_.fitness) {
                    this->apply_topology_avoidance(candidate, current_de_generation);
                }
                this->apply_topology_avoidance(run_.best_fitness, current_de_generation);
                this->update_best_from_population();
            }

            if (exact_escape_configured) {
                const int raw_best_index = this->raw_merit_best_index();
                const std::string raw_best_key =
                    raw_best_index >= 0
                        ? this->fitness_diversity_key(
                              run_.fitness[static_cast<std::size_t>(raw_best_index)])
                        : std::string{};
                const bool raw_best_exact =
                    this->geometry_memory_key_is_exact(raw_best_key);
                if (raw_best_exact) {
                    ++epoch_pbest_stats
                          .geometry_memory_exact_escape_resident_generation;
                }
                if (exact_escape_pulse_remaining <= 0) {
                    run_.geometry_memory_exact_escape_active = false;
                    if (exact_escape_cooldown_remaining > 0) {
                        --exact_escape_cooldown_remaining;
                        exact_escape_resident_generations = 0;
                        exact_escape_resident_key.clear();
                    } else if (raw_best_exact) {
                        if (raw_best_key == exact_escape_resident_key) {
                            ++exact_escape_resident_generations;
                        } else {
                            exact_escape_resident_key = raw_best_key;
                            exact_escape_resident_generations = 1;
                        }
                        if (exact_escape_resident_generations >=
                            std::max(
                                1,
                                options
                                    .geometry_memory_exact_escape_residence_generations)) {
                            exact_escape_pulse_remaining = std::max(
                                1,
                                options
                                    .geometry_memory_exact_escape_pulse_generations);
                            run_.geometry_memory_exact_escape_active = true;
                            exact_escape_resident_generations = 0;
                            ++epoch_pbest_stats
                                  .geometry_memory_exact_escape_activation;
                        }
                    } else {
                        exact_escape_resident_generations = 0;
                        exact_escape_resident_key.clear();
                    }
                } else {
                    run_.geometry_memory_exact_escape_active = true;
                }
                if (run_.geometry_memory_exact_escape_active) {
                    ++epoch_pbest_stats
                          .geometry_memory_exact_escape_active_generation;
                }
            } else {
                run_.geometry_memory_exact_escape_active = false;
            }

            if (family_escape_configured) {
                const double current_merit = run_.best_fitness.score.merit;
                const double min_relative_improvement = std::max(
                    0.0,
                    options.geometry_memory_family_escape_min_relative_improvement);
                const bool meaningful_improvement =
                    !std::isfinite(family_escape_reference_merit) ||
                    (std::isfinite(current_merit) &&
                     current_merit <
                         family_escape_reference_merit *
                             (1.0 - min_relative_improvement));
                if (meaningful_improvement) {
                    family_escape_reference_merit = current_merit;
                    family_escape_stagnant_generations = 0;
                } else if (family_escape_pulse_remaining <= 0 &&
                           family_escape_cooldown_remaining <= 0) {
                    ++family_escape_stagnant_generations;
                }
                if (family_escape_pulse_remaining <= 0) {
                    run_.geometry_memory_family_escape_active = false;
                    if (family_escape_cooldown_remaining > 0) {
                        --family_escape_cooldown_remaining;
                        family_escape_reference_merit = current_merit;
                        family_escape_stagnant_generations = 0;
                    } else if (
                        family_escape_stagnant_generations >=
                        std::max(
                            1,
                            options.geometry_memory_family_escape_stagnation_generations)) {
                        family_escape_pulse_remaining = std::max(
                            1,
                            options.geometry_memory_family_escape_pulse_generations);
                        run_.geometry_memory_family_escape_active = true;
                        family_escape_reference_merit = current_merit;
                        family_escape_stagnant_generations = 0;
                        ++epoch_pbest_stats.geometry_memory_family_escape_activation;
                    }
                } else {
                    run_.geometry_memory_family_escape_active = true;
                }
                if (run_.geometry_memory_family_escape_active) {
                    ++epoch_pbest_stats.geometry_memory_family_escape_active_generation;
                }
            } else {
                run_.geometry_memory_family_escape_active = false;
            }

            const auto order = sorted_fitness_indices(run_.fitness, options);
            const int pbest_count = std::clamp(
                static_cast<int>(std::ceil(std::clamp(options.pbest_fraction, 0.01, 1.0) *
                                           static_cast<double>(run_.pop_size))),
                1,
                run_.pop_size);
            int geometry_memory_novelty_guide_index = -1;
            double geometry_memory_novelty_guide_similarity =
                std::numeric_limits<double>::infinity();
            const bool geometry_memory_novelty_escape_active =
                run_.geometry_memory_exact_escape_active ||
                run_.geometry_memory_family_escape_active;
            if (geometry_memory_novelty_escape_active && !order.empty()) {
                const int quality_limit = std::clamp(
                    static_cast<int>(std::ceil(
                        std::clamp(
                            options.geometry_memory_novelty_guide_quality_fraction,
                            0.01,
                            1.0) *
                        static_cast<double>(order.size()))),
                    1,
                    static_cast<int>(order.size()));
                for (int rank = 0; rank < quality_limit; ++rank) {
                    const int candidate = order[static_cast<std::size_t>(rank)];
                    if (candidate < 0 || candidate >= run_.pop_size) {
                        continue;
                    }
                    const CandidateFitness& candidate_fitness =
                        run_.fitness[static_cast<std::size_t>(candidate)];
                    if (!candidate_fitness.trace_feasible ||
                        !candidate_fitness.score.trace_ok ||
                        !std::isfinite(candidate_fitness.score.merit)) {
                        continue;
                    }
                    const std::string candidate_key =
                        this->fitness_diversity_key(candidate_fitness);
                    if (candidate_key.empty()) {
                        continue;
                    }
                    const double similarity =
                        this->geometry_memory_key_max_similarity(candidate_key);
                    if (geometry_memory_novelty_guide_index < 0 ||
                        similarity <
                            geometry_memory_novelty_guide_similarity - 1.0e-12 ||
                        (std::abs(
                             similarity -
                             geometry_memory_novelty_guide_similarity) <= 1.0e-12 &&
                         candidate_better(
                             candidate_fitness,
                             run_.fitness[static_cast<std::size_t>(
                                 geometry_memory_novelty_guide_index)],
                             options))) {
                        geometry_memory_novelty_guide_index = candidate;
                        geometry_memory_novelty_guide_similarity = similarity;
                    }
                }
                if (geometry_memory_novelty_guide_index >= 0) {
                    ++epoch_pbest_stats.geometry_memory_novelty_guide_generation;
                    if (geometry_memory_novelty_guide_similarity + 1.0e-12 <
                        options.geometry_memory_difference_key_similarity_threshold) {
                        ++epoch_pbest_stats.geometry_memory_novelty_guide_unseen_generation;
                    }
                }
            }
            run_.geometry_memory_population_near_cache.assign(
                static_cast<std::size_t>(run_.pop_size),
                static_cast<signed char>(-1));
            run_.geometry_memory_difference_index_cache.assign(
                static_cast<std::size_t>(run_.pop_size),
                -2);
            std::vector<double> generation_best_scaled = run_.best_scaled;
            if (this->active_plateau_fitness_matches_retired(run_.best_scaled, run_.best_fitness)) {
                for (int index : order) {
                    if (index < 0 || index >= run_.pop_size) {
                        continue;
                    }
                    const std::vector<double> candidate_scaled =
                        population_row(run_.population, index, run_.dim);
                    if (!this->active_plateau_fitness_matches_retired(
                            candidate_scaled,
                            run_.fitness[static_cast<std::size_t>(index)])) {
                        generation_best_scaled = std::move(candidate_scaled);
                        break;
                    }
                }
            }
            LocalMatingPools local_mating_pools;
            ActiveMatingQuotaState active_mating_quota;
            ActiveNicheGenerationState active_niche_state;
            int active_niche_floor_blocked = 0;
            int active_niche_family_cap_blocked = 0;
            int active_niche_repair_moves = 0;
            int active_niche_rebalance_candidates = 0;
            int active_niche_rebalance_moves = 0;
            if (run_.variant_uses_de) {
                const auto mating_pool_start = Clock::now();
                local_mating_pools = this->build_local_mating_pools();
                if (run_.detailed_trial_timing) {
                    run_.timing.trial_mating_pool_seconds +=
                        seconds_since(mating_pool_start);
                }
                active_mating_quota = this->make_active_mating_quota_state(local_mating_pools);
                this->refresh_active_niche_membership(
                    local_mating_pools,
                    run_.active_niche_membership_reset_pending);
                active_niche_state =
                    this->make_active_niche_generation_state();
                const int hard_cap_releases =
                    this->release_active_niche_hard_cap_excess(active_niche_state);
                if (hard_cap_releases > 0) {
                    active_niche_state =
                        this->make_active_niche_generation_state();
                }
                const int soft_fill_interval =
                    std::max(1, options.basins.active_niche_rebalance_interval);
                const bool soft_fill_due =
                    gen == 1 || gen % soft_fill_interval == 0;
                if ((this->active_niche_soft_fill_enabled() ||
                     this->active_focus_soft_fill_enabled()) &&
                    active_niche_state.configured &&
                    soft_fill_due) {
                    const int soft_fill_moves =
                        this->active_soft_fill_population(active_niche_state);
                    if (soft_fill_moves > 0) {
                        this->sync_merits();
                        this->trim_external_archive();
                        this->update_best_from_population();
                        const auto rebuilt_mating_pool_start = Clock::now();
                        local_mating_pools = this->build_local_mating_pools();
                        if (run_.detailed_trial_timing) {
                            run_.timing.trial_mating_pool_seconds +=
                                seconds_since(rebuilt_mating_pool_start);
                        }
                        active_mating_quota =
                            this->make_active_mating_quota_state(local_mating_pools);
                        this->refresh_active_niche_membership(
                            local_mating_pools,
                            false);
                        active_niche_state =
                            this->make_active_niche_generation_state();
                    }
                }
                this->publish_active_niche_stats(active_niche_state, 0, 0, 0, 0, 0, 0);
            }
            const double trial_detail_before =
                run_.timing.trial_mating_plan_seconds +
                run_.timing.trial_pbest_seconds +
                run_.timing.trial_mating_select_seconds;
            run_.phase_start = Clock::now();
            std::vector<double> trials(static_cast<std::size_t>(run_.pop_size) * run_.dim, 0.0);
            std::vector<double> sampled_f(static_cast<std::size_t>(run_.pop_size), run_.mu_f);
            std::vector<double> sampled_cr(static_cast<std::size_t>(run_.pop_size), run_.mu_cr);
            std::vector<double> geometry_memory_qohr_step(
                static_cast<std::size_t>(run_.dim),
                0.0);
            
            
            
            
            
    this->generate_trial_population(GenerateTrialPopulationContext{epoch_pbest_stats, epoch_mating_stats, order, pbest_count, geometry_memory_novelty_guide_index, geometry_memory_novelty_guide_similarity, geometry_memory_novelty_escape_active, generation_best_scaled, local_mating_pools, active_mating_quota, trials, sampled_f, sampled_cr, geometry_memory_qohr_step});

            const double trial_generation_seconds = seconds_since(run_.phase_start);
            run_.timing.trial_generation_seconds += trial_generation_seconds;
            if (run_.detailed_trial_timing) {
                const double trial_detail_after =
                    run_.timing.trial_mating_plan_seconds +
                    run_.timing.trial_pbest_seconds +
                    run_.timing.trial_mating_select_seconds;
                run_.timing.trial_mutation_seconds += std::max(
                    0.0,
                    trial_generation_seconds -
                        (trial_detail_after - trial_detail_before));
            }

            run_.phase_start = Clock::now();
            EvaluationLocalReset certificate_context_reset;
            run_.evaluation_batch_certificates.assign(run_.pop_size, EvaluationCertificate{});
            if (run_.evaluation_certificate_allowed) {
                for (int ri = 0; ri < run_.pop_size; ++ri) {
                    const CandidateFitness& rp = run_.fitness[ri];
                    auto& certificate = run_.evaluation_batch_certificates[ri];
                    certificate.eligible = rp.score.ray_path_structural_failure_fields == 0 &&
                        (rp.trace_feasible || std::isfinite(rp.trace_violation));
                    certificate.parent_trace_feasible = rp.trace_feasible;
                    certificate.parent_trace_violation = rp.trace_violation;
                    certificate.parent_adjusted_merit=rp.score.merit+std::max(0.0,rp.avoidance_penalty);
                    certificate.merit_eligible=rp.trace_feasible &&
                        rp.score.ray_path_structural_failure_fields==0 &&
                        unified_scalar_merit_score(rp.score) &&
                        efl_sign_matches_target(rp.score.efl,owner.merit_config_.target_efl) &&
                        std::isfinite(certificate.parent_adjusted_merit);
                }
                evaluation_local_set_context(run_.evaluation_certificate_mode,
                    run_.evaluation_batch_certificates, options.trace_coverage_epsilon);
            }
            std::vector<CandidateFitness> trial_fitness =
                this->evaluate_population_fitness(trials, run_.pop_size, run_.total_de_generations + 1, "trial");
            if (run_.evaluation_certificate_allowed) evaluation_local_set_context(0, {}, 0.0);
            // LENSDE_REPLAY_TRIAL

            
            

            std::vector<double> success_f;
            std::vector<double> success_cr;
            std::vector<double> success_delta;
            int accepted = 0;
            run_.phase_start = Clock::now();
            double selection_family_gate_seconds = 0.0;
            double selection_compare_seconds = 0.0;
            double selection_update_seconds = 0.0;
            double selection_basin_seconds = 0.0;
            int audit_trial_same_topology_count = 0;
            int audit_trial_within_family_count = 0;
            int audit_trial_cross_family_count = 0;
            int audit_trial_unknown_topology_count = 0;
            int audit_accepted_same_topology_count = 0;
            int audit_accepted_within_family_count = 0;
            int audit_accepted_cross_family_count = 0;
            int audit_accepted_unknown_topology_count = 0;
            int exact_escape_generation_crossing_accepted = 0;
            int family_escape_generation_crossing_accepted = 0;
            int family_escape_generation_novelty_accepted = 0;
            
    this->select_trial_population(SelectTrialPopulationContext{outer, epoch_pbest_stats, epoch_retired_trial_reject_count, deferred_niche_candidates, active_niche_state, active_niche_floor_blocked, active_niche_family_cap_blocked, trials, sampled_f, sampled_cr, trial_fitness, success_f, success_cr, success_delta, accepted, selection_family_gate_seconds, selection_compare_seconds, selection_update_seconds, selection_basin_seconds, audit_trial_same_topology_count, audit_trial_within_family_count, audit_trial_cross_family_count, audit_trial_unknown_topology_count, audit_accepted_same_topology_count, audit_accepted_within_family_count, audit_accepted_cross_family_count, audit_accepted_unknown_topology_count, exact_escape_generation_crossing_accepted, family_escape_generation_crossing_accepted, family_escape_generation_novelty_accepted});

            if (options.family_transition_audit) {
                FamilyTransitionAuditEvent generation_audit;
                generation_audit.event = "generation_summary";
                generation_audit.epoch = outer;
                generation_audit.generation = run_.total_de_generations + 1;
                generation_audit.evaluated_trials = run_.pop_size;
                generation_audit.trial_same_topology_count =
                    audit_trial_same_topology_count;
                generation_audit.trial_within_family_count =
                    audit_trial_within_family_count;
                generation_audit.trial_cross_family_count =
                    audit_trial_cross_family_count;
                generation_audit.trial_unknown_topology_count =
                    audit_trial_unknown_topology_count;
                generation_audit.accepted_same_topology_count =
                    audit_accepted_same_topology_count;
                generation_audit.accepted_within_family_count =
                    audit_accepted_within_family_count;
                generation_audit.accepted_cross_family_count =
                    audit_accepted_cross_family_count;
                generation_audit.accepted_unknown_topology_count =
                    audit_accepted_unknown_topology_count;
                options.family_transition_audit(generation_audit);
            }
            
    this->apply_deferred_niche_survival(ApplyDeferredNicheSurvivalContext{deferred_niche_candidates, gen, active_niche_state, active_niche_repair_moves, success_f, success_cr, success_delta, accepted});

            
    this->rebalance_active_niches(gen, active_niche_state, active_niche_rebalance_candidates, active_niche_rebalance_moves);

            epoch_accepted_trials += accepted;
            const auto finalize_start = Clock::now();
            this->sync_merits();
            this->trim_external_archive();
            this->update_best_from_population();
            if (run_.geometry_memory_exact_escape_active) {
                epoch_pbest_stats
                    .geometry_memory_exact_escape_crossing_accepted +=
                    exact_escape_generation_crossing_accepted;
                const int raw_best_index = this->raw_merit_best_index();
                const std::string raw_best_key =
                    raw_best_index >= 0
                        ? this->fitness_diversity_key(
                              run_.fitness[static_cast<std::size_t>(raw_best_index)])
                        : std::string{};
                if (!this->geometry_memory_key_is_exact(raw_best_key)) {
                    ++epoch_pbest_stats
                          .geometry_memory_exact_escape_success;
                    exact_escape_pulse_remaining = 0;
                    exact_escape_cooldown_remaining = std::max(
                        0,
                        options
                            .geometry_memory_exact_escape_cooldown_generations);
                    exact_escape_resident_generations = 0;
                    exact_escape_resident_key.clear();
                    run_.geometry_memory_exact_escape_active = false;
                } else {
                    --exact_escape_pulse_remaining;
                    if (exact_escape_pulse_remaining <= 0) {
                        ++epoch_pbest_stats
                              .geometry_memory_exact_escape_timeout;
                        exact_escape_pulse_remaining = 0;
                        exact_escape_cooldown_remaining = std::max(
                            0,
                            options
                                .geometry_memory_exact_escape_cooldown_generations);
                        exact_escape_resident_generations = 0;
                        exact_escape_resident_key.clear();
                        run_.geometry_memory_exact_escape_active = false;
                    }
                }
            }
            if (run_.geometry_memory_family_escape_active) {
                epoch_pbest_stats.geometry_memory_family_escape_crossing_accepted +=
                    family_escape_generation_crossing_accepted;
                if (family_escape_generation_crossing_accepted > 0) {
                    ++epoch_pbest_stats.geometry_memory_family_escape_success;
                    family_escape_pulse_remaining = 0;
                    family_escape_cooldown_remaining = std::max(
                        0,
                        options.geometry_memory_family_escape_cooldown_generations);
                    family_escape_reference_merit = run_.best_fitness.score.merit;
                    family_escape_stagnant_generations = 0;
                    run_.geometry_memory_family_escape_active = false;
                } else {
                    --family_escape_pulse_remaining;
                    if (family_escape_pulse_remaining <= 0) {
                        family_escape_pulse_remaining = 0;
                        family_escape_cooldown_remaining = std::max(
                            0,
                            options.geometry_memory_family_escape_cooldown_generations);
                        family_escape_reference_merit = run_.best_fitness.score.merit;
                        family_escape_stagnant_generations = 0;
                        run_.geometry_memory_family_escape_active = false;
                    }
                }
            }
            if (active_niche_state.configured) {
                active_niche_state =
                    this->make_active_niche_generation_state();
                const int hard_cap_releases =
                    this->release_active_niche_hard_cap_excess(active_niche_state);
                if (hard_cap_releases > 0) {
                    active_niche_state =
                        this->make_active_niche_generation_state();
                }
                this->publish_active_niche_stats(
                    active_niche_state,
                    active_niche_floor_blocked,
                    active_niche_family_cap_blocked,
                    static_cast<int>(deferred_niche_candidates.size()),
                    active_niche_repair_moves,
                    active_niche_rebalance_candidates,
                    active_niche_rebalance_moves);
            }
            run_.timing.selection_family_gate_seconds += selection_family_gate_seconds;
            run_.timing.selection_compare_seconds += selection_compare_seconds;
            run_.timing.selection_update_seconds += selection_update_seconds;
            run_.timing.selection_basin_seconds += selection_basin_seconds;
            run_.timing.selection_finalize_seconds += seconds_since(finalize_start);
            run_.timing.selection_seconds += seconds_since(run_.phase_start);

            if (!success_f.empty()) {
                double weight_sum = std::accumulate(success_delta.begin(), success_delta.end(), 0.0);
                if (!std::isfinite(weight_sum) || weight_sum <= 0.0) {
                    weight_sum = static_cast<double>(success_f.size());
                    std::fill(success_delta.begin(), success_delta.end(), 1.0);
                }
                double lehmer_num = 0.0;
                double lehmer_den = 0.0;
                double cr_sum = 0.0;
                for (std::size_t i = 0; i < success_f.size(); ++i) {
                    const double w = success_delta[i] / weight_sum;
                    lehmer_num += w * success_f[i] * success_f[i];
                    lehmer_den += w * success_f[i];
                    cr_sum += w * success_cr[i];
                }
                const double c = std::clamp(options.adaptation_rate, 0.0, 1.0);
                if (lehmer_den > 0.0 && std::isfinite(lehmer_den)) {
                    run_.mu_f = std::clamp((1.0 - c) * run_.mu_f + c * (lehmer_num / lehmer_den),
                                      1.0e-6,
                                      1.0);
                }
                if (std::isfinite(cr_sum)) {
                    run_.mu_cr = std::clamp((1.0 - c) * run_.mu_cr + c * cr_sum, 0.0, 1.0);
                }
            }

            ++run_.total_de_generations;
            ++epoch_de_generations;
            this->reduce_population_if_needed();
            if (progress && run_.de_options.progress_every > 0 &&
                run_.total_de_generations % run_.de_options.progress_every == 0) {
                progress(
                    run_.total_de_generations,
                    run_.best_fitness.score,
                    scaled_to_physical(owner.variables_, run_.best_scaled, run_.internal_mode));
            }
            if (!run_.research_snapshot_dir.empty() && run_.de_options.progress_every > 0 &&
                run_.total_de_generations % run_.de_options.progress_every == 0) {
                this->emit_research_snapshot(run_.total_de_generations);
            }
            // LENSDE_REPLAY_GENERATION_END
        }
}

} } // namespace optiland::optimizer_detail
