/*
 * Update adaptive basin lifecycles and assemble active-family anchors and strict mating pools.
 * The epoch controller shares these identities with population niches and protected archive slots.
 * Keep lifecycle resets and family/slot index correspondence aligned before generation work begins.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

void AdaptiveSession::update_active_basin_lifecycle(int epoch) {
        run_.last_active_plateau_new_cooldown_count = 0;
        run_.last_active_plateau_retired_family_count = 0;
        run_.last_active_plateau_active_niche_tracked_count = 0;
        run_.last_active_plateau_evict_count = 0;
        run_.last_active_plateau_evict_candidate_count = 0;
        run_.last_active_plateau_evict_cap_count = 0;
        run_.last_active_plateau_evict_distance_min =
            std::numeric_limits<double>::quiet_NaN();
        run_.last_active_plateau_evict_distance_median =
            std::numeric_limits<double>::quiet_NaN();
        run_.last_active_plateau_evict_distance_max =
            std::numeric_limits<double>::quiet_NaN();
        run_.last_active_plateau_refill_basin_count = 0;
        run_.last_active_plateau_refill_population_count = 0;
        run_.last_active_plateau_refill_refresh_count = 0;
        run_.last_active_plateau_refill_initial_seed_count = 0;
        run_.last_active_plateau_refill_generated_count = 0;
        run_.last_active_plateau_refill_lhs_count = 0;
        run_.last_active_plateau_refill_clone_fallback_count = 0;
        run_.last_active_plateau_refill_rejected_trace_count = 0;
        run_.last_active_plateau_refill_rejected_retired_count = 0;
        this->clear_active_lifecycle_match_caches();
        if (!this->active_plateau_lifecycle_enabled()) {
            run_.active_basin_lifecycle.clear();
            return;
        }
        
        std::unordered_map<std::string, ActivePlateauCandidateSnapshot>
            current_best_by_key;
        auto add_plateau_snapshot =
            [&](const std::string& key,
                const std::vector<double>& scaled,
                const CandidateFitness& candidate) {
            if (key.empty() || scaled.size() != static_cast<std::size_t>(run_.dim)) {
                return false;
            }
            const double merit = candidate.score.merit;
            if (!std::isfinite(merit)) {
                return false;
            }
            auto it = current_best_by_key.find(key);
            if (it == current_best_by_key.end() || merit < it->second.merit) {
                current_best_by_key[key] = {merit, scaled};
            }
            return true;
        };
        auto note_active_niche_tracked_key =
            [&](const std::string& key, std::vector<std::string>& tracked_keys) {
            if (key.empty()) {
                return;
            }
            const bool allow_similarity = this->active_mating_allow_similarity();
            for (const std::string& existing : tracked_keys) {
                if (this->active_keys_match(key, existing, allow_similarity)) {
                    return;
                }
            }
            tracked_keys.push_back(key);
        };
        for (int index : this->active_plateau_tracking_candidate_indices()) {
            const BasinElite& basin = run_.basins[static_cast<std::size_t>(index)];
            add_plateau_snapshot(
                this->active_lifecycle_key_for_basin(basin),
                basin.scaled_values,
                basin.fitness);
        }
        if (options.basins.active_plateau_track_active_niches) {
            std::vector<std::string> active_niche_tracked_keys;
            if (run_.population_niche_id.size() == static_cast<std::size_t>(run_.pop_size) &&
                !run_.active_niche_family_keys.empty()) {
                for (int idx = 0; idx < run_.pop_size; ++idx) {
                    const int niche_id =
                        run_.population_niche_id[static_cast<std::size_t>(idx)];
                    if (niche_id < 0 ||
                        niche_id >= static_cast<int>(run_.active_niche_family_keys.size())) {
                        continue;
                    }
                    const std::string& key =
                        run_.active_niche_family_keys[static_cast<std::size_t>(niche_id)];
                    if (add_plateau_snapshot(
                            key,
                            population_row(run_.population, idx, run_.dim),
                            run_.fitness[static_cast<std::size_t>(idx)])) {
                        note_active_niche_tracked_key(key, active_niche_tracked_keys);
                    }
                }
            }
            for (std::size_t active_offset = 0;
                 active_offset < run_.active_basin_slots.size();
                 ++active_offset) {
                const int slot = run_.active_basin_slots[active_offset];
                if (slot < 0 || slot >= run_.pop_size ||
                    slot >= static_cast<int>(run_.fitness.size())) {
                    continue;
                }
                const std::string key =
                    this->active_slot_current_family_key_for_offset(active_offset);
                if (add_plateau_snapshot(
                        key,
                        this->active_slot_scaled_row_for_offset(active_offset),
                        run_.fitness[static_cast<std::size_t>(slot)])) {
                    note_active_niche_tracked_key(key, active_niche_tracked_keys);
                }
            }
            run_.last_active_plateau_active_niche_tracked_count =
                static_cast<int>(active_niche_tracked_keys.size());
        }
        std::vector<std::string> retired_keys;
        std::vector<std::vector<double>> retired_scaled_rows;
        auto add_retired_key = [&](const std::string& key,
                                   const std::vector<double>& scaled) {
            if (key.empty()) {
                return false;
            }
            const bool allow_similarity = this->active_mating_allow_similarity();
            for (const std::string& existing : retired_keys) {
                if (this->active_keys_match(key, existing, allow_similarity)) {
                    return false;
                }
            }
            const bool added_unique =
                this->add_active_plateau_retired_filter(key, scaled);
            if (!added_unique) {
                return false;
            }
            retired_keys.push_back(key);
            retired_scaled_rows.push_back(scaled);
            return true;
        };
        std::vector<std::string> drifted_source_keys;
        const bool allow_similarity = this->active_mating_allow_similarity();
        auto add_drifted_source_key = [&](const std::string& key) {
            if (key.empty()) {
                return;
            }
            for (const std::string& existing : drifted_source_keys) {
                if (this->active_keys_match(key, existing, allow_similarity)) {
                    return;
                }
            }
            drifted_source_keys.push_back(key);
        };
        const std::size_t drift_slot_count =
            std::min(run_.active_basin_slots.size(), run_.active_basin_slot_origin_keys.size());
        for (std::size_t active_offset = 0; active_offset < drift_slot_count; ++active_offset) {
            const std::string& source_key =
                run_.active_basin_slot_origin_keys[active_offset];
            if (source_key.empty()) {
                continue;
            }
            const std::string current_key =
                this->active_slot_current_family_key_for_offset(active_offset);
            if (current_key.empty() ||
                !this->active_keys_match(current_key, source_key, allow_similarity)) {
                add_drifted_source_key(source_key);
            }
        }
        const double min_rel_improvement = std::max(
            0.0,
            options.basins.active_plateau_min_rel_improvement);
        const int retire_after = std::max(0, options.basins.active_plateau_retire_after);
        const int cooldown_epochs =
            std::max(0, options.basins.active_plateau_cooldown_epochs);
        for (const auto& [key, snapshot] : current_best_by_key) {
            const double merit = snapshot.merit;
            ActiveBasinLifecycleState& state = run_.active_basin_lifecycle[key];
            state.last_seen_epoch = epoch;
            if (!std::isfinite(state.best_merit)) {
                state.best_merit = merit;
                state.stale_epochs = 0;
                continue;
            }
            const bool improved = merit < state.best_merit;
            const double rel_improvement =
                improved
                    ? (state.best_merit - merit) /
                          std::max(1.0e-12, std::abs(state.best_merit))
                    : 0.0;
            if (improved) {
                state.best_merit = merit;
            }
            if (improved && rel_improvement + 1.0e-12 >= min_rel_improvement) {
                state.stale_epochs = 0;
                if (state.cooldown_until_epoch < epoch) {
                    state.cooldown_until_epoch = 0;
                }
                continue;
            }
            ++state.stale_epochs;
            if (retire_after > 0 &&
                state.stale_epochs >= retire_after &&
                cooldown_epochs > 0) {
                const int cooldown_until = epoch + cooldown_epochs;
                if (state.cooldown_until_epoch < cooldown_until) {
                    if (add_retired_key(key, snapshot.scaled)) {
                        ++run_.last_active_plateau_new_cooldown_count;
                    }
                }
                state.cooldown_until_epoch = cooldown_until;
                state.stale_epochs = 0;
            }
        }
        const int drift_cooldown_epochs = std::max(1, cooldown_epochs);
        for (const std::string& key : drifted_source_keys) {
            ActiveBasinLifecycleState& state = run_.active_basin_lifecycle[key];
            state.last_seen_epoch = epoch;
            const int cooldown_until = epoch + drift_cooldown_epochs;
            if (state.cooldown_until_epoch < cooldown_until) {
                ++run_.last_active_plateau_new_cooldown_count;
            }
            state.cooldown_until_epoch = cooldown_until;
            state.stale_epochs = 0;
        }
        for (auto it = run_.active_basin_lifecycle.begin();
             it != run_.active_basin_lifecycle.end();) {
            if (epoch - it->second.last_seen_epoch >
                std::max(8, options.basins.active_plateau_window * 4)) {
                it = run_.active_basin_lifecycle.erase(it);
            } else {
                ++it;
            }
        }
        run_.last_active_plateau_retired_family_count =
            static_cast<int>(retired_keys.size());
        this->evict_active_plateau_retired_keys(retired_keys, retired_scaled_rows, epoch);
    }

std::string AdaptiveSession::active_family_key_for_population_index(int idx, const LocalMatingPools& pools) {
            if (idx >= 0 && idx < static_cast<int>(pools.population_keys.size())) {
                const std::string& key =
                    pools.population_keys[static_cast<std::size_t>(idx)];
                if (!key.empty()) {
                    return key;
                }
            }
            if (idx >= 0 && idx < static_cast<int>(run_.fitness.size())) {
                return this->fitness_diversity_key(run_.fitness[static_cast<std::size_t>(idx)]);
            }
            return std::string{};
        }

int AdaptiveSession::active_family_anchor_index(const std::vector<ActiveFamilyAnchor>& families, const std::string& key) {
            if (key.empty()) {
                return -1;
            }
            const bool allow_similarity = this->active_mating_allow_similarity();
            for (int family = 0; family < static_cast<int>(families.size()); ++family) {
                if (this->active_keys_match(
                        key,
                        families[static_cast<std::size_t>(family)].key,
                        allow_similarity)) {
                    return family;
                }
            }
            return -1;
        }

std::vector<ActiveFamilyAnchor> AdaptiveSession::build_active_family_anchors(const LocalMatingPools& pools) {
        std::vector<ActiveFamilyAnchor> families;
        families.reserve(run_.active_basin_slots.size());
        for (std::size_t active_offset = 0;
             active_offset < run_.active_basin_slots.size();
             ++active_offset) {
            const int slot = run_.active_basin_slots[active_offset];
            if (slot < 0 || slot >= run_.pop_size ||
                slot >= static_cast<int>(run_.fitness.size())) {
                continue;
            }
            std::string key;
            if (active_offset < run_.active_basin_slot_family_keys.size()) {
                key = run_.active_basin_slot_family_keys[active_offset];
            }
            if (key.empty()) {
                key = this->active_family_key_for_population_index(slot, pools);
            }
            if (key.empty()) {
                continue;
            }
            const std::vector<double> row = population_row(run_.population, slot, run_.dim);
            if (row.size() != static_cast<std::size_t>(run_.dim)) {
                continue;
            }
            const int existing = this->active_family_anchor_index(families, key);
            if (existing >= 0) {
                ActiveFamilyAnchor& family =
                    families[static_cast<std::size_t>(existing)];
                ++family.anchor_count;
                if (family.representative_slot < 0 ||
                    candidate_better(
                        run_.fitness[static_cast<std::size_t>(slot)],
                        run_.fitness[static_cast<std::size_t>(family.representative_slot)],
                        options)) {
                    family.representative_slot = slot;
                    family.key = key;
                    family.row = row;
                }
                continue;
            }
            ActiveFamilyAnchor family;
            family.representative_slot = slot;
            family.anchor_count = 1;
            family.key = key;
            family.row = row;
            families.push_back(std::move(family));
        }
        return families;
    }

std::vector<std::string > AdaptiveSession::active_family_keys_from_anchors(const std::vector<ActiveFamilyAnchor>& families) {
            std::vector<std::string> keys;
            keys.reserve(families.size());
            for (const ActiveFamilyAnchor& family : families) {
                if (!family.key.empty()) {
                    keys.push_back(family.key);
                }
            }
            return keys;
        }

std::vector<int> AdaptiveSession::strict_active_slot_population_pool(int idx, const LocalMatingPools& pools, bool allow_similarity) {
            std::vector<int> pool;
            if (!this->active_slot_uses_strict_frozen_family(idx)) {
                return pool;
            }
            const std::string frozen_key = this->active_slot_frozen_family_key_for_index(idx);
            if (frozen_key.empty()) {
                return pool;
            }
            pool.reserve(pools.population_keys.size());
            for (int candidate = 0;
                 candidate < static_cast<int>(pools.population_keys.size());
                 ++candidate) {
                const std::string& candidate_key =
                    pools.population_keys[static_cast<std::size_t>(candidate)];
                if (this->active_keys_match(candidate_key, frozen_key, allow_similarity)) {
                    pool.push_back(candidate);
                }
            }
            return pool;
        }

std::vector<int> AdaptiveSession::strict_active_slot_archive_pool(int idx, const LocalMatingPools& pools, bool allow_similarity) {
            std::vector<int> pool;
            if (!this->active_slot_uses_strict_frozen_family(idx)) {
                return pool;
            }
            const std::string frozen_key = this->active_slot_frozen_family_key_for_index(idx);
            if (frozen_key.empty()) {
                return pool;
            }
            pool.reserve(pools.archive_keys.size());
            for (int archive_idx = 0;
                 archive_idx < static_cast<int>(pools.archive_keys.size());
                 ++archive_idx) {
                const std::string& candidate_key =
                    pools.archive_keys[static_cast<std::size_t>(archive_idx)];
                if (this->active_keys_match(candidate_key, frozen_key, allow_similarity)) {
                    pool.push_back(archive_idx);
                }
            }
            return pool;
        }

std::vector<std::string > AdaptiveSession::active_family_keys_from_slots(const LocalMatingPools& pools) {
        return this->active_family_keys_from_anchors(this->build_active_family_anchors(pools));
    }

std::vector<std::string > AdaptiveSession::active_family_keys_from_frozen_slots() {
        return this->active_slot_frozen_family_keys();
    }

int AdaptiveSession::active_family_index_for_key(const std::string& target_key, const std::vector<std::string>& family_keys) {
        if (target_key.empty()) {
            return -1;
        }
        for (int family = 0; family < static_cast<int>(family_keys.size()); ++family) {
            if (this->active_keys_match(
                    target_key,
                    family_keys[static_cast<std::size_t>(family)],
                    this->active_mating_allow_similarity())) {
                return family;
            }
        }
        return -1;
    }

ActiveFamilyCoverage AdaptiveSession::active_family_coverage(const LocalMatingPools& pools) {
        ActiveFamilyCoverage coverage;
        coverage.family_keys = this->active_family_keys_from_frozen_slots();
        if (coverage.family_keys.empty()) {
            coverage.family_keys = this->active_family_keys_from_slots(pools);
        }
        coverage.family_for_population.assign(static_cast<std::size_t>(run_.pop_size), -1);
        coverage.family_counts.assign(coverage.family_keys.size(), 0);
        for (int idx = 0; idx < run_.pop_size &&
                          idx < static_cast<int>(pools.population_keys.size());
             ++idx) {
            const int family = this->active_family_index_for_key(
                this->active_target_key_for_index(idx, pools),
                coverage.family_keys);
            coverage.family_for_population[static_cast<std::size_t>(idx)] = family;
            if (family >= 0) {
                int& count = coverage.family_counts[static_cast<std::size_t>(family)];
                ++count;
                coverage.max_family_size = std::max(coverage.max_family_size, count);
            }
        }
        const double configured_fraction =
            options.basins.active_refresh_max_family_fraction;
        const double auto_fraction =
            coverage.family_keys.empty()
                ? 0.0
                : 1.0 / static_cast<double>(coverage.family_keys.size());
        const double fraction =
            configured_fraction > 0.0
                ? std::clamp(configured_fraction, 0.0, 1.0)
                : auto_fraction;
        coverage.family_limit =
            fraction > 0.0
                ? std::clamp(
                      static_cast<int>(std::ceil(
                          fraction * static_cast<double>(run_.pop_size))),
                      1,
                      run_.pop_size)
                : 0;
        return coverage;
    }

std::string AdaptiveSession::find_matching_active_family_key(const std::string& key, const std::vector<std::string>& family_keys) {
            if (key.empty()) {
                return std::string{};
            }
            for (const std::string& family_key : family_keys) {
                if (key == family_key) {
                    return family_key;
                }
            }
            const bool allow_similarity = this->active_mating_allow_similarity();
            if (!allow_similarity) {
                return std::string{};
            }
            for (const std::string& family_key : family_keys) {
                if (this->active_keys_match(key, family_key, true)) {
                    return family_key;
                }
            }
            return std::string{};
        }

bool AdaptiveSession::target_matches_active_basin_family(int i, const LocalMatingPools& pools) {
        if (i < 0 || i >= static_cast<int>(pools.population_keys.size())) {
            return false;
        }
        const std::string& target_key = this->active_local_target_key_for_index(i, pools);
        if (target_key.empty()) {
            return false;
        }
        std::vector<std::string> active_family_keys =
            this->active_family_keys_from_frozen_slots();
        if (active_family_keys.empty()) {
            active_family_keys = this->active_family_keys_from_slots(pools);
        }
        if (active_family_keys.empty()) {
            return false;
        }
        for (const std::string& active_key : active_family_keys) {
            if (this->active_keys_match(
                    target_key,
                    active_key,
                    this->active_mating_allow_similarity())) {
                return true;
            }
        }
        return false;
    }

} } // namespace optiland::optimizer_detail
