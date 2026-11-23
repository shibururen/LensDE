/*
 * Provide adaptive incumbent, LPSR, F-sampling and replacement-policy helpers.
 * Epoch, mutation and refill phases share run_ population indices, archive limits and random stream.
 * Keep generation-horizon arithmetic, protected-slot rules and sampling order consistent across callers.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

int AdaptiveSession::best_index() {
        const auto order = sorted_fitness_indices(run_.fitness, options);
        return order.empty() ? 0 : order.front();
    }

std::vector<int> AdaptiveSession::active_protected_indices() {
        std::vector<int> protected_indices;
        auto add = [&](int index) {
            if (index < 0 || index >= run_.pop_size) {
                return;
            }
            if (std::find(protected_indices.begin(), protected_indices.end(), index) ==
                protected_indices.end()) {
                protected_indices.push_back(index);
            }
        };
        add(run_.best_idx);
        if (options.inject_best) {
            add(0);
        }
        for (int index : run_.active_basin_slots) {
            add(index);
        }
        return protected_indices;
    }

bool AdaptiveSession::global_refill_blocked_source(const std::string& source) {
        return source == "micro_island_retired_archive";
    }

int AdaptiveSession::count_matched_family_keys(const std::vector<std::string>& source_keys, const std::vector<std::string>& target_keys) {
            int matched = 0;
            const bool allow_similarity = this->active_mating_allow_similarity();
            for (const std::string& source_key : source_keys) {
                if (source_key.empty()) {
                    continue;
                }
                for (const std::string& target_key : target_keys) {
                    if (!target_key.empty() &&
                        this->active_keys_match(source_key, target_key, allow_similarity)) {
                        ++matched;
                        break;
                    }
                }
            }
            return matched;
        }

void AdaptiveSession::generate_seed_perturb_refill_rows(int& epoch, int& evict_count, std::vector<ActivePlateauReplacement>& replacements, std::vector<ActivePlateauReplacement>& refill_seed_pool, std::vector<std::vector<double> >& selected_replacement_rows, const double& replacement_min_distance, int target) {
                    if (target <= 0) {
                        return;
                    }
                    std::vector<double> seed_population;
                    std::vector<int> seed_parent_indices;
                    seed_population.reserve(
                        refill_seed_pool.size() * static_cast<std::size_t>(run_.dim));
                    seed_parent_indices.reserve(refill_seed_pool.size());
                    for (const ActivePlateauReplacement& seed : refill_seed_pool) {
                        if (seed.scaled.size() != static_cast<std::size_t>(run_.dim)) {
                            continue;
                        }
                        seed_parent_indices.push_back(
                            static_cast<int>(seed_parent_indices.size()));
                        seed_population.insert(
                            seed_population.end(),
                            seed.scaled.begin(),
                            seed.scaled.end());
                    }
                    if (seed_parent_indices.empty()) {
                        return;
                    }
                    PopulationGenerationOptions refill_options =
                        options.refresh_generation;
                    refill_options.trace_feasible_gate = true;
                    refill_options.mode =
                        PopulationGenerationMode::RestrictedPerturbation;
                    const auto refill_start = Clock::now();
                    const int max_attempts = std::max(
                        target,
                        population_generation_candidate_limit(refill_options, target));
                    int attempts = 0;
                    while (static_cast<int>(replacements.size()) < evict_count &&
                           attempts < max_attempts &&
                           !population_generation_time_exhausted(
                               refill_start,
                               refill_options)) {
                        const int remaining =
                            evict_count - static_cast<int>(replacements.size());
                        const int batch = std::min(
                            max_attempts - attempts,
                            std::max(remaining, std::min(64, std::max(8, target))));
                        std::vector<double> rows =
                            make_population_generation_rows(
                                owner.base_,
                                owner.variables_,
                                seed_population,
                                run_.dim,
                                seed_parent_indices,
                                batch,
                                refill_options,
                                run_.internal_mode,
                                run_.rng,
                                run_.uni);
                        attempts += batch;
                        std::vector<CandidateFitness> generated_fitness =
                            this->evaluate_population_fitness(
                                rows,
                                batch,
                                epoch,
                                "active_plateau_retire_refill_seed_perturb_candidate");
                        for (int row = 0;
                             row < batch &&
                             static_cast<int>(replacements.size()) < evict_count;
                             ++row) {
                            std::vector<double> scaled =
                                population_row(rows, row, run_.dim);
                            CandidateFitness candidate =
                                std::move(generated_fitness[static_cast<std::size_t>(row)]);
                            if (this->add_replacement(evict_count, replacements, selected_replacement_rows, replacement_min_distance, 
                                    scaled,
                                    std::move(candidate),
                                    "active_plateau_retire_refill_generated",
                                    true,
                                    true,
                                    &run_.last_active_plateau_refill_generated_count)) {
                                refill_seed_pool.push_back(replacements.back());
                            }
                        }
                    }
                }

double AdaptiveSession::relative_improvement(double previous, double current) {
        if (!std::isfinite(previous) || !std::isfinite(current) || previous <= 0.0) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        return std::max(0.0, (previous - current) / previous);
    }

double AdaptiveSession::absolute_improvement(double previous, double current) {
        if (!std::isfinite(previous) || !std::isfinite(current)) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        return std::max(0.0, previous - current);
    }

int AdaptiveSession::lpsr_horizon_generations() {
        if (options.lpsr_horizon_generations > 0) {
            return std::max(1, options.lpsr_horizon_generations);
        }
        return std::max(1, run_.max_total_generations);
    }

double AdaptiveSession::lpsr_progress_fraction() {
        if (!run_.variant_uses_lpsr) {
            return 1.0;
        }
        return std::clamp(
            static_cast<double>(run_.total_de_generations) /
                static_cast<double>(this->lpsr_horizon_generations()),
            0.0,
            1.0);
    }

int AdaptiveSession::lpsr_target_size() {
        if (!run_.variant_uses_lpsr) {
            return run_.initial_pop_size;
        }
        const double progress_fraction = this->lpsr_progress_fraction();
        const double target = static_cast<double>(run_.initial_pop_size) -
                              progress_fraction *
                                  static_cast<double>(run_.initial_pop_size - run_.min_pop_size);
        return std::clamp(
            static_cast<int>(std::round(target)),
            run_.min_pop_size,
            run_.initial_pop_size);
    }

double AdaptiveSession::sample_f() {
        std::cauchy_distribution<double> cauchy(run_.mu_f, std::max(1.0e-9, options.f_cauchy_scale));
        double f = cauchy(run_.rng);
        for (int attempt = 0; attempt < 16 && (!std::isfinite(f) || f <= 0.0); ++attempt) {
            f = cauchy(run_.rng);
        }
        if (!std::isfinite(f) || f <= 0.0) {
            f = run_.mu_f;
        }
        return std::clamp(f, 1.0e-6, 1.0);
    }

double AdaptiveSession::sample_classic_f() {
        return std::clamp(run_.classic_f_min + run_.uni(run_.rng) * (run_.classic_f_max - run_.classic_f_min),
                          1.0e-6,
                          2.0);
    }

std::string AdaptiveSession::active_target_key_for_index(int idx, const LocalMatingPools& pools) {
        if (this->active_slot_uses_strict_frozen_family(idx)) {
            return this->active_slot_frozen_family_key_for_index(idx);
        }
        if (idx >= 0 && idx < static_cast<int>(pools.active_cohort_keys.size())) {
            const std::string& cohort_key =
                pools.active_cohort_keys[static_cast<std::size_t>(idx)];
            if (!cohort_key.empty()) {
                return cohort_key;
            }
        }
        if (idx >= 0 && idx < static_cast<int>(pools.population_keys.size())) {
            return pools.population_keys[static_cast<std::size_t>(idx)];
        }
        static const std::string empty;
        return empty;
    }

std::string AdaptiveSession::active_local_target_key_for_index(int idx, const LocalMatingPools& pools) {
        if (this->active_slot_uses_strict_frozen_family(idx)) {
            return this->active_slot_frozen_family_key_for_index(idx);
        }
        if (options.de_mating_scope == DeMatingScope::ActiveBasinOnly &&
            options.basins.active_local_cohort) {
            if (idx >= 0 && idx < static_cast<int>(pools.active_cohort_keys.size())) {
                return pools.active_cohort_keys[static_cast<std::size_t>(idx)];
            }
            static const std::string empty;
            return empty;
        }
        return this->active_target_key_for_index(idx, pools);
    }

void AdaptiveSession::add_active_local_cohorts(LocalMatingPools& pools, const bool& skip_active_local_cohort) {
if (options.basins.active_local_cohort &&
            options.de_mating_scope == DeMatingScope::ActiveBasinOnly &&
            !skip_active_local_cohort &&
            !run_.active_basin_slots.empty()) {
            std::vector<ActiveFamilyAnchor> families =
                this->build_active_family_anchors(pools);
            const int family_count = static_cast<int>(families.size());
            
    this->allocate_active_cohort_candidates(pools, families, family_count);

        }
}

void AdaptiveSession::allocate_active_cohort_candidates(LocalMatingPools& pools, std::vector<ActiveFamilyAnchor>& families, const int& family_count) {
if (family_count > 0) {
                int anchor_count = 0;
                for (const ActiveFamilyAnchor& family : families) {
                    anchor_count += std::max(0, family.anchor_count);
                }
                const double local_rate =
                    std::clamp(options.de_mating_local_rate, 0.0, 1.0);
                const int min_family_pool =
                    std::min(run_.pop_size, std::max(4, options.de_mating_min_pool));
                int per_family = options.basins.active_local_cohort_size;
                int total_limit = 0;
                if (per_family > 0) {
                    per_family = std::clamp(per_family, min_family_pool, run_.pop_size);
                    total_limit = std::min(run_.pop_size, per_family * family_count);
                } else {
                    const double cohort_fraction = std::clamp(
                        options.basins.active_local_cohort_fraction,
                        0.0,
                        1.0);
                    const int requested_total = std::clamp(
                        static_cast<int>(std::ceil(
                            local_rate *
                            cohort_fraction *
                            static_cast<double>(run_.pop_size))),
                        0,
                        run_.pop_size);
                    const int minimum_total =
                        std::min(run_.pop_size, min_family_pool * family_count);
                    total_limit = std::max(requested_total, minimum_total);
                    per_family = static_cast<int>(std::ceil(
                        static_cast<double>(std::max(1, total_limit)) /
                        static_cast<double>(family_count)));
                    per_family = std::clamp(per_family, min_family_pool, run_.pop_size);
                    total_limit = std::min(run_.pop_size, per_family * family_count);
                }
                const double max_distance =
                    std::max(0.0, options.basins.active_local_cohort_max_distance);
                
                std::vector<CohortCandidate> candidates;
                candidates.reserve(
                    static_cast<std::size_t>(family_count) *
                    static_cast<std::size_t>(run_.pop_size));
                for (int family = 0; family < family_count; ++family) {
                    const auto& active = families[static_cast<std::size_t>(family)];
                    for (int idx = 0; idx < run_.pop_size; ++idx) {
                        const std::vector<double> row =
                            population_row(run_.population, idx, run_.dim);
                        if (row.size() != static_cast<std::size_t>(run_.dim)) {
                            continue;
                        }
                        const double distance = scaled_vector_distance(
                            owner.variables_,
                            row,
                            active.row,
                            options.basins.distance_metric,
                            run_.internal_mode,
                            &owner.base_,
                            options.basins.air_thickness_distance_cap);
                        if (!std::isfinite(distance) ||
                            (max_distance > 0.0 && distance > max_distance)) {
                            continue;
                        }
                        double second_best_distance =
                            std::numeric_limits<double>::infinity();
                        for (int other = 0; other < family_count; ++other) {
                            if (other == family) {
                                continue;
                            }
                            const auto& other_active =
                                families[static_cast<std::size_t>(other)];
                            const double other_distance = scaled_vector_distance(
                                owner.variables_,
                                row,
                                other_active.row,
                                options.basins.distance_metric,
                                run_.internal_mode,
                                &owner.base_,
                                options.basins.air_thickness_distance_cap);
                            if (std::isfinite(other_distance) &&
                                other_distance < second_best_distance) {
                                second_best_distance = other_distance;
                            }
                        }
                        const double margin =
                            std::isfinite(second_best_distance)
                                ? (second_best_distance - distance)
                                : std::numeric_limits<double>::infinity();
                        double merit =
                            run_.fitness[static_cast<std::size_t>(idx)].score.merit;
                        if (!std::isfinite(merit)) {
                            merit = std::numeric_limits<double>::infinity();
                        }
                        const std::string candidate_key =
                            this->active_family_key_for_population_index(idx, pools);
                        const bool same_family =
                            !candidate_key.empty() &&
                            this->active_keys_match(candidate_key, active.key, true);
                        candidates.push_back(
                            {distance, margin, merit, same_family, family, idx});
                    }
                }
                std::stable_sort(
                    candidates.begin(),
                    candidates.end(),
                    [](const CohortCandidate& a, const CohortCandidate& b) {
                        if (a.same_family != b.same_family) {
                            return a.same_family;
                        }
                        if (std::abs(a.margin - b.margin) > 1.0e-12) {
                            return a.margin > b.margin;
                        }
                        if (std::abs(a.distance - b.distance) > 1.0e-12) {
                            return a.distance < b.distance;
                        }
                        if (std::abs(a.merit - b.merit) > 1.0e-12) {
                            return a.merit < b.merit;
                        }
                        if (a.family != b.family) {
                            return a.family < b.family;
                        }
                        return a.index < b.index;
                    });
                std::vector<int> used(static_cast<std::size_t>(family_count), 0);
                std::vector<int> assigned(static_cast<std::size_t>(run_.pop_size), -1);
                int selected_total = 0;
                for (const CohortCandidate& candidate : candidates) {
                    if (candidate.index < 0 || candidate.index >= run_.pop_size ||
                        candidate.family < 0 || candidate.family >= family_count ||
                        assigned[static_cast<std::size_t>(candidate.index)] >= 0 ||
                        selected_total >= total_limit ||
                        used[static_cast<std::size_t>(candidate.family)] >= per_family) {
                        continue;
                    }
                    assigned[static_cast<std::size_t>(candidate.index)] =
                        candidate.family;
                    ++used[static_cast<std::size_t>(candidate.family)];
                    ++selected_total;
                    const std::string& key =
                        families[static_cast<std::size_t>(candidate.family)].key;
                    pools.active_cohort_keys[static_cast<std::size_t>(candidate.index)] =
                        key;
                    this->add_population_pool_index(pools, key, candidate.index);
                    auto& cohort_bucket = pools.active_cohort_by_key[key];
                    if (std::find(
                            cohort_bucket.begin(),
                            cohort_bucket.end(),
                            candidate.index) == cohort_bucket.end()) {
                        cohort_bucket.push_back(candidate.index);
                    }
                }
                run_.last_active_local_cohort_family_count = family_count;
                run_.last_active_local_cohort_anchor_count = anchor_count;
                run_.last_active_local_cohort_total_limit = total_limit;
                run_.last_active_local_cohort_per_family = per_family;
                for (int count : used) {
                    run_.last_active_local_cohort_selected_count += count;
                    run_.last_active_local_cohort_max_family_size =
                        std::max(run_.last_active_local_cohort_max_family_size, count);
                }
            }
}

bool AdaptiveSession::add_replacement(int& evict_count, std::vector<ActivePlateauReplacement>& replacements, std::vector<std::vector<double> >& selected_replacement_rows, const double& replacement_min_distance, const std::vector<double>& scaled, CandidateFitness candidate, const std::string& source, bool require_trace, bool require_unique, int* accepted_counter) {
                    if (static_cast<int>(replacements.size()) >= evict_count ||
                        scaled.size() != static_cast<std::size_t>(run_.dim)) {
                        return false;
                    }
                    if (require_trace && !candidate.trace_feasible) {
                        ++run_.last_active_plateau_refill_rejected_trace_count;
                        return false;
                    }
                    if (this->global_refill_blocked_fitness(candidate)) {
                        ++run_.last_active_plateau_refill_rejected_retired_count;
                        return false;
                    }
                    candidate.source = source;
                    candidate.population_index = -1;
                    if (this->active_plateau_fitness_matches_retired(scaled, candidate)) {
                        ++run_.last_active_plateau_refill_rejected_retired_count;
                        return false;
                    }
                    if (require_unique && this->replacement_is_duplicate(selected_replacement_rows, replacement_min_distance, scaled)) {
                        return false;
                    }
                    replacements.push_back({scaled, std::move(candidate), source});
                    selected_replacement_rows.push_back(scaled);
                    if (accepted_counter) {
                        ++(*accepted_counter);
                    }
                    return true;
                }

bool AdaptiveSession::scaled_rows_equivalent(const std::vector<double>& a, const std::vector<double>& b) {
            if (a.size() != b.size()) {
                return false;
            }
            double max_abs_delta = 0.0;
            for (std::size_t i = 0; i < a.size(); ++i) {
                max_abs_delta = std::max(max_abs_delta, std::abs(a[i] - b[i]));
            }
            return max_abs_delta <= 1.0e-12;
        }

bool AdaptiveSession::family_is_focus(ActiveNicheGenerationState& state, int niche_id) {
                return niche_id >= 0 &&
                       niche_id < static_cast<int>(state.focus_family.size()) &&
                       state.focus_family[static_cast<std::size_t>(niche_id)] != 0;
            }

int AdaptiveSession::family_elite_seed_count(ActiveNicheGenerationState& state, int niche_id) {
                return this->family_is_focus(state, niche_id)
                           ? std::max(
                                 1,
                                 options.basins.active_focus_elite_seed_count)
                           : std::max(
                                 1,
                                 options.basins.active_niche_elite_seed_count);
            }

bool AdaptiveSession::candidate_matches_family(const FocusFillCandidate& candidate, const std::string& family_key) {
                    return !candidate.key.empty() &&
                           this->active_keys_match(
                               candidate.key,
                               family_key,
                               this->active_mating_allow_similarity());
                }

int AdaptiveSession::select_target(ActiveNicheGenerationState& state, std::vector<char>& used_target, const std::vector<int>& protected_indices, int niche_id, const std::string& family_key, int candidate_source_index, bool allow_family_replacement) {
                    std::vector<int> targets;
                    targets.reserve(static_cast<std::size_t>(run_.pop_size));
                    const bool target_is_focus = this->family_is_focus(state, niche_id);
                    for (int idx = 0; idx < run_.pop_size; ++idx) {
                        if (used_target[static_cast<std::size_t>(idx)] ||
                            this->target_protected(protected_indices, idx) ||
                            idx == candidate_source_index) {
                            continue;
                        }
                        const int old_niche =
                            run_.population_niche_id[static_cast<std::size_t>(idx)];
                        if (!target_is_focus &&
                            old_niche >= 0 &&
                            old_niche <
                                static_cast<int>(state.focus_family.size()) &&
                            state.focus_family[
                                static_cast<std::size_t>(old_niche)] != 0) {
                            continue;
                        }
                        if (old_niche == niche_id &&
                            (!allow_family_replacement ||
                             this->population_matches_family(idx, family_key))) {
                            continue;
                        }
                        targets.push_back(idx);
                    }
                    std::stable_sort(
                        targets.begin(),
                        targets.end(),
                        [&](int a, int b) {
                            auto category = [&](int idx) {
                                const int old_niche =
                                    run_.population_niche_id[
                                        static_cast<std::size_t>(idx)];
                                if (old_niche < 0) {
                                    return 0;
                                }
                                if (old_niche != niche_id &&
                                    !this->family_soft_fill_enabled(state, old_niche)) {
                                    return 1;
                                }
                                if (old_niche == niche_id) {
                                    return 2;
                                }
                                return 3;
                            };
                            const int cat_a = category(a);
                            const int cat_b = category(b);
                            if (cat_a != cat_b) {
                                return cat_a < cat_b;
                            }
                            return candidate_better(
                                run_.fitness[static_cast<std::size_t>(b)],
                                run_.fitness[static_cast<std::size_t>(a)],
                                options);
                        });
                    return targets.empty() ? -1 : targets.front();
                }

bool AdaptiveSession::replace_target(ActiveNicheGenerationState& state, std::vector<char>& used_target, int target, int niche_id, const FocusFillCandidate& candidate, const char* source_tag, bool focus_family) {
                    if (target < 0 || target >= run_.pop_size) {
                        return false;
                    }
                    const CandidateFitness replaced =
                        run_.fitness[static_cast<std::size_t>(target)];
                    const std::vector<double> replaced_scaled =
                        population_row(run_.population, target, run_.dim);
                    this->stage_traceable_refresh_archive(
                        replaced_scaled,
                        replaced,
                        "active_soft_fill_replaced");
                    if (run_.variant_uses_archive) {
                        run_.external_archive.push_back(replaced_scaled);
                        run_.external_archive_keys.push_back(
                            this->fitness_diversity_key(replaced));
                    }
                    set_population_row(
                        run_.population,
                        target,
                        candidate.scaled,
                        owner.variables_,
                        run_.internal_mode);
                    run_.fitness[static_cast<std::size_t>(target)] = candidate.fitness;
                    run_.fitness[static_cast<std::size_t>(target)].population_index =
                        target;
                    run_.fitness[static_cast<std::size_t>(target)].source =
                        source_tag;
                    this->apply_active_niche_membership_change(
                        target,
                        niche_id,
                        state);
                    this->consider_basin(
                        candidate.scaled,
                        run_.fitness[static_cast<std::size_t>(target)],
                        source_tag,
                        false);
                    this->stage_traceable_refresh_archive(
                        candidate.scaled,
                        run_.fitness[static_cast<std::size_t>(target)],
                        "active_soft_fill_accepted");
                    used_target[static_cast<std::size_t>(target)] = 1;
                    if (focus_family) {
                        ++run_.epoch_active_focus_soft_fill_move_count;
                    } else {
                        ++run_.epoch_active_niche_soft_fill_move_count;
                    }
                    return true;
                }





bool AdaptiveSession::protected_index(std::vector<int>& protected_indices, int idx) {
                    return this->contains_index(protected_indices, idx);
                }

bool AdaptiveSession::replacement_is_duplicate(std::vector<std::vector<double> >& selected_replacement_rows, const double& replacement_min_distance, const std::vector<double>& scaled) {
                if (replacement_min_distance <= 0.0) {
                    return false;
                }
                for (const std::vector<double>& existing : selected_replacement_rows) {
                    if (existing.size() != static_cast<std::size_t>(run_.dim) ||
                        scaled.size() != static_cast<std::size_t>(run_.dim)) {
                        continue;
                    }
                    const double distance = scaled_vector_distance(
                        owner.variables_,
                        scaled,
                        existing,
                        options.basins.distance_metric,
                        run_.internal_mode,
                        &owner.base_,
                        options.basins.air_thickness_distance_cap);
                    if (std::isfinite(distance) && distance < replacement_min_distance) {
                        return true;
                    }
                }
                return false;
            }

bool AdaptiveSession::target_protected(const std::vector<int>& protected_indices, int index) {
                return std::find(
                           protected_indices.begin(),
                           protected_indices.end(),
                           index) != protected_indices.end();
            }

} } // namespace optiland::optimizer_detail
