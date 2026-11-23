/*
 * Select and record pbest guides from population, local families and available guide sources.
 * Adaptive mutation shares run_.population/fitness and consumes the session RNG for guide sampling.
 * Preserve exclusions, sorted candidate order and fallback draws to retain the same trial stream.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

int AdaptiveSession::draw_population_index_excluding_list(std::initializer_list<int> excluded) {
        auto allowed = [&](int idx) {
            return std::find(excluded.begin(), excluded.end(), idx) == excluded.end();
        };
        for (int attempt = 0; attempt < 64; ++attempt) {
            const int idx = std::min(run_.pop_size - 1, static_cast<int>(run_.uni(run_.rng) * run_.pop_size));
            if (allowed(idx)) {
                return idx;
            }
        }
        for (int idx = 0; idx < run_.pop_size; ++idx) {
            if (allowed(idx)) {
                return idx;
            }
        }
        return 0;
    }

PBestGuideSelection AdaptiveSession::no_pbest_guide() {
        return PBestGuideSelection{};
    }

PBestGuideSelection AdaptiveSession::population_pbest_guide(int index, PBestGuideSource source) {
        PBestGuideSelection guide;
        if (index >= 0 && index < run_.pop_size) {
            guide.has_guide = true;
            guide.population_index = index;
            guide.source = source;
            guide.guide_key = this->fitness_diversity_key(run_.fitness[static_cast<std::size_t>(index)]);
        }
        return guide;
    }

double AdaptiveSession::pbest_guide_value(const PBestGuideSelection& guide, int dimension_index) {
        if (guide.population_index >= 0) {
            return run_.population[static_cast<std::size_t>(guide.population_index) * run_.dim +
                              static_cast<std::size_t>(dimension_index)];
        }
        if (guide.scaled_values &&
            guide.scaled_values->size() == static_cast<std::size_t>(run_.dim)) {
            return (*guide.scaled_values)[static_cast<std::size_t>(dimension_index)];
        }
        return 0.0;
    }

PBestGuideSelection AdaptiveSession::select_pbest_guide(int i, const std::vector<int>& order, int pbest_count) {
        const std::string target_key = this->active_slot_uses_strict_frozen_family(i)
                                           ? this->active_slot_frozen_family_key_for_index(i)
                                           : this->fitness_diversity_key(
                                                 run_.fitness[static_cast<std::size_t>(i)]);
        const bool target_plateau_suppressed =
            this->active_lifecycle_mating_key_suppressed(target_key) &&
            !this->active_focus_key_matches_current(target_key);
        auto same_key_guide = [&]() {
            return this->population_pbest_guide(
                this->select_same_key_population_index(i, order, pbest_count),
                PBestGuideSource::SameKeyPopulation);
        };
        auto basin_guide = [&]() {
            if (this->active_archive_only_enabled()) {
                return this->no_pbest_guide();
            }
            return this->select_basin_archive_guide(i);
        };
        auto global_guide = [&]() {
            return this->population_pbest_guide(
                this->select_global_population_index(
                    order,
                    pbest_count,
                    this->active_plateau_lifecycle_enabled()),
                PBestGuideSource::GlobalPopulation);
        };
        if (target_plateau_suppressed) {
            if (this->active_slot_uses_strict_frozen_family(i)) {
                return this->mark_active_plateau_pbest_suppressed(this->no_pbest_guide(), false);
            }
            if (options.pbest_global_fallback) {
                PBestGuideSelection guide = global_guide();
                return this->mark_active_plateau_pbest_suppressed(
                    guide,
                    guide.has_guide && guide.source == PBestGuideSource::GlobalPopulation);
            }
            return this->mark_active_plateau_pbest_suppressed(this->no_pbest_guide(), false);
        }
        if (this->active_diversity_enabled() && this->is_active_basin_slot(i)) {
            return this->population_pbest_guide(i, PBestGuideSource::Self);
        }

        switch (options.pbest_scope) {
            case PBestGuideScope::None:
                return this->no_pbest_guide();
            case PBestGuideScope::Global:
                return global_guide();
            case PBestGuideScope::SameKey: {
                PBestGuideSelection guide = same_key_guide();
                return guide.has_guide ? guide : this->no_pbest_guide();
            }
            case PBestGuideScope::BasinLocal: {
                PBestGuideSelection guide = basin_guide();
                if (guide.has_guide) {
                    return guide;
                }
                guide = same_key_guide();
                return guide.has_guide ? guide : this->no_pbest_guide();
            }
            case PBestGuideScope::Mixed: {
                const double basin_rate =
                    this->active_archive_only_enabled()
                        ? 0.0
                        : std::max(0.0, options.pbest_basin_rate);
                const double same_rate = std::max(0.0, options.pbest_same_key_rate);
                const double global_rate = this->active_slot_uses_strict_frozen_family(i)
                                               ? 0.0
                                               : std::max(0.0, options.pbest_global_rate);
                const double total_rate = basin_rate + same_rate + global_rate;
                if (total_rate <= 0.0) {
                    return this->no_pbest_guide();
                }
                const double draw = run_.uni(run_.rng) * total_rate;
                PBestGuideSelection guide;
                if (draw < basin_rate) {
                    guide = basin_guide();
                    if (guide.has_guide) {
                        return guide;
                    }
                    guide = same_key_guide();
                    if (guide.has_guide) {
                        return guide;
                    }
                } else if (draw < basin_rate + same_rate) {
                    guide = same_key_guide();
                    if (guide.has_guide) {
                        return guide;
                    }
                    guide = basin_guide();
                    if (guide.has_guide) {
                        return guide;
                    }
                }
                if (options.pbest_global_fallback && global_rate > 0.0) {
                    guide = global_guide();
                    if (guide.has_guide) {
                        return guide;
                    }
                }
                return this->no_pbest_guide();
            }
            case PBestGuideScope::Legacy:
            default: {
                PBestGuideSelection same = same_key_guide();
                if (same.has_guide && run_.uni(run_.rng) < 0.70) {
                    return same;
                }
                if (options.pbest_global_fallback &&
                    !this->active_slot_uses_strict_frozen_family(i)) {
                    PBestGuideSelection global = global_guide();
                    if (global.has_guide) {
                        return global;
                    }
                }
                return same.has_guide ? same : this->no_pbest_guide();
            }
        }
    }

void AdaptiveSession::record_pbest_guide(PBestGuideStats& stats, int i, const PBestGuideSelection& guide) {
        switch (guide.source) {
            case PBestGuideSource::Self:
                ++stats.self;
                break;
            case PBestGuideSource::SameKeyPopulation:
                ++stats.same_key;
                break;
            case PBestGuideSource::BasinArchive:
                ++stats.basin;
                break;
            case PBestGuideSource::GlobalPopulation:
                ++stats.global;
                break;
            case PBestGuideSource::None:
            default:
                ++stats.none;
                break;
        }
        if (guide.active_plateau_suppressed) {
            ++stats.active_plateau_suppressed;
        }
        if (guide.active_plateau_global_rescue) {
            ++stats.active_plateau_global_rescue;
        }
        if (!guide.has_guide) {
            return;
        }
        const std::string parent_key = this->fitness_diversity_key(run_.fitness[static_cast<std::size_t>(i)]);
        if (parent_key.empty() || guide.guide_key.empty()) {
            ++stats.key_unknown;
        } else if (parent_key == guide.guide_key) {
            ++stats.key_match;
        } else {
            ++stats.key_mismatch;
        }
    }

PBestGuideSelection AdaptiveSession::select_local_population_pbest_guide(int i, const std::vector<int>& order, LocalMatingPools& pools, DeMatingScope scope) {
        const std::string key = this->select_local_mating_key(
            i,
            this->no_pbest_guide(),
            pools,
            scope);
        if (this->active_lifecycle_mating_key_suppressed(key)) {
            return this->no_pbest_guide();
        }
        const bool allow_similarity = scope != DeMatingScope::SameKey;
        std::vector<int> strict_candidates;
        const std::vector<int>* candidates_ptr = nullptr;
        if (this->active_slot_uses_strict_frozen_family(i)) {
            strict_candidates =
                this->strict_active_slot_population_pool(i, pools, allow_similarity);
            strict_candidates =
                this->population_pool_in_global_order(strict_candidates, order);
            candidates_ptr = &strict_candidates;
        } else {
            candidates_ptr =
                &this->local_sorted_population_pbest_pool(
                    pools,
                    key,
                    allow_similarity,
                    order);
        }
        const std::vector<int>& candidates = *candidates_ptr;
        if (static_cast<int>(candidates.size()) < std::max(4, options.de_mating_min_pool)) {
            return this->no_pbest_guide();
        }
        const int local_pbest_count = std::clamp(
            static_cast<int>(std::ceil(
                std::clamp(options.pbest_fraction, 0.01, 1.0) *
                static_cast<double>(candidates.size()))),
            1,
            static_cast<int>(candidates.size()));
        const int idx = std::min(local_pbest_count - 1,
                                 static_cast<int>(run_.uni(run_.rng) * local_pbest_count));
        const PBestGuideSource source =
            scope == DeMatingScope::SameKey
                ? PBestGuideSource::SameKeyPopulation
                : PBestGuideSource::BasinArchive;
        return this->population_pbest_guide(candidates[static_cast<std::size_t>(idx)], source);
    }

} } // namespace optiland::optimizer_detail
