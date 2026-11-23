/*
 * Fill niche membership quotas and apply candidates deferred by adaptive survival constraints.
 * Selection supplies pending candidates while run_ owns the population, fitness and external archive.
 * Preserve deferred-candidate order and update row, fitness, niche and archive state as one decision.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

void AdaptiveSession::fill_niche_member_quotas(ActiveNicheGenerationState& state, std::vector<FocusFillCandidate>& candidates, std::vector<char>& used_candidate, std::vector<char>& used_target, const std::vector<int>& protected_indices, int& moves) {
for (int niche_id = 0;
                 niche_id < static_cast<int>(state.family_keys.size());
                 ++niche_id) {
                if (!this->family_soft_fill_enabled(state, niche_id)) {
                    continue;
                }
                const bool focus_family = this->family_is_focus(state, niche_id);
                const std::string& family_key =
                    state.family_keys[static_cast<std::size_t>(niche_id)];
                int matching_member_count = 0;
                double anchor_best_merit =
                    std::numeric_limits<double>::infinity();
                for (int idx = 0; idx < run_.pop_size; ++idx) {
                    if (run_.population_niche_id[static_cast<std::size_t>(idx)] !=
                        niche_id) {
                        continue;
                    }
                    if (!this->population_matches_family(idx, family_key)) {
                        continue;
                    }
                    ++matching_member_count;
                    const double merit =
                        run_.fitness[static_cast<std::size_t>(idx)].score.merit;
                    if (std::isfinite(merit)) {
                        anchor_best_merit = std::min(anchor_best_merit, merit);
                    }
                }
                const int target_quota =
                    this->family_soft_fill_target_quota(state, niche_id);
                const int elite_seed_count = this->family_elite_seed_count(state, niche_id);
                int anchor_need =
                    std::max(0, elite_seed_count - matching_member_count);
                for (int pass = 0; pass < 2 && anchor_need > 0; ++pass) {
                    for (int candidate_index = 0;
                         candidate_index < static_cast<int>(candidates.size()) &&
                         anchor_need > 0;
                         ++candidate_index) {
                        if (used_candidate[static_cast<std::size_t>(candidate_index)]) {
                            continue;
                        }
                        const FocusFillCandidate& candidate =
                            candidates[static_cast<std::size_t>(candidate_index)];
                        if (!this->candidate_matches_family(candidate, family_key)) {
                            continue;
                        }
                        if (pass == 0 && candidate.source_index >= 0 &&
                            run_.population_niche_id[
                                static_cast<std::size_t>(candidate.source_index)] ==
                                niche_id) {
                            continue;
                        }
                        const int target = this->select_target(state, used_target, protected_indices, 
                            niche_id,
                            family_key,
                            candidate.source_index,
                            true);
                        if (target < 0) {
                            continue;
                        }
                        if (!this->replace_target(state, used_target, 
                                target,
                                niche_id,
                                candidate,
                                focus_family
                                    ? "focus_soft_fill_anchor"
                                    : "niche_soft_fill_anchor",
                                focus_family)) {
                            continue;
                        }
                        used_candidate[
                            static_cast<std::size_t>(candidate_index)] = 1;
                        ++moves;
                        --anchor_need;
                        ++matching_member_count;
                        if (focus_family) {
                            ++run_.epoch_active_focus_soft_fill_anchor_count;
                        } else {
                            ++run_.epoch_active_niche_soft_fill_anchor_count;
                        }
                        const double merit = candidate.fitness.score.merit;
                        if (std::isfinite(merit)) {
                            anchor_best_merit =
                                std::min(anchor_best_merit, merit);
                        }
                    }
                }
                if (!std::isfinite(anchor_best_merit)) {
                    continue;
                }
                while (niche_id <
                           static_cast<int>(state.member_counts.size()) &&
                       state.member_counts[static_cast<std::size_t>(niche_id)] <
                           target_quota) {
                    int selected_candidate = -1;
                    int selected_target = -1;
                    for (int pass = 0; pass < 2 && selected_candidate < 0;
                         ++pass) {
                        for (int candidate_index = 0;
                             candidate_index < static_cast<int>(candidates.size());
                             ++candidate_index) {
                            if (used_candidate[
                                    static_cast<std::size_t>(candidate_index)]) {
                                continue;
                            }
                            const FocusFillCandidate& candidate =
                                candidates[
                                    static_cast<std::size_t>(candidate_index)];
                            const bool matches_family =
                                this->candidate_matches_family(candidate, family_key);
                            if (pass == 0 && !matches_family) {
                                continue;
                            }
                            if (pass == 1 &&
                                candidate.fitness.score.merit + 1.0e-12 <
                                    anchor_best_merit) {
                                continue;
                            }
                            selected_target = this->select_target(state, used_target, protected_indices, 
                                niche_id,
                                family_key,
                                candidate.source_index,
                                false);
                            if (selected_target < 0) {
                                continue;
                            }
                            selected_candidate = candidate_index;
                            break;
                        }
                    }
                    if (selected_candidate < 0 || selected_target < 0) {
                        break;
                    }
                    const FocusFillCandidate& candidate =
                        candidates[static_cast<std::size_t>(selected_candidate)];
                    if (!this->replace_target(state, used_target, 
                            selected_target,
                            niche_id,
                            candidate,
                            focus_family
                                ? "focus_soft_fill"
                                : "niche_soft_fill",
                            focus_family)) {
                        break;
                    }
                    used_candidate[
                        static_cast<std::size_t>(selected_candidate)] = 1;
                    ++moves;
                }
            }
}

void AdaptiveSession::apply_deferred_niche_survival(const ApplyDeferredNicheSurvivalContext& frame_) {
    auto& deferred_niche_candidates = frame_.deferred_niche_candidates;
    auto& gen = frame_.gen;
    auto& active_niche_state = frame_.active_niche_state;
    auto& active_niche_repair_moves = frame_.active_niche_repair_moves;
    auto& success_f = frame_.success_f;
    auto& success_cr = frame_.success_cr;
    auto& success_delta = frame_.success_delta;
    auto& accepted = frame_.accepted;

if (active_niche_state.survival_enabled &&
                active_niche_state.configured &&
                !deferred_niche_candidates.empty() &&
                options.basins.active_niche_repair_fraction > 0.0 &&
                options.basins.active_niche_repair_interval > 0 &&
                gen % std::max(1, options.basins.active_niche_repair_interval) == 0) {
                std::stable_sort(
                    deferred_niche_candidates.begin(),
                    deferred_niche_candidates.end(),
                    [&](const DeferredNicheCandidate& a,
                        const DeferredNicheCandidate& b) {
                        return candidate_better(a.fitness, b.fitness, options);
                    });
                const int max_moves = std::clamp(
                    static_cast<int>(std::ceil(
                        std::clamp(
                            options.basins.active_niche_repair_fraction,
                            0.0,
                            1.0) *
                        static_cast<double>(run_.pop_size))),
                    1,
                    run_.pop_size);
                const std::vector<int> protected_indices = this->active_protected_indices();
                auto target_protected = [&](int index) {
                    return std::find(
                               protected_indices.begin(),
                               protected_indices.end(),
                               index) != protected_indices.end();
                };
                auto replacement_category = [&](int old_niche) {
                    if (old_niche < 0) {
                        return 0;
                    }
                    if (old_niche >=
                        static_cast<int>(active_niche_state.member_counts.size())) {
                        return 0;
                    }
                    const int count =
                        active_niche_state.member_counts[
                            static_cast<std::size_t>(old_niche)];
                    if (count >
                        this->active_niche_family_cap_for(
                            active_niche_state,
                            old_niche)) {
                        return 1;
                    }
                    if (count >
                        this->active_niche_target_quota_for(
                            active_niche_state,
                            old_niche)) {
                        return 2;
                    }
                    if (count > active_niche_state.min_quota) {
                        return 3;
                    }
                    return 4;
                };
                for (const DeferredNicheCandidate& candidate :
                     deferred_niche_candidates) {
                    if (active_niche_repair_moves >= max_moves) {
                        break;
                    }
                    if (candidate.niche_id < 0 ||
                        candidate.niche_id >=
                            static_cast<int>(
                                active_niche_state.member_counts.size())) {
                        continue;
                    }
                    if (active_niche_state.member_counts[
                            static_cast<std::size_t>(candidate.niche_id)] >=
                        this->active_niche_target_quota_for(
                            active_niche_state,
                            candidate.niche_id)) {
                        continue;
                    }
                    if (active_niche_state.member_counts[
                            static_cast<std::size_t>(candidate.niche_id)] >=
                        this->active_niche_family_cap_for(
                            active_niche_state,
                            candidate.niche_id)) {
                        continue;
                    }
                    int best_target = -1;
                    int best_category = 5;
                    for (int idx = 0; idx < run_.pop_size; ++idx) {
                        if (target_protected(idx) ||
                            idx == candidate.source_index ||
                            run_.population_niche_id.size() !=
                                static_cast<std::size_t>(run_.pop_size)) {
                            continue;
                        }
                        const int old_niche =
                            run_.population_niche_id[static_cast<std::size_t>(idx)];
                        if (old_niche == candidate.niche_id) {
                            continue;
                        }
                        const int category = replacement_category(old_niche);
                        if (category >= 4) {
                            continue;
                        }
                        if (!candidate_better(
                                candidate.fitness,
                                run_.fitness[static_cast<std::size_t>(idx)],
                                options)) {
                            continue;
                        }
                        if (best_target < 0 ||
                            category < best_category ||
                            (category == best_category &&
                             candidate_better(
                                 run_.fitness[static_cast<std::size_t>(best_target)],
                                 run_.fitness[static_cast<std::size_t>(idx)],
                                 options))) {
                            best_target = idx;
                            best_category = category;
                        }
                    }
                    if (best_target < 0) {
                        continue;
                    }
                    const CandidateFitness replaced =
                        run_.fitness[static_cast<std::size_t>(best_target)];
                    this->stage_traceable_refresh_archive(
                        population_row(run_.population, best_target, run_.dim),
                        replaced,
                        "niche_quota_repair_replaced");
                    if (run_.variant_uses_archive) {
                        run_.external_archive.push_back(
                            population_row(run_.population, best_target, run_.dim));
                        run_.external_archive_keys.push_back(
                            this->fitness_diversity_key(replaced));
                    }
                    for (int j = 0; j < run_.dim; ++j) {
                        run_.population[static_cast<std::size_t>(best_target) * run_.dim + j] =
                            candidate.scaled[static_cast<std::size_t>(j)];
                    }
                    run_.fitness[static_cast<std::size_t>(best_target)] =
                        candidate.fitness;
                    run_.fitness[static_cast<std::size_t>(best_target)].population_index =
                        best_target;
                    this->apply_active_niche_membership_change(
                        best_target,
                        candidate.niche_id,
                        active_niche_state);
                    const double old_merit = replaced.score.merit;
                    const double new_merit = candidate.fitness.score.merit;
                    double improvement =
                        (std::isfinite(old_merit) && std::isfinite(new_merit))
                            ? std::abs(old_merit - new_merit)
                            : 1.0;
                    if (replaced.feasible != candidate.fitness.feasible ||
                        replaced.trace_feasible !=
                            candidate.fitness.trace_feasible) {
                        improvement = std::max(improvement, 1.0);
                    }
                    improvement = std::max(1.0e-12, improvement);
                    if (run_.variant_uses_de) {
                        success_f.push_back(candidate.sampled_f);
                        success_cr.push_back(candidate.sampled_cr);
                        success_delta.push_back(improvement);
                    }
                    this->stage_traceable_refresh_archive(
                        candidate.scaled,
                        run_.fitness[static_cast<std::size_t>(best_target)],
                        "niche_quota_repair_accepted");
                    ++accepted;
                    ++active_niche_repair_moves;
                }
                deferred_niche_candidates.clear();
            }
}

} } // namespace optiland::optimizer_detail
