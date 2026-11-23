/*
 * Maintain adaptive incumbents, LPSR population size and external-archive rows/keys.
 * Generation and epoch phases call these helpers after scoring and replacement decisions.
 * Preserve stable survivor order and parallel row/key/fitness updates when trimming or reducing a population.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

void AdaptiveSession::update_best_from_population() {
        const int idx = this->best_index();
        if (candidate_better(run_.fitness[static_cast<std::size_t>(idx)], run_.best_fitness, options)) {
            run_.best_idx = idx;
            run_.best_scaled = population_row(run_.population, idx, run_.dim);
            run_.best_fitness = run_.fitness[static_cast<std::size_t>(idx)];
        }
    }

bool AdaptiveSession::active_archive_only_enabled() {
        return this->active_diversity_enabled() && options.basins.active_archive_only;
    }

void AdaptiveSession::reset_best_from_population_and_incumbent(const std::vector<double>& previous_best_scaled, int generation) {
            const int idx = this->best_index();
            run_.best_idx = idx;
            run_.best_scaled = population_row(run_.population, idx, run_.dim);
            run_.best_fitness = run_.fitness[static_cast<std::size_t>(idx)];
            if (previous_best_scaled.size() == static_cast<std::size_t>(run_.dim)) {
                CandidateFitness incumbent = this->score_fitness_for_scaled(
                    previous_best_scaled,
                    generation,
                    -1,
                    "adaptive_glass_tolerance_incumbent",
                    run_.best_fitness.polished);
                if (candidate_better(incumbent, run_.best_fitness, options)) {
                    run_.best_scaled = previous_best_scaled;
                    run_.best_fitness = std::move(incumbent);
                }
            }
        }

void AdaptiveSession::trim_external_archive() {
        if (!run_.variant_uses_archive) {
            run_.external_archive.clear();
            run_.external_archive_keys.clear();
            return;
        }
        const int capacity = std::max(
            0,
            static_cast<int>(std::round(std::max(0.0, options.archive_size_ratio) * run_.pop_size)));
        if (capacity <= 0) {
            run_.external_archive.clear();
            run_.external_archive_keys.clear();
            return;
        }
        if (run_.external_archive_keys.size() != run_.external_archive.size()) {
            run_.external_archive_keys.resize(run_.external_archive.size());
        }
        while (static_cast<int>(run_.external_archive.size()) > capacity) {
            const int remove_idx =
                std::min(static_cast<int>(run_.external_archive.size()) - 1,
                         static_cast<int>(run_.uni(run_.rng) * run_.external_archive.size()));
            run_.external_archive.erase(run_.external_archive.begin() + remove_idx);
            run_.external_archive_keys.erase(run_.external_archive_keys.begin() + remove_idx);
        }
    }

void AdaptiveSession::reduce_population_if_needed() {
        int target_size = this->lpsr_target_size();
        if (options.basins.active_niche_survival &&
            !run_.active_niche_family_keys.empty()) {
            int min_quota = options.basins.active_niche_min_quota;
            if (min_quota <= 0) {
                const int by_fraction =
                    static_cast<int>(std::ceil(0.02 * static_cast<double>(run_.pop_size)));
                min_quota = std::max(
                    std::max(1, options.de_mating_min_pool),
                    by_fraction);
            }
            min_quota = std::clamp(min_quota, 1, std::max(1, run_.pop_size));
            const int free_floor =
                std::max(0, std::min(run_.pop_size, options.de_mating_min_pool));
            const int niche_floor =
                static_cast<int>(run_.active_niche_family_keys.size()) * min_quota +
                free_floor;
            target_size = std::max(target_size, std::min(run_.pop_size, niche_floor));
        }
        if (target_size >= run_.pop_size) {
            return;
        }
        const int old_pop_size = run_.pop_size;
        const auto order = sorted_fitness_indices(run_.fitness, options);
        std::vector<int> keep_order;
        keep_order.reserve(static_cast<std::size_t>(target_size));
        auto add_keep = [&](int index) {
            if (index < 0 || index >= run_.pop_size ||
                static_cast<int>(keep_order.size()) >= target_size) {
                return;
            }
            if (std::find(keep_order.begin(), keep_order.end(), index) == keep_order.end()) {
                keep_order.push_back(index);
            }
        };
        if (this->active_diversity_enabled()) {
            for (int index : this->active_protected_indices()) {
                add_keep(index);
            }
        }
        for (int index : order) {
            add_keep(index);
            if (static_cast<int>(keep_order.size()) >= target_size) {
                break;
            }
        }
        if (this->refresh_trace_archive_enabled()) {
            for (int old_i = 0; old_i < run_.pop_size; ++old_i) {
                if (std::find(keep_order.begin(), keep_order.end(), old_i) !=
                    keep_order.end()) {
                    continue;
                }
                this->stage_traceable_refresh_archive(
                    population_row(run_.population, old_i, run_.dim),
                    run_.fitness[static_cast<std::size_t>(old_i)],
                    "lpsr_removed");
            }
        }
        std::vector<double> reduced_population(static_cast<std::size_t>(target_size) * run_.dim, 0.0);
        std::vector<CandidateFitness> reduced_fitness;
        reduced_fitness.reserve(static_cast<std::size_t>(target_size));
        std::vector<JointConstraintAnchorState> reduced_joint_constraint_anchors;
        reduced_joint_constraint_anchors.reserve(static_cast<std::size_t>(target_size));
        std::vector<int> remapped_active_slots;
        std::vector<std::string> remapped_active_slot_origin_keys;
        std::vector<std::string> remapped_active_slot_family_keys;
        const bool preserve_archive_only_active_keys =
            this->active_archive_only_enabled() && run_.active_basin_slots.empty();
        std::vector<std::string> archive_only_active_origin_keys;
        std::vector<std::string> archive_only_active_family_keys;
        if (preserve_archive_only_active_keys) {
            archive_only_active_origin_keys = run_.active_basin_slot_origin_keys;
            archive_only_active_family_keys = run_.active_basin_slot_family_keys;
        }
        std::vector<int> remapped_population_niche_id;
        if (run_.population_niche_id.size() == static_cast<std::size_t>(run_.pop_size)) {
            remapped_population_niche_id.reserve(static_cast<std::size_t>(target_size));
        }
        for (int new_i = 0; new_i < target_size; ++new_i) {
            const int old_i = keep_order[static_cast<std::size_t>(new_i)];
            for (int j = 0; j < run_.dim; ++j) {
                reduced_population[static_cast<std::size_t>(new_i) * run_.dim + j] =
                    run_.population[static_cast<std::size_t>(old_i) * run_.dim + j];
            }
            CandidateFitness keep = run_.fitness[static_cast<std::size_t>(old_i)];
            keep.population_index = new_i;
            reduced_fitness.push_back(std::move(keep));
            reduced_joint_constraint_anchors.push_back(
                run_.joint_constraint_anchors[static_cast<std::size_t>(old_i)]);
            if (run_.population_niche_id.size() == static_cast<std::size_t>(run_.pop_size)) {
                remapped_population_niche_id.push_back(
                    run_.population_niche_id[static_cast<std::size_t>(old_i)]);
            }
            const auto active_it =
                std::find(run_.active_basin_slots.begin(), run_.active_basin_slots.end(), old_i);
            if (active_it != run_.active_basin_slots.end()) {
                remapped_active_slots.push_back(new_i);
                const std::size_t active_offset =
                    static_cast<std::size_t>(std::distance(
                        run_.active_basin_slots.begin(),
                        active_it));
                if (active_offset < run_.active_basin_slot_origin_keys.size()) {
                    remapped_active_slot_origin_keys.push_back(
                        run_.active_basin_slot_origin_keys[active_offset]);
                }
                if (active_offset < run_.active_basin_slot_family_keys.size()) {
                    remapped_active_slot_family_keys.push_back(
                        run_.active_basin_slot_family_keys[active_offset]);
                }
            }
        }
        run_.population = std::move(reduced_population);
        run_.fitness = std::move(reduced_fitness);
        run_.joint_constraint_anchors =
            std::move(reduced_joint_constraint_anchors);
        run_.pop_size = target_size;
        ++run_.lpsr_reduction_count;
        run_.lpsr_removed_total += std::max(0, old_pop_size - target_size);
        if (preserve_archive_only_active_keys) {
            run_.active_basin_slots.clear();
            run_.active_basin_slot_origin_keys = std::move(archive_only_active_origin_keys);
            run_.active_basin_slot_family_keys = std::move(archive_only_active_family_keys);
        } else {
            run_.active_basin_slots = std::move(remapped_active_slots);
            run_.active_basin_slot_origin_keys = std::move(remapped_active_slot_origin_keys);
            run_.active_basin_slot_family_keys = std::move(remapped_active_slot_family_keys);
        }
        if (!remapped_population_niche_id.empty()) {
            run_.population_niche_id = std::move(remapped_population_niche_id);
        } else {
            run_.population_niche_id.clear();
        }
        this->sync_merits();
        this->trim_external_archive();
        this->update_best_from_population();
    }

int AdaptiveSession::select_same_key_population_index(int i, const std::vector<int>& order, int pbest_count) {
        const std::string key = this->fitness_diversity_key(run_.fitness[static_cast<std::size_t>(i)]);
        if (key.empty()) {
            return -1;
        }
        std::vector<int> candidates;
        candidates.reserve(static_cast<std::size_t>(std::max(1, pbest_count)));
        for (int ordered_index : order) {
            if (ordered_index < 0 || ordered_index >= run_.pop_size) {
                continue;
            }
            if (this->fitness_diversity_key(run_.fitness[static_cast<std::size_t>(ordered_index)]) == key) {
                if (this->active_plateau_fitness_matches_retired(
                        population_row(run_.population, ordered_index, run_.dim),
                        run_.fitness[static_cast<std::size_t>(ordered_index)])) {
                    continue;
                }
                candidates.push_back(ordered_index);
                if (static_cast<int>(candidates.size()) >= pbest_count) {
                    break;
                }
            }
        }
        if (candidates.empty()) {
            return -1;
        }
        const int idx = std::min(static_cast<int>(candidates.size()) - 1,
                                 static_cast<int>(run_.uni(run_.rng) * candidates.size()));
        return candidates[static_cast<std::size_t>(idx)];
    }

int AdaptiveSession::select_global_population_index(const std::vector<int>& order, int pbest_count, bool avoid_plateau_family) {
        if (order.empty() || pbest_count <= 0) {
            return -1;
        }
        const int limit = std::clamp(pbest_count, 1, static_cast<int>(order.size()));
        if (avoid_plateau_family) {
            std::vector<int> candidates;
            candidates.reserve(static_cast<std::size_t>(limit));
            for (int rank = 0; rank < limit; ++rank) {
                const int candidate = order[static_cast<std::size_t>(rank)];
                if (candidate < 0 || candidate >= run_.pop_size) {
                    continue;
                }
                if (this->active_lifecycle_mating_key_suppressed(
                        this->fitness_diversity_key(
                            run_.fitness[static_cast<std::size_t>(candidate)])) &&
                    !this->active_focus_key_matches_current(
                        this->fitness_diversity_key(
                            run_.fitness[static_cast<std::size_t>(candidate)]))) {
                    continue;
                }
                if (this->active_plateau_fitness_matches_retired(
                        population_row(run_.population, candidate, run_.dim),
                        run_.fitness[static_cast<std::size_t>(candidate)])) {
                    continue;
                }
                candidates.push_back(candidate);
            }
            for (int candidate : order) {
                if (static_cast<int>(candidates.size()) >= limit) {
                    break;
                }
                if (candidate < 0 || candidate >= run_.pop_size) {
                    continue;
                }
                if (std::find(candidates.begin(), candidates.end(), candidate) !=
                    candidates.end()) {
                    continue;
                }
                if (this->active_lifecycle_mating_key_suppressed(
                        this->fitness_diversity_key(
                            run_.fitness[static_cast<std::size_t>(candidate)])) &&
                    !this->active_focus_key_matches_current(
                        this->fitness_diversity_key(
                            run_.fitness[static_cast<std::size_t>(candidate)]))) {
                    continue;
                }
                if (this->active_plateau_fitness_matches_retired(
                        population_row(run_.population, candidate, run_.dim),
                        run_.fitness[static_cast<std::size_t>(candidate)])) {
                    continue;
                }
                candidates.push_back(candidate);
            }
            if (candidates.empty()) {
                return -1;
            }
            const int idx = std::min(static_cast<int>(candidates.size()) - 1,
                                     static_cast<int>(run_.uni(run_.rng) * candidates.size()));
            return candidates[static_cast<std::size_t>(idx)];
        }
        if (this->active_plateau_retire_filter_enabled()) {
            std::vector<int> candidates;
            candidates.reserve(static_cast<std::size_t>(limit));
            for (int rank = 0; rank < limit; ++rank) {
                const int candidate = order[static_cast<std::size_t>(rank)];
                if (candidate < 0 || candidate >= run_.pop_size) {
                    continue;
                }
                if (this->active_plateau_fitness_matches_retired(
                        population_row(run_.population, candidate, run_.dim),
                        run_.fitness[static_cast<std::size_t>(candidate)])) {
                    continue;
                }
                candidates.push_back(candidate);
            }
            if (!candidates.empty()) {
                const int idx = std::min(
                    static_cast<int>(candidates.size()) - 1,
                    static_cast<int>(run_.uni(run_.rng) * candidates.size()));
                return candidates[static_cast<std::size_t>(idx)];
            }
        }
        const int idx = std::min(limit - 1, static_cast<int>(run_.uni(run_.rng) * limit));
        return order[static_cast<std::size_t>(idx)];
    }

bool AdaptiveSession::population_matches_family(int idx, const std::string& family_key) {
                    if (idx < 0 || idx >= run_.pop_size) {
                        return false;
                    }
                    return this->active_keys_match(
                        this->fitness_diversity_key(run_.fitness[static_cast<std::size_t>(idx)]),
                        family_key,
                        this->active_mating_allow_similarity());
                }

} } // namespace optiland::optimizer_detail
