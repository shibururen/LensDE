/*
 * Choose basin-archive guides for adaptive mutation and compare their diversity identities.
 * Guide selection reads the current population, fitness and basin representatives in the same coordinates.
 * Preserve archive traversal and guide tie breaks; selected guides affect subsequent trial generation.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

PBestGuideSelection AdaptiveSession::select_basin_archive_guide(int i) {
        PBestGuideSelection guide;
        const std::vector<int> candidate_basin_indices = this->active_basin_candidate_indices();
        if (candidate_basin_indices.empty()) {
            return guide;
        }
        const std::vector<double> target_row = population_row(run_.population, i, run_.dim);
        if (target_row.size() != static_cast<std::size_t>(run_.dim)) {
            return guide;
        }
        const std::string parent_key = this->fitness_diversity_key(run_.fitness[static_cast<std::size_t>(i)]);
        int best_same_key = -1;
        int best_nearest = -1;
        double best_same_key_distance = std::numeric_limits<double>::infinity();
        double best_nearest_distance = std::numeric_limits<double>::infinity();
        auto basin_distance_to_target = [&](const BasinElite& basin) {
            if (basin.scaled_values.size() != static_cast<std::size_t>(run_.dim)) {
                return std::numeric_limits<double>::infinity();
            }
            return scaled_vector_distance(
                owner.variables_,
                target_row,
                basin.scaled_values,
                options.basins.distance_metric,
                run_.internal_mode,
                &owner.base_,
                options.basins.air_thickness_distance_cap);
        };
        for (int b : candidate_basin_indices) {
            const BasinElite& basin = run_.basins[static_cast<std::size_t>(b)];
            if (this->active_lifecycle_mating_key_suppressed(
                    this->active_lifecycle_key_for_basin(basin)) &&
                !this->active_focus_key_matches_current(
                    this->active_lifecycle_key_for_basin(basin))) {
                continue;
            }
            const double distance = basin_distance_to_target(basin);
            if (!std::isfinite(distance)) {
                continue;
            }
            if (distance < best_nearest_distance ||
                (std::abs(distance - best_nearest_distance) <= 1.0e-12 &&
                 best_nearest >= 0 &&
                 candidate_better(
                     basin.fitness,
                     run_.basins[static_cast<std::size_t>(best_nearest)].fitness,
                     options))) {
                best_nearest_distance = distance;
                best_nearest = b;
            }
            bool same_parent_family = false;
            if (!parent_key.empty()) {
                if (run_.topology_similarity_enabled) {
                    same_parent_family = this->active_topology_keys_are_similar(
                        this->active_current_family_key_for_basin(basin),
                        parent_key);
                } else {
                    same_parent_family = this->basin_diversity_key(basin) == parent_key;
                }
            }
            if (same_parent_family) {
                if (distance < best_same_key_distance ||
                    (std::abs(distance - best_same_key_distance) <= 1.0e-12 &&
                     best_same_key >= 0 &&
                     candidate_better(
                         basin.fitness,
                         run_.basins[static_cast<std::size_t>(best_same_key)].fitness,
                         options))) {
                    best_same_key_distance = distance;
                    best_same_key = b;
                }
            }
        }
        const int selected = best_same_key >= 0
                                 ? best_same_key
                                 : (options.pbest_basin_nearest_fallback ? best_nearest : -1);
        if (selected < 0) {
            return guide;
        }
        const BasinElite& basin = run_.basins[static_cast<std::size_t>(selected)];
        guide.has_guide = true;
        guide.population_index = -1;
        guide.scaled_values = &basin.scaled_values;
        guide.source = PBestGuideSource::BasinArchive;
        guide.guide_key = this->basin_diversity_key(basin);
        return guide;
    }



bool AdaptiveSession::diversity_keys_match(const std::string& candidate_key, const std::string& target_key, bool allow_similarity) {
        if (candidate_key.empty() || target_key.empty()) {
            return false;
        }
        if (candidate_key == target_key) {
            return true;
        }
        return allow_similarity && run_.topology_similarity_enabled &&
               this->active_topology_keys_are_similar(candidate_key, target_key);
    }

} } // namespace optiland::optimizer_detail
