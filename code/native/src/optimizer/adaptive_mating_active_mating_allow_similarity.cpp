/*
 * Build local/global adaptive mating pools and draw parent/archive indices under family quotas.
 * Trial generation shares these pools with active-family policy and uses run_.rng plus run_.uni.
 * Preserve global index order, exclusion rules and the sequence of fallback draws.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

bool AdaptiveSession::active_mating_allow_similarity() {
        std::string mode = options.basins.active_mating_match_mode;
        std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char ch) {
            if (ch == '-' || std::isspace(ch)) {
                return '_';
            }
            return static_cast<char>(std::tolower(ch));
        });
        return mode == "family" ||
               mode == "family_similarity" ||
               mode == "similarity" ||
               mode == "topology_family" ||
               mode == "topology_family_similarity" ||
               mode == "basin_local";
    }

DeMatingSelection AdaptiveSession::draw_global_de_mating(int i, int pbest_population_index) {
        DeMatingSelection selection;
        selection.r0 = this->draw_population_index_excluding_list({i, pbest_population_index});
        selection.r1 =
            this->draw_population_index_excluding_list({i, pbest_population_index, selection.r0});
        const int total_pool =
            run_.pop_size + (run_.variant_uses_archive ? static_cast<int>(run_.external_archive.size()) : 0);
        for (int attempt = 0; attempt < 64; ++attempt) {
            selection.r2_pool =
                std::min(total_pool - 1, static_cast<int>(run_.uni(run_.rng) * total_pool));
            if (selection.r2_pool >= run_.pop_size ||
                (selection.r2_pool != i && selection.r2_pool != selection.r0 &&
                 selection.r2_pool != selection.r1 &&
                 selection.r2_pool != pbest_population_index)) {
                break;
            }
        }
        if (total_pool <= run_.pop_size &&
            (selection.r2_pool == i || selection.r2_pool == selection.r0 ||
             selection.r2_pool == selection.r1 ||
             selection.r2_pool == pbest_population_index)) {
            selection.r2_pool = this->draw_population_index_excluding_list(
                {i, pbest_population_index, selection.r0, selection.r1});
        }
        selection.archive_r2 =
            run_.variant_uses_archive && selection.r2_pool >= run_.pop_size && !run_.external_archive.empty();
        return selection;
    }

DeMatingSelection AdaptiveSession::draw_external_probe_de_mating(int i, int pbest_population_index, const LocalMatingPools& pools, const std::string& family_key) {
        auto outside_family = [&](const std::string& key) {
            return family_key.empty() || key.empty() ||
                   !this->active_keys_match(
                       key,
                       family_key,
                       this->active_mating_allow_similarity());
        };
        auto population_allowed = [&](int idx, std::initializer_list<int> excluded) {
            if (idx < 0 || idx >= run_.pop_size ||
                std::find(excluded.begin(), excluded.end(), idx) != excluded.end()) {
                return false;
            }
            if (idx >= static_cast<int>(pools.population_keys.size())) {
                return true;
            }
            return outside_family(pools.population_keys[static_cast<std::size_t>(idx)]);
        };
        auto draw_population_external = [&](std::initializer_list<int> excluded) {
            for (int attempt = 0; attempt < 96; ++attempt) {
                const int idx =
                    std::min(run_.pop_size - 1, static_cast<int>(run_.uni(run_.rng) * run_.pop_size));
                if (population_allowed(idx, excluded)) {
                    return idx;
                }
            }
            for (int idx = 0; idx < run_.pop_size; ++idx) {
                if (population_allowed(idx, excluded)) {
                    return idx;
                }
            }
            return this->draw_population_index_excluding_list(excluded);
        };
        DeMatingSelection selection;
        selection.r0 = draw_population_external({i, pbest_population_index});
        selection.r1 = draw_population_external({i, pbest_population_index, selection.r0});
        const int total_pool =
            run_.pop_size + (run_.variant_uses_archive ? static_cast<int>(run_.external_archive.size()) : 0);
        for (int attempt = 0; attempt < 96; ++attempt) {
            const int pool_idx =
                std::min(total_pool - 1, static_cast<int>(run_.uni(run_.rng) * total_pool));
            if (pool_idx < run_.pop_size) {
                if (population_allowed(
                        pool_idx,
                        {i, pbest_population_index, selection.r0, selection.r1})) {
                    selection.r2_pool = pool_idx;
                    break;
                }
            } else {
                const int archive_index = pool_idx - run_.pop_size;
                const bool key_ok =
                    archive_index < static_cast<int>(pools.archive_keys.size())
                        ? outside_family(
                              pools.archive_keys[static_cast<std::size_t>(archive_index)])
                        : true;
                if (key_ok) {
                    selection.r2_pool = pool_idx;
                    break;
                }
            }
        }
        if (selection.r2_pool < 0) {
            return this->draw_global_de_mating(i, pbest_population_index);
        }
        selection.archive_r2 =
            run_.variant_uses_archive && selection.r2_pool >= run_.pop_size && !run_.external_archive.empty();
        return selection;
    }

LocalMatingPools AdaptiveSession::build_local_mating_pools() {
        LocalMatingPools pools;
        pools.population_keys.resize(static_cast<std::size_t>(run_.pop_size));
        pools.active_cohort_keys.resize(static_cast<std::size_t>(run_.pop_size));
        
        for (int idx = 0; idx < run_.pop_size; ++idx) {
            std::string key =
                this->fitness_diversity_key(run_.fitness[static_cast<std::size_t>(idx)]);
            pools.population_keys[static_cast<std::size_t>(idx)] = key;
            if (!key.empty()) {
                pools.population_by_key[std::move(key)].push_back(idx);
            }
        }
        auto assign_active_cohort_key = [&](int idx, const std::string& key) {
            if (idx < 0 || idx >= run_.pop_size || key.empty()) {
                return;
            }
            pools.active_cohort_keys[static_cast<std::size_t>(idx)] = key;
            this->add_population_pool_index(pools, key, idx);
            auto& cohort_bucket = pools.active_cohort_by_key[key];
            if (std::find(
                    cohort_bucket.begin(),
                    cohort_bucket.end(),
                    idx) == cohort_bucket.end()) {
                cohort_bucket.push_back(idx);
            }
        };
        if ((this->active_niche_soft_fill_enabled() || this->active_focus_soft_fill_enabled()) &&
            run_.population_niche_id.size() == static_cast<std::size_t>(run_.pop_size) &&
            !run_.active_niche_family_keys.empty()) {
            for (int idx = 0; idx < run_.pop_size; ++idx) {
                const int niche_id =
                    run_.population_niche_id[static_cast<std::size_t>(idx)];
                if (niche_id < 0 ||
                    niche_id >=
                        static_cast<int>(run_.active_niche_family_keys.size())) {
                    continue;
                }
                const std::string& family_key =
                    run_.active_niche_family_keys[static_cast<std::size_t>(niche_id)];
                ++pools.active_niche_member_count;
                if (this->active_focus_key_matches_current(family_key)) {
                    ++pools.active_focus_member_count;
                }
                if (this->active_niche_soft_fill_enabled() ||
                    this->active_focus_key_matches_current(family_key)) {
                    assign_active_cohort_key(idx, family_key);
                }
            }
        }
        if (!run_.active_focus_family_keys.empty()) {
            const int focus_count =
                std::max(1, static_cast<int>(run_.active_focus_family_keys.size()));
            const double focus_fraction = std::clamp(
                options.basins.active_focus_population_fraction,
                0.0,
                1.0);
            const int focus_population = std::clamp(
                static_cast<int>(std::ceil(
                    focus_fraction * static_cast<double>(run_.pop_size))),
                focus_count,
                std::max(1, run_.pop_size));
            int focus_target = static_cast<int>(std::ceil(
                static_cast<double>(focus_population) /
                static_cast<double>(focus_count)));
            if (options.basins.active_focus_min_quota > 0) {
                focus_target = std::max(
                    focus_target,
                    options.basins.active_focus_min_quota);
            }
            pools.active_focus_target_quota =
                std::clamp(focus_target, 1, std::max(1, run_.pop_size));
        }
        const bool active_focus_rebuild_underfilled =
            options.basins.active_focus_rebalance &&
            !run_.active_focus_family_keys.empty() &&
            pools.active_focus_target_quota > 0 &&
            pools.active_focus_member_count <
                static_cast<int>(std::ceil(
                    0.70 * static_cast<double>(pools.active_focus_target_quota)));
        const bool active_niche_coverage_low =
            run_.population_niche_id.size() == static_cast<std::size_t>(run_.pop_size) &&
            !run_.active_niche_family_keys.empty() &&
            pools.active_niche_member_count <
                static_cast<int>(std::ceil(0.70 * static_cast<double>(run_.pop_size)));
        const bool skip_active_local_cohort =
            active_focus_rebuild_underfilled || active_niche_coverage_low;
        run_.last_active_local_cohort_family_count = 0;
        run_.last_active_local_cohort_anchor_count = 0;
        run_.last_active_local_cohort_total_limit = 0;
        run_.last_active_local_cohort_per_family = 0;
        run_.last_active_local_cohort_selected_count = 0;
        run_.last_active_local_cohort_max_family_size = 0;
        
    this->add_active_local_cohorts(pools, skip_active_local_cohort);

        pools.archive_keys = run_.external_archive_keys;
        if (pools.archive_keys.size() != run_.external_archive.size()) {
            pools.archive_keys.resize(run_.external_archive.size());
        }
        for (int idx = 0; idx < static_cast<int>(pools.archive_keys.size()); ++idx) {
            const std::string& key = pools.archive_keys[static_cast<std::size_t>(idx)];
            if (!key.empty()) {
                pools.archive_by_key[key].push_back(idx);
            }
        }
        return pools;
    }

int AdaptiveSession::draw_population_pool_index_excluding(const std::vector<int>& pool, std::initializer_list<int> excluded) {
            auto allowed = [&](int idx) {
                return idx >= 0 &&
                       std::find(excluded.begin(), excluded.end(), idx) == excluded.end();
            };
            if (pool.empty()) {
                return -1;
            }
            for (int attempt = 0; attempt < 64; ++attempt) {
                const int pool_idx =
                    std::min(static_cast<int>(pool.size()) - 1,
                             static_cast<int>(run_.uni(run_.rng) * pool.size()));
                const int idx = pool[static_cast<std::size_t>(pool_idx)];
                if (allowed(idx)) {
                    return idx;
                }
            }
            for (int idx : pool) {
                if (allowed(idx)) {
                    return idx;
                }
            }
            return -1;
        }

const std::vector<int>& AdaptiveSession::local_population_pool(LocalMatingPools& pools, const std::string& key, bool allow_similarity) {
            static const std::vector<int> empty;
            if (key.empty()) {
                return empty;
            }
            if (!allow_similarity) {
                if (options.de_mating_scope == DeMatingScope::ActiveBasinOnly &&
                    options.basins.active_local_cohort) {
                    auto cohort_it = pools.active_cohort_by_key.find(key);
                    if (cohort_it != pools.active_cohort_by_key.end() &&
                        !cohort_it->second.empty()) {
                        return cohort_it->second;
                    }
                }
                auto it = pools.population_by_key.find(key);
                return it != pools.population_by_key.end() ? it->second : empty;
            }
            auto cache_it = pools.basin_population_cache.find(key);
            if (cache_it != pools.basin_population_cache.end()) {
                return cache_it->second;
            }
            std::vector<int> pool;
            pool.reserve(pools.population_keys.size());
            if (options.de_mating_scope == DeMatingScope::ActiveBasinOnly &&
                options.basins.active_local_cohort) {
                for (int idx = 0; idx < static_cast<int>(pools.active_cohort_keys.size());
                     ++idx) {
                    const std::string& cohort_key =
                        pools.active_cohort_keys[static_cast<std::size_t>(idx)];
                    if (this->diversity_keys_match(cohort_key, key, true)) {
                        pool.push_back(idx);
                    }
                }
                auto inserted = pools.basin_population_cache.emplace(key, std::move(pool));
                return inserted.first->second;
            }
            for (int idx = 0; idx < static_cast<int>(pools.population_keys.size()); ++idx) {
                const std::string& population_key =
                    pools.population_keys[static_cast<std::size_t>(idx)];
                const std::string cohort_key =
                    idx < static_cast<int>(pools.active_cohort_keys.size())
                        ? pools.active_cohort_keys[static_cast<std::size_t>(idx)]
                        : std::string{};
                if (this->diversity_keys_match(population_key, key, true) ||
                    this->diversity_keys_match(cohort_key, key, true)) {
                    pool.push_back(idx);
                }
            }
            auto inserted = pools.basin_population_cache.emplace(key, std::move(pool));
            return inserted.first->second;
        }

const std::vector<int>& AdaptiveSession::local_archive_pool(LocalMatingPools& pools, const std::string& key, bool allow_similarity) {
            static const std::vector<int> empty;
            if (key.empty()) {
                return empty;
            }
            if (!allow_similarity) {
                auto it = pools.archive_by_key.find(key);
                return it != pools.archive_by_key.end() ? it->second : empty;
            }
            auto cache_it = pools.basin_archive_cache.find(key);
            if (cache_it != pools.basin_archive_cache.end()) {
                return cache_it->second;
            }
            std::vector<int> pool;
            pool.reserve(pools.archive_keys.size());
            for (int idx = 0; idx < static_cast<int>(pools.archive_keys.size()); ++idx) {
                if (this->diversity_keys_match(
                        pools.archive_keys[static_cast<std::size_t>(idx)],
                        key,
                        true)) {
                    pool.push_back(idx);
                }
            }
            auto inserted = pools.basin_archive_cache.emplace(key, std::move(pool));
            return inserted.first->second;
        }

std::vector<int> AdaptiveSession::population_pool_in_global_order(const std::vector<int>& candidates, const std::vector<int>& order) {
            std::vector<unsigned char> included(
                static_cast<std::size_t>(run_.pop_size),
                static_cast<unsigned char>(0));
            for (int candidate : candidates) {
                if (candidate >= 0 && candidate < run_.pop_size) {
                    included[static_cast<std::size_t>(candidate)] =
                        static_cast<unsigned char>(1);
                }
            }
            std::vector<int> sorted;
            sorted.reserve(candidates.size());
            for (int candidate : order) {
                if (candidate >= 0 && candidate < run_.pop_size &&
                    included[static_cast<std::size_t>(candidate)] != 0) {
                    sorted.push_back(candidate);
                }
            }
            return sorted;
        }

const std::vector<int>& AdaptiveSession::local_sorted_population_pbest_pool(LocalMatingPools& pools, const std::string& key, bool allow_similarity, const std::vector<int>& order) {
            static const std::vector<int> empty;
            if (key.empty()) {
                return empty;
            }
            if (allow_similarity) {
                auto cache_it = pools.basin_pbest_cache.find(key);
                if (cache_it != pools.basin_pbest_cache.end()) {
                    return cache_it->second;
                }
                const std::vector<int>& pool =
                    this->local_population_pool(pools, key, true);
                auto inserted = pools.basin_pbest_cache.emplace(
                    key, this->population_pool_in_global_order(pool, order));
                return inserted.first->second;
            }
            auto cache_it = pools.same_key_pbest_cache.find(key);
            if (cache_it != pools.same_key_pbest_cache.end()) {
                return cache_it->second;
            }
            const std::vector<int>& pool =
                this->local_population_pool(pools, key, false);
            auto inserted =
                pools.same_key_pbest_cache.emplace(
                    key, this->population_pool_in_global_order(pool, order));
            return inserted.first->second;
        }

ActiveMatingQuotaState AdaptiveSession::make_active_mating_quota_state(const LocalMatingPools& pools) {
        ActiveMatingQuotaState state;
        if (options.de_mating_scope != DeMatingScope::ActiveBasinOnly ||
            !options.basins.active_mating_quota) {
            return state;
        }
        state.family_keys = this->active_family_keys_from_frozen_slots();
        if (state.family_keys.empty()) {
            state.family_keys = this->active_family_keys_from_slots(pools);
        }
        if (state.family_keys.empty()) {
            return state;
        }
        state.configured = true;
        const double local_rate =
            std::clamp(options.de_mating_local_rate, 0.0, 1.0);
        int quota_population = run_.pop_size;
        if (options.basins.active_local_cohort) {
            int cohort_population = 0;
            for (const std::string& key : pools.active_cohort_keys) {
                if (!key.empty()) {
                    ++cohort_population;
                }
            }
            if (cohort_population > 0) {
                quota_population = cohort_population;
            }
        }
        const int total_quota = std::clamp(
            static_cast<int>(std::ceil(local_rate * static_cast<double>(quota_population))),
            0,
            quota_population);
        state.per_family_limit =
            state.family_keys.empty()
                ? 0
                : static_cast<int>(std::ceil(
                      static_cast<double>(total_quota) /
                      static_cast<double>(state.family_keys.size())));
        state.per_family_limit = std::clamp(state.per_family_limit, 0, run_.pop_size);
        state.used.assign(state.family_keys.size(), 0);
        state.family_limits.assign(state.family_keys.size(), state.per_family_limit);
        state.stalled.assign(state.family_keys.size(), 0);
        if (this->active_plateau_lifecycle_enabled() && state.per_family_limit > 0) {
            const double quota_scale = std::clamp(
                options.basins.active_plateau_local_quota_scale,
                0.0,
                1.0);
            for (std::size_t family = 0; family < state.family_keys.size(); ++family) {
                if (!this->active_lifecycle_mating_key_stalled(state.family_keys[family]) ||
                    this->active_focus_key_matches_current(state.family_keys[family])) {
                    continue;
                }
                state.stalled[family] = 1;
                ++state.stalled_family_count;
                state.family_limits[family] = std::clamp(
                    static_cast<int>(std::ceil(
                        static_cast<double>(state.per_family_limit) * quota_scale)),
                    0,
                    state.per_family_limit);
            }
        }
        state.enabled = state.per_family_limit > 0;
        return state;
    }

std::string AdaptiveSession::select_local_mating_key(int i, const PBestGuideSelection& pbest_guide, const LocalMatingPools& pools, DeMatingScope scope) {
        const std::string& active_key = this->active_target_key_for_index(i, pools);
        if (!active_key.empty()) {
            return active_key;
        }
        if (scope != DeMatingScope::SameKey &&
            pbest_guide.source == PBestGuideSource::BasinArchive &&
            !pbest_guide.guide_key.empty()) {
            return pbest_guide.guide_key;
        }
        return this->fitness_diversity_key(run_.fitness[static_cast<std::size_t>(i)]);
    }

int AdaptiveSession::draw_local_r2_pool(const std::vector<int>& population_pool, const std::vector<int>& archive_pool, std::initializer_list<int> excluded) {
        const int total_pool =
            static_cast<int>(population_pool.size()) +
            (run_.variant_uses_archive ? static_cast<int>(archive_pool.size()) : 0);
        if (total_pool <= 0) {
            return -1;
        }
        auto population_allowed = [&](int idx) {
            return idx >= 0 &&
                   std::find(excluded.begin(), excluded.end(), idx) == excluded.end();
        };
        for (int attempt = 0; attempt < 64; ++attempt) {
            const int pool_idx =
                std::min(total_pool - 1, static_cast<int>(run_.uni(run_.rng) * total_pool));
            if (pool_idx < static_cast<int>(population_pool.size())) {
                const int idx = population_pool[static_cast<std::size_t>(pool_idx)];
                if (population_allowed(idx)) {
                    return idx;
                }
            } else {
                const int archive_offset = pool_idx - static_cast<int>(population_pool.size());
                return run_.pop_size + archive_pool[static_cast<std::size_t>(archive_offset)];
            }
        }
        for (int idx : population_pool) {
            if (population_allowed(idx)) {
                return idx;
            }
        }
        if (run_.variant_uses_archive && !archive_pool.empty()) {
            return run_.pop_size + archive_pool.front();
        }
        return -1;
    }

DeMatingSelection AdaptiveSession::try_draw_local_de_mating(int i, const PBestGuideSelection& pbest_guide, LocalMatingPools& pools, DeMatingScope scope, DeMatingStats& stats) {
        DeMatingSelection selection;
        const int pbest_population_index = pbest_guide.population_index;
        const std::string key =
            this->select_local_mating_key(i, pbest_guide, pools, scope);
        if (key.empty()) {
            ++stats.key_unknown;
            return selection;
        }
        const bool allow_similarity = scope != DeMatingScope::SameKey;
        const std::vector<int> strict_population_pool =
            this->strict_active_slot_population_pool(i, pools, allow_similarity);
        const std::vector<int> strict_archive_pool =
            this->strict_active_slot_archive_pool(i, pools, allow_similarity);
        const std::vector<int>& population_pool = this->active_slot_uses_strict_frozen_family(i)
                                                      ? strict_population_pool
                                                      : this->local_population_pool(
                                                            pools,
                                                            key,
                                                            allow_similarity);
        const std::vector<int>& archive_pool = this->active_slot_uses_strict_frozen_family(i)
                                                   ? strict_archive_pool
                                                   : this->local_archive_pool(
                                                         pools,
                                                         key,
                                                         allow_similarity);
        stats.record_local_pool(static_cast<int>(population_pool.size()));
        if (static_cast<int>(population_pool.size()) <
            std::max(4, options.de_mating_min_pool)) {
            return selection;
        }
        selection.r0 =
            this->draw_population_pool_index_excluding(population_pool, {i, pbest_population_index});
        selection.r1 = this->draw_population_pool_index_excluding(
            population_pool,
            {i, pbest_population_index, selection.r0});
        if (selection.r0 < 0 || selection.r1 < 0) {
            return DeMatingSelection{};
        }
        selection.r2_pool = this->draw_local_r2_pool(
            population_pool,
            archive_pool,
            {i, pbest_population_index, selection.r0, selection.r1});
        if (selection.r2_pool < 0) {
            return DeMatingSelection{};
        }
        selection.used_local = true;
        selection.archive_r2 =
            run_.variant_uses_archive && selection.r2_pool >= run_.pop_size && !archive_pool.empty();
        return selection;
    }

DeMatingPlan AdaptiveSession::make_de_mating_plan(int i, LocalMatingPools& pools, ActiveMatingQuotaState& active_quota, DeMatingStats& stats) {
        DeMatingPlan plan;
        const DeMatingScope scope = options.de_mating_scope;
        plan.local_scope = scope;
        if (scope == DeMatingScope::SameKey || scope == DeMatingScope::BasinLocal) {
            plan.request_local = true;
        } else if (scope == DeMatingScope::ActiveBasinOnly) {
            auto maybe_plan_external_probe = [&](const std::string& family_key,
                                                 bool stalled) {
                if (!stalled || family_key.empty() ||
                    this->active_slot_uses_strict_frozen_family(i)) {
                    return false;
                }
                const double probe_rate = std::clamp(
                    options.basins.active_plateau_external_probe_rate,
                    0.0,
                    1.0);
                if (probe_rate <= 0.0 || run_.uni(run_.rng) >= probe_rate) {
                    return false;
                }
                plan.request_local = true;
                plan.external_probe = true;
                plan.external_probe_family_key = family_key;
                ++stats.active_plateau_external_probe;
                return true;
            };
            auto local_pool_ready = [&](const std::string& family_key) {
                if (family_key.empty()) {
                    return false;
                }
                const bool allow_similarity = this->active_mating_allow_similarity();
                const std::vector<int>& pool =
                    this->local_population_pool(pools, family_key, allow_similarity);
                return static_cast<int>(pool.size()) >=
                       std::max(4, options.de_mating_min_pool);
            };
            if (active_quota.configured) {
                stats.active_quota_family_count =
                    static_cast<int>(active_quota.family_keys.size());
                stats.active_quota_per_family = active_quota.per_family_limit;
                stats.active_plateau_quota_scaled_family_count =
                    active_quota.stalled_family_count;
                if (i >= 0 && i < static_cast<int>(pools.population_keys.size())) {
                    const int family = this->active_family_index_for_key(
                        this->active_local_target_key_for_index(i, pools),
                        active_quota.family_keys);
                    if (family >= 0) {
                        ++stats.active_quota_match;
                        const std::string& family_key =
                            active_quota.family_keys[static_cast<std::size_t>(family)];
                        const bool stalled =
                            family < static_cast<int>(active_quota.stalled.size()) &&
                            active_quota.stalled[static_cast<std::size_t>(family)] != 0;
                        if (maybe_plan_external_probe(family_key, stalled)) {
                            return plan;
                        }
                        int& used =
                            active_quota.used[static_cast<std::size_t>(family)];
                        const int family_limit =
                            family < static_cast<int>(active_quota.family_limits.size())
                                ? active_quota.family_limits[static_cast<std::size_t>(family)]
                                : active_quota.per_family_limit;
                        if (active_quota.enabled && used < family_limit &&
                            local_pool_ready(family_key)) {
                            ++used;
                            stats.record_active_quota_use(family, active_quota.used);
                            plan.request_local = true;
                        } else {
                            ++stats.active_quota_blocked;
                        }
                    }
                }
            } else {
                plan.request_local = this->target_matches_active_basin_family(i, pools);
                if (plan.request_local &&
                    i >= 0 &&
                    i < static_cast<int>(pools.population_keys.size())) {
                    const std::string& target_key =
                        this->active_local_target_key_for_index(i, pools);
                    std::vector<std::string> active_family_keys =
                        this->active_family_keys_from_frozen_slots();
                    if (active_family_keys.empty()) {
                        active_family_keys = this->active_family_keys_from_slots(pools);
                    }
                    const int family =
                        this->active_family_index_for_key(target_key, active_family_keys);
                    if (family >= 0) {
                        const std::string& family_key =
                            active_family_keys[static_cast<std::size_t>(family)];
                        if (!local_pool_ready(family_key)) {
                            plan.request_local = false;
                            return plan;
                        }
                        maybe_plan_external_probe(
                            family_key,
                            this->active_lifecycle_mating_key_stalled(family_key));
                    }
                }
            }
            plan.local_scope = this->active_mating_allow_similarity()
                                   ? DeMatingScope::BasinLocal
                                   : DeMatingScope::SameKey;
        } else if (scope == DeMatingScope::Mixed) {
            plan.request_local = run_.uni(run_.rng) < std::clamp(options.de_mating_local_rate, 0.0, 1.0);
            plan.local_scope = DeMatingScope::BasinLocal;
        }
        return plan;
    }

} } // namespace optiland::optimizer_detail
