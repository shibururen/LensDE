/*
 * Select archive basins for active slots and inject their anchors into the adaptive population.
 * Epoch setup uses the shared basin archive, best incumbent and active-family lifecycle state.
 * Preserve candidate traversal, tie breaks and slot assignment order because later mating observes them.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

std::vector<int> AdaptiveSession::select_active_basin_indices() {
        const int limit = this->active_basin_slot_budget();
        if (limit <= 0 || run_.basins.empty()) {
            return std::vector<int>{};
        }
        this->sort_basins();
        const std::vector<int> family_candidates =
            this->active_basin_family_candidate_indices();
        const double min_distance = std::max(0.0, options.basins.radius * 0.5);
        auto far_from_best = [&](const BasinElite& basin) {
            const std::string key = this->active_current_family_key_for_basin(basin);
            const std::string best_key = this->fitness_diversity_key(run_.best_fitness);
            if (run_.best_scaled.size() != static_cast<std::size_t>(run_.dim)) {
                return true;
            }
            if (options.basins.semantic_diversity &&
                !key.empty() &&
                !best_key.empty() &&
                (run_.topology_similarity_enabled
                     ? !this->active_topology_keys_are_similar(
                           key,
                           best_key)
                     : key != best_key)) {
                return true;
            }
            return scaled_vector_distance(
                       owner.variables_,
                       basin.scaled_values,
                       run_.best_scaled,
                       options.basins.distance_metric,
                       run_.internal_mode,
                       &owner.base_,
                       options.basins.air_thickness_distance_cap) >= min_distance;
        };
        std::vector<int> eligible;
        eligible.reserve(run_.basins.size());
        for (int index : family_candidates) {
            if (far_from_best(run_.basins[static_cast<std::size_t>(index)])) {
                eligible.push_back(index);
            }
        }
        const int active_family_slot_limit =
            options.basins.active_max_slots_per_family > 0
                ? std::min(options.basins.active_max_slots_per_family, limit)
                : 0;
        auto active_family_recently_drifted = [&](const std::string& key) {
            if (key.empty()) {
                return false;
            }
            if (!this->active_mating_allow_similarity()) {
                return this->active_lifecycle_recently_drifted_key(key);
            }
            for (const auto& [lifecycle_key, state] : run_.active_basin_lifecycle) {
                if (run_.current_outer_epoch <= 0 ||
                    state.cooldown_until_epoch < run_.current_outer_epoch) {
                    continue;
                }
                if (this->active_keys_match(key, lifecycle_key, true)) {
                    return true;
                }
            }
            return false;
        };
        auto active_family_key_for_basin_index = [&](int index) {
            if (index < 0 || index >= static_cast<int>(run_.basins.size())) {
                return std::string{};
            }
            return this->active_lifecycle_key_for_basin(run_.basins[static_cast<std::size_t>(index)]);
        };
        auto selected_active_family_count =
            [&](const std::vector<int>& selected, int index, const std::string& key) {
                if (key.empty()) {
                    return 0;
                }
                const std::vector<double>* candidate_scaled =
                    index >= 0 && index < static_cast<int>(run_.basins.size())
                        ? &run_.basins[static_cast<std::size_t>(index)].scaled_values
                        : nullptr;
                int count = 0;
                const bool allow_similarity = this->active_mating_allow_similarity();
                for (int selected_index : selected) {
                    if (selected_index < 0 ||
                        selected_index >= static_cast<int>(run_.basins.size())) {
                        continue;
                    }
                    const BasinElite& selected_basin =
                        run_.basins[static_cast<std::size_t>(selected_index)];
                    if (this->active_family_match_basin_to_key(
                            selected_basin,
                            key,
                            candidate_scaled,
                            allow_similarity)) {
                        ++count;
                    }
                }
                return count;
            };
        auto active_family_slot_quota_ok =
            [&](const std::vector<int>& selected, int index) {
                if (active_family_slot_limit <= 0) {
                    return true;
                }
                const std::string key = active_family_key_for_basin_index(index);
                if (key.empty()) {
                    return true;
                }
                return selected_active_family_count(selected, index, key) <
                       active_family_slot_limit;
            };
        auto active_selection_distance_score =
            [&](int index, const std::vector<int>& selected) {
                if (index < 0 || index >= static_cast<int>(run_.basins.size())) {
                    return -std::numeric_limits<double>::infinity();
                }
                if (selected.empty()) {
                    return std::numeric_limits<double>::infinity();
                }
                const BasinElite& basin = run_.basins[static_cast<std::size_t>(index)];
                const std::string key = active_family_key_for_basin_index(index);
                double nearest_topology = std::numeric_limits<double>::infinity();
                double nearest_scaled = std::numeric_limits<double>::infinity();
                for (int selected_index : selected) {
                    if (selected_index < 0 ||
                        selected_index >= static_cast<int>(run_.basins.size())) {
                        continue;
                    }
                    const BasinElite& selected_basin =
                        run_.basins[static_cast<std::size_t>(selected_index)];
                    nearest_topology = std::min(
                        nearest_topology,
                        this->basin_topology_distance(
                            key,
                            active_family_key_for_basin_index(selected_index)));
                    nearest_scaled = std::min(
                        nearest_scaled,
                        this->basin_scaled_distance(basin, selected_basin));
                }
                if (!std::isfinite(nearest_topology)) {
                    nearest_topology = 0.0;
                }
                if (!std::isfinite(nearest_scaled)) {
                    nearest_scaled = 0.0;
                }
                return nearest_topology * 1000.0 + nearest_scaled;
            };
        auto add_active_slot_candidate =
            [&](std::vector<int>& selected, int index) {
                if (index < 0 ||
                    index >= static_cast<int>(run_.basins.size()) ||
                    static_cast<int>(selected.size()) >= limit ||
                    std::find(selected.begin(), selected.end(), index) != selected.end() ||
                    !active_family_slot_quota_ok(selected, index)) {
                    return false;
                }
                selected.push_back(index);
                return true;
            };
        auto pick_active_diversity_fill = [&](const std::vector<int>& pool,
                                              std::vector<int>& selected,
                                              bool avoid_recent_drift) {
            while (static_cast<int>(selected.size()) < limit) {
                int best_index = -1;
                bool best_recent_drift = true;
                double best_distance_score = -std::numeric_limits<double>::infinity();
                for (int index : pool) {
                    if (index < 0 ||
                        index >= static_cast<int>(run_.basins.size()) ||
                        std::find(selected.begin(), selected.end(), index) != selected.end() ||
                        !active_family_slot_quota_ok(selected, index)) {
                        continue;
                    }
                    const bool recent_drift =
                        active_family_recently_drifted(
                            active_family_key_for_basin_index(index));
                    if (avoid_recent_drift && recent_drift) {
                        continue;
                    }
                    const double distance_score =
                        active_selection_distance_score(index, selected);
                    bool better = best_index < 0;
                    if (!better && recent_drift != best_recent_drift) {
                        better = !recent_drift && best_recent_drift;
                    }
                    if (!better &&
                        std::abs(distance_score - best_distance_score) > 1.0e-12) {
                        better = distance_score > best_distance_score;
                    }
                    if (!better &&
                        candidate_better(
                            run_.basins[static_cast<std::size_t>(index)].fitness,
                            run_.basins[static_cast<std::size_t>(best_index)].fitness,
                            options)) {
                        better = true;
                    }
                    if (better) {
                        best_index = index;
                        best_recent_drift = recent_drift;
                        best_distance_score = distance_score;
                    }
                }
                if (!add_active_slot_candidate(selected, best_index)) {
                    break;
                }
            }
        };
        const std::string active_selection_mode =
            normalized_token(options.basins.active_selection_mode);
        if (active_selection_mode == "topology_merit_potential" ||
            active_selection_mode == "merit_potential" ||
            active_selection_mode == "potential_merit" ||
            active_selection_mode == "merit_potential_diversity" ||
            active_selection_mode == "potential_topology") {
            std::vector<int> mixed_candidates = eligible;
            for (int index : family_candidates) {
                if (std::find(mixed_candidates.begin(), mixed_candidates.end(), index) ==
                    mixed_candidates.end()) {
                    mixed_candidates.push_back(index);
                }
            }
            std::vector<int> selected;
            selected.reserve(static_cast<std::size_t>(limit));
            const int per_topology_limit =
                options.basins.semantic_diversity ? 1 : -1;
            const double min_same_key_distance = min_distance;
            auto contains = [&](int index) {
                return std::find(selected.begin(), selected.end(), index) != selected.end();
            };
            auto key_for_index = [&](int index) {
                const BasinElite& basin = run_.basins[static_cast<std::size_t>(index)];
                return run_.topology_family_similarity_enabled
                           ? this->basin_family_group_key(basin)
                           : this->basin_key(basin);
            };
            auto selected_same_key_count = [&](const std::string& key) {
                return static_cast<int>(std::count_if(
                    selected.begin(),
                    selected.end(),
                    [&](int selected_index) {
                        return key_for_index(selected_index) == key;
                    }));
            };
            auto same_key_distance_ok = [&](int index) {
                if (per_topology_limit < 0) {
                    return true;
                }
                const std::string key = key_for_index(index);
                const int same_key_count = selected_same_key_count(key);
                if (same_key_count < per_topology_limit) {
                    return true;
                }
                if (min_same_key_distance <= 0.0) {
                    return false;
                }
                double nearest_same_key = std::numeric_limits<double>::infinity();
                const BasinElite& basin = run_.basins[static_cast<std::size_t>(index)];
                for (int selected_index : selected) {
                    if (key_for_index(selected_index) != key) {
                        continue;
                    }
                    nearest_same_key = std::min(
                        nearest_same_key,
                        this->basin_scaled_distance(
                            basin,
                            run_.basins[static_cast<std::size_t>(selected_index)]));
                }
                return nearest_same_key >= min_same_key_distance;
            };
            auto allowed = [&](int index, bool enforce_same_key) {
                if (index < 0 ||
                    index >= static_cast<int>(run_.basins.size()) ||
                    contains(index) ||
                    static_cast<int>(selected.size()) >= limit) {
                    return false;
                }
                return active_family_slot_quota_ok(selected, index) &&
                       (!enforce_same_key || same_key_distance_ok(index));
            };
            auto add = [&](int index, bool enforce_same_key = true) {
                if (!allowed(index, enforce_same_key)) {
                    return false;
                }
                return add_active_slot_candidate(selected, index);
            };
            auto better_merit_index = [&](int a, int b) {
                if (candidate_better(
                        run_.basins[static_cast<std::size_t>(a)].fitness,
                        run_.basins[static_cast<std::size_t>(b)].fitness,
                        options)) {
                    return true;
                }
                if (candidate_better(
                        run_.basins[static_cast<std::size_t>(b)].fitness,
                        run_.basins[static_cast<std::size_t>(a)].fitness,
                        options)) {
                    return false;
                }
                return a < b;
            };
            auto potential_for_index = [&](int index) {
                const Score& score =
                    run_.basins[static_cast<std::size_t>(index)].fitness.score;
                if (std::isfinite(score.topology_potential_score_v3)) {
                    return score.topology_potential_score_v3;
                }
                return score.topology_potential_score_v2;
            };
            auto better_potential_index = [&](int a, int b) {
                const double pa = potential_for_index(a);
                const double pb = potential_for_index(b);
                const bool fa = std::isfinite(pa);
                const bool fb = std::isfinite(pb);
                if (fa != fb) {
                    return fa;
                }
                if (fa && std::abs(pa - pb) > 1.0e-12) {
                    return pa > pb;
                }
                return better_merit_index(a, b);
            };
            std::vector<int> merit_order = mixed_candidates;
            std::stable_sort(merit_order.begin(), merit_order.end(), better_merit_index);
            std::vector<int> potential_order = mixed_candidates;
            std::stable_sort(
                potential_order.begin(),
                potential_order.end(),
                better_potential_index);

            const int merit_quota =
                std::min(limit, std::max(1, static_cast<int>(std::round(limit * 0.40))));
            const int potential_quota =
                std::min(limit - merit_quota,
                         std::max(0, static_cast<int>(std::round(limit * 0.40))));
            const int potential_target = merit_quota + potential_quota;

            for (int index : merit_order) {
                if (static_cast<int>(selected.size()) >= merit_quota) {
                    break;
                }
                add(index);
            }
            for (int index : potential_order) {
                if (static_cast<int>(selected.size()) >= potential_target) {
                    break;
                }
                if (std::isfinite(potential_for_index(index))) {
                    add(index);
                }
            }
            for (int index : merit_order) {
                if (static_cast<int>(selected.size()) >= potential_target) {
                    break;
                }
                add(index);
            }

            while (static_cast<int>(selected.size()) < limit) {
                int best_candidate = -1;
                double best_topology_distance = -1.0;
                double best_scaled_distance = -1.0;
                double best_potential = -std::numeric_limits<double>::infinity();
                for (int index : mixed_candidates) {
                    if (!allowed(index, true)) {
                        continue;
                    }
                    const BasinElite& basin = run_.basins[static_cast<std::size_t>(index)];
                    const std::string key = key_for_index(index);
                    double nearest_topology = std::numeric_limits<double>::infinity();
                    double nearest_scaled = std::numeric_limits<double>::infinity();
                    for (int selected_index : selected) {
                        nearest_topology = std::min(
                            nearest_topology,
                            this->basin_topology_distance(key, key_for_index(selected_index)));
                        nearest_scaled = std::min(
                            nearest_scaled,
                            this->basin_scaled_distance(
                                basin,
                                run_.basins[static_cast<std::size_t>(selected_index)]));
                    }
                    if (selected.empty()) {
                        nearest_topology = 0.0;
                        nearest_scaled = 0.0;
                    }
                    const double potential = potential_for_index(index);
                    const double finite_potential =
                        std::isfinite(potential)
                            ? potential
                            : -std::numeric_limits<double>::infinity();
                    bool better =
                        nearest_topology > best_topology_distance + 1.0e-12;
                    if (!better &&
                        std::abs(nearest_topology - best_topology_distance) <= 1.0e-12) {
                        better = nearest_scaled > best_scaled_distance + 1.0e-12;
                    }
                    if (!better &&
                        std::abs(nearest_topology - best_topology_distance) <= 1.0e-12 &&
                        std::abs(nearest_scaled - best_scaled_distance) <= 1.0e-12) {
                        better = finite_potential > best_potential + 1.0e-12;
                    }
                    if (!better &&
                        std::abs(nearest_topology - best_topology_distance) <= 1.0e-12 &&
                        std::abs(nearest_scaled - best_scaled_distance) <= 1.0e-12 &&
                        std::abs(finite_potential - best_potential) <= 1.0e-12) {
                        better = best_candidate < 0 ||
                                 better_merit_index(index, best_candidate);
                    }
                    if (better) {
                        best_candidate = index;
                        best_topology_distance = nearest_topology;
                        best_scaled_distance = nearest_scaled;
                        best_potential = finite_potential;
                    }
                }
                if (!add(best_candidate)) {
                    break;
                }
            }
            for (int index : merit_order) {
                if (static_cast<int>(selected.size()) >= limit) {
                    break;
                }
                add(index, false);
            }
            return selected;
        }
        std::vector<int> representatives =
            run_.descriptor_select_enabled
                ? this->select_basin_descriptor_representative_indices(
                      eligible,
                      limit,
                      1,
                      min_distance,
                      true)
                : this->select_basin_representative_indices(
                      eligible,
                      limit,
                      1,
                      min_distance,
                      true);
        std::vector<int> selected;
        selected.reserve(static_cast<std::size_t>(limit));
        for (int index : representatives) {
            add_active_slot_candidate(selected, index);
        }
        if (static_cast<int>(selected.size()) < limit) {
            pick_active_diversity_fill(eligible, selected, true);
        }
        if (static_cast<int>(selected.size()) < limit) {
            std::vector<int> fallback =
                run_.descriptor_select_enabled
                    ? this->select_basin_descriptor_representative_indices(
                          family_candidates,
                          limit,
                          1,
                          min_distance,
                          true)
                    : this->select_basin_representative_indices(
                          family_candidates,
                          limit,
                          1,
                          min_distance,
                          true);
            for (int index : fallback) {
                if (static_cast<int>(selected.size()) >= limit) {
                    break;
                }
                add_active_slot_candidate(selected, index);
            }
        }
        if (static_cast<int>(selected.size()) < limit) {
            pick_active_diversity_fill(family_candidates, selected, true);
        }
        if (static_cast<int>(selected.size()) < limit) {
            pick_active_diversity_fill(family_candidates, selected, false);
        }
        return selected;
    }

void AdaptiveSession::inject_active_basins() {
        run_.active_basin_slots.clear();
        run_.active_basin_slot_origin_keys.clear();
        run_.active_basin_slot_family_keys.clear();
        const std::vector<int> active_indices = this->select_active_basin_indices();
        const bool archive_only = this->active_archive_only_enabled();
        int slot = 1;
        for (int basin_index : active_indices) {
            if (!archive_only && slot >= run_.pop_size) {
                break;
            }
            const BasinElite& basin = run_.basins[static_cast<std::size_t>(basin_index)];
            run_.active_basin_slot_origin_keys.push_back(this->active_lifecycle_key_for_basin(basin));
            run_.active_basin_slot_family_keys.push_back(this->active_current_family_key_for_basin(basin));
            if (!archive_only) {
                set_population_row(run_.population, slot, basin.scaled_values, owner.variables_, run_.internal_mode);
                CandidateFitness injected = basin.fitness;
                injected.population_index = slot;
                injected.source = "active_basin";
                run_.fitness[static_cast<std::size_t>(slot)] = injected;
                run_.active_basin_slots.push_back(slot);
                ++slot;
            }
        }
        run_.population_niche_id.assign(static_cast<std::size_t>(run_.pop_size), -1);
        run_.active_niche_family_keys.clear();
        run_.active_niche_membership_reset_pending = true;
        if (!run_.active_basin_slots.empty()) {
            this->sync_merits();
            this->update_best_from_population();
        }
    }

int AdaptiveSession::matching_active_basin_slot(const std::vector<double>& scaled) {
        const double match_distance = std::max(1.0e-12, options.basins.radius * 0.25);
        for (int slot : run_.active_basin_slots) {
            if (slot < 0 || slot >= run_.pop_size) {
                continue;
            }
            if (!run_.descriptor_full_enabled &&
                options.basins.semantic_diversity &&
                !run_.fitness[static_cast<std::size_t>(slot)].topology_key.empty()) {
                const std::string key = this->topology_key_for_scaled(scaled);
                const std::string slot_key =
                    run_.fitness[static_cast<std::size_t>(slot)].topology_key;
                if (!key.empty() &&
                    (run_.topology_similarity_enabled
                         ? !this->active_topology_keys_are_similar(slot_key, key)
                         : key != slot_key)) {
                    continue;
                }
            }
            const std::vector<double> row = population_row(run_.population, slot, run_.dim);
            if (scaled_vector_distance(
                    owner.variables_,
                    scaled,
                    row,
                    options.basins.distance_metric,
                    run_.internal_mode,
                    &owner.base_,
                    options.basins.air_thickness_distance_cap) <= match_distance) {
                return slot;
            }
        }
        return -1;
    }

} } // namespace optiland::optimizer_detail
