/*
 * Choose an adaptive DE mating plan and prepare basin-guided refresh parent pools.
 * The trial generator combines population/basin identities with the local mating context supplied here.
 * Preserve guide-source precedence and pool insertion order when selecting compatible parents.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

DeMatingSelection AdaptiveSession::select_de_mating(int i, const PBestGuideSelection& pbest_guide, LocalMatingPools& pools, const DeMatingPlan& plan, DeMatingStats& stats) {
        const int pbest_population_index = pbest_guide.population_index;
        if (plan.external_probe) {
            DeMatingSelection external = this->draw_external_probe_de_mating(
                i,
                pbest_population_index,
                pools,
                plan.external_probe_family_key);
            ++stats.global;
            if (external.archive_r2) {
                ++stats.archive_global;
            }
            return external;
        }
        if (plan.request_local) {
            DeMatingSelection local =
                this->try_draw_local_de_mating(i, pbest_guide, pools, plan.local_scope, stats);
            if (local.used_local) {
                ++stats.local;
                if (local.archive_r2) {
                    ++stats.archive_local;
                }
                return local;
            }
            ++stats.fallback;
            if (this->active_slot_uses_strict_frozen_family(i)) {
                return DeMatingSelection{};
            }
        }
        DeMatingSelection global = this->draw_global_de_mating(i, pbest_population_index);
        ++stats.global;
        if (global.archive_r2) {
            ++stats.archive_global;
        }
        return global;
    }

BasinRefreshParentPool AdaptiveSession::make_basin_refresh_parent_pool(const bool& top_half_single_surface_refresh, LocalMatingPools& refresh_rebalance_pools, ActiveFamilyCoverage& refresh_rebalance_coverage, bool& refresh_rebalance_coverage_ready) {
                        BasinRefreshParentPool pool;
                        if (top_half_single_surface_refresh ||
                            !options.basins.enabled ||
                            !options.basins.balanced_refresh ||
                            run_.basins.empty()) {
                            return pool;
                        }
                        const int max_parents_per_topology =
                            std::max(1, options.basins.semantic_max_per_topology);
                        const int max_parents_per_family =
                            std::max(1, options.basins.semantic_max_per_topology);
                        const std::vector<int> active_parent_candidates =
                            this->active_basin_candidate_indices();
                        if (active_parent_candidates.empty() &&
                            !refresh_rebalance_coverage_ready) {
                            return pool;
                        }
                        if (refresh_rebalance_coverage_ready &&
                            !refresh_rebalance_coverage.family_keys.empty()) {
                            pool.family_parent_indices.resize(
                                refresh_rebalance_coverage.family_keys.size());
                        }
                        const std::vector<int> selected_parent_indices =
                            run_.descriptor_select_enabled
                                ? this->select_basin_descriptor_representative_indices(
                                      active_parent_candidates,
                                      static_cast<int>(run_.basins.size()),
                                      max_parents_per_topology,
                                      options.basins.radius,
                                      true)
                                : this->select_basin_representative_indices(
                                      active_parent_candidates,
                                      static_cast<int>(run_.basins.size()),
                                      max_parents_per_topology,
                                      options.basins.radius,
                                      true);
                        std::vector<std::string> topology_keys;
                        std::vector<int> topology_parent_counts;
                        std::vector<int> family_parent_counts(
                            pool.family_parent_indices.size(),
                            0);
                        auto topology_index = [&](const std::string& key) {
                            for (std::size_t i = 0; i < topology_keys.size(); ++i) {
                                if (topology_keys[i] == key) {
                                    return static_cast<int>(i);
                                }
                            }
                            return -1;
                        };
                        auto family_index_for_key = [&](const std::string& key) {
                            if (!refresh_rebalance_coverage_ready ||
                                refresh_rebalance_coverage.family_keys.empty()) {
                                return -1;
                            }
                            return this->active_family_index_for_key(
                                key,
                                refresh_rebalance_coverage.family_keys);
                        };
                        auto add_parent = [&](const std::vector<double>& scaled_values,
                                              const std::string& topology_key,
                                              const std::string& family_key,
                                              bool prefer_family) {
                            if (scaled_values.size() != static_cast<std::size_t>(run_.dim)) {
                                return;
                            }
                            int topology_group = -1;
                            if (!topology_key.empty()) {
                                topology_group = topology_index(topology_key);
                                if (topology_group < 0) {
                                    topology_group =
                                        static_cast<int>(topology_keys.size());
                                    topology_keys.push_back(topology_key);
                                    topology_parent_counts.push_back(0);
                                    pool.topology_parent_indices.emplace_back();
                                }
                            }
                            const int family_group = family_index_for_key(family_key);
                            const bool topology_room =
                                topology_group >= 0 &&
                                topology_group < static_cast<int>(
                                                     topology_parent_counts.size()) &&
                                topology_parent_counts[static_cast<std::size_t>(
                                    topology_group)] < max_parents_per_topology;
                            const bool family_room =
                                family_group >= 0 &&
                                family_group < static_cast<int>(
                                                   family_parent_counts.size()) &&
                                family_parent_counts[static_cast<std::size_t>(
                                    family_group)] < max_parents_per_family;
                            if (!topology_room && !(prefer_family && family_room) &&
                                !family_room) {
                                return;
                            }
                            const int parent_index =
                                static_cast<int>(pool.population.size() /
                                                 static_cast<std::size_t>(run_.dim));
                            pool.population.insert(
                                pool.population.end(),
                                scaled_values.begin(),
                                scaled_values.end());
                            if (topology_room) {
                                pool.topology_parent_indices[static_cast<std::size_t>(
                                    topology_group)]
                                    .push_back(parent_index);
                                ++topology_parent_counts[static_cast<std::size_t>(
                                    topology_group)];
                            }
                            if (family_room) {
                                pool.family_parent_indices[static_cast<std::size_t>(
                                    family_group)]
                                    .push_back(parent_index);
                                ++family_parent_counts[static_cast<std::size_t>(
                                    family_group)];
                            }
                        };
                        if (refresh_rebalance_coverage_ready &&
                            !refresh_rebalance_coverage.family_keys.empty()) {
                            const std::vector<ActiveFamilyAnchor> family_anchors =
                                this->build_active_family_anchors(refresh_rebalance_pools);
                            for (const ActiveFamilyAnchor& family : family_anchors) {
                                add_parent(
                                    family.row,
                                    family.key.empty() ? "__unknown__" : family.key,
                                    family.key,
                                    true);
                            }
                        }
                        for (int basin_index : selected_parent_indices) {
                            if (basin_index < 0 ||
                                basin_index >= static_cast<int>(run_.basins.size())) {
                                continue;
                            }
                            const BasinElite& basin =
                                run_.basins[static_cast<std::size_t>(basin_index)];
                            if (basin.scaled_values.size() != static_cast<std::size_t>(run_.dim)) {
                                continue;
                            }
                            if (this->global_refill_blocked_basin(basin)) {
                                continue;
                            }
                            std::string key =
                                run_.descriptor_full_enabled
                                    ? this->basin_diversity_key(basin)
                                    : (run_.topology_similarity_enabled
                                           ? this->basin_similarity_family_key(basin)
                                           : this->basin_key(basin));
                            if (key.empty()) {
                                key = "__unknown__";
                            }
                            add_parent(
                                basin.scaled_values,
                                key,
                                this->active_lifecycle_key_for_basin(basin),
                                false);
                        }
                        return pool;
                    }

void AdaptiveSession::add_population_pool_index(LocalMatingPools& pools, const std::string& key, int idx) {
            if (key.empty() || idx < 0 || idx >= run_.pop_size) {
                return;
            }
            auto& bucket = pools.population_by_key[key];
            if (std::find(bucket.begin(), bucket.end(), idx) == bucket.end()) {
                bucket.push_back(idx);
            }
        }

} } // namespace optiland::optimizer_detail
