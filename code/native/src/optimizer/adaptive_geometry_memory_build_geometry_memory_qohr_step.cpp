/*
 * Build geometry-memory QOHR guidance and soften pbest choices near remembered structures.
 * Trial generation shares merit-ranked population/history data and memory policy from run_.
 * Preserve history filtering and coordinate accumulation order before mutation consumes the guide.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

bool AdaptiveSession::build_geometry_memory_qohr_step(int target_index, const PBestGuideSelection& pbest_guide, int r0, int r1, int r2_pool, double f, std::vector<double>& step, PBestGuideStats& stats) {
        if (!options.geometry_memory_qohr_mutation ||
            !run_.geometry_memory_enabled ||
            !run_.variant_uses_jade ||
            target_index < 0 || target_index >= run_.pop_size ||
            r0 < 0 || r0 >= run_.pop_size ||
            r1 < 0 || r1 >= run_.pop_size ||
            run_.geometry_memory_qohr_dimensions.size() < 2) {
            return false;
        }
        const auto qohr_start = Clock::now();
        ++stats.geometry_memory_qohr_checked;
        const double quality_full_ratio = std::max(
            1.0,
            options.geometry_memory_qohr_quality_full_ratio);
        const double quality_off_ratio = std::max(
            quality_full_ratio + 1.0e-12,
            options.geometry_memory_qohr_quality_off_ratio);
        auto quality_weight = [&](double merit) {
            if (!std::isfinite(merit) || merit <= 0.0 ||
                !std::isfinite(run_.geometry_memory_qohr_reference_merit) ||
                run_.geometry_memory_qohr_reference_merit <= 0.0) {
                return 0.0;
            }
            const double ratio = merit / run_.geometry_memory_qohr_reference_merit;
            const double linear = std::clamp(
                (quality_off_ratio - ratio) /
                    (quality_off_ratio - quality_full_ratio),
                0.0,
                1.0);
            return linear * linear * (3.0 - 2.0 * linear);
        };
        const double target_merit = run_.fitness[
            static_cast<std::size_t>(target_index)].score.merit;
        if (!(quality_weight(target_merit) > 0.0)) {
            ++stats.geometry_memory_qohr_quality_gated;
            stats.geometry_memory_qohr_seconds += seconds_since(qohr_start);
            return false;
        }
        std::fill(step.begin(), step.end(), 0.0);
        std::fill(
            run_.geometry_memory_qohr_history.begin(),
            run_.geometry_memory_qohr_history.end(),
            0.0);
        const double* target = run_.population.data() +
            static_cast<std::size_t>(target_index) * run_.dim;
        const double* random_one = run_.population.data() +
            static_cast<std::size_t>(r1) * run_.dim;
        const double* random_two = nullptr;
        if (run_.variant_uses_archive && r2_pool >= run_.pop_size &&
            !run_.external_archive.empty()) {
            const int archive_index = r2_pool - run_.pop_size;
            if (archive_index >= 0 &&
                archive_index < static_cast<int>(run_.external_archive.size())) {
                random_two = run_.external_archive[
                    static_cast<std::size_t>(archive_index)].data();
            }
        }
        if (random_two == nullptr) {
            const int r2 = std::clamp(r2_pool, 0, run_.pop_size - 1);
            random_two = run_.population.data() + static_cast<std::size_t>(r2) * run_.dim;
        }

        double quality_squared_norm = 0.0;
        for (int j : run_.geometry_memory_qohr_dimensions) {
            const double range =
                run_.geometry_memory_qohr_ranges[static_cast<std::size_t>(j)];
            const double guide = pbest_guide.has_guide
                                     ? this->pbest_guide_value(pbest_guide, j)
                                     : run_.population[static_cast<std::size_t>(r0) * run_.dim + j];
            const double quality = (guide - target[j]) / range;
            const double random_difference =
                f * (random_one[j] - random_two[j]) / range;
            run_.geometry_memory_qohr_g[static_cast<std::size_t>(j)] = quality;
            run_.geometry_memory_qohr_e[static_cast<std::size_t>(j)] =
                random_difference;
            quality_squared_norm += quality * quality;
        }
        const double quality_norm = std::sqrt(quality_squared_norm);
        if (!(quality_norm > 1.0e-14)) {
            ++stats.geometry_memory_qohr_degenerate;
            stats.geometry_memory_qohr_seconds += seconds_since(qohr_start);
            return false;
        }

        double random_parallel = 0.0;
        for (int j : run_.geometry_memory_qohr_dimensions) {
            random_parallel +=
                run_.geometry_memory_qohr_g[static_cast<std::size_t>(j)] /
                quality_norm *
                run_.geometry_memory_qohr_e[static_cast<std::size_t>(j)];
        }
        double perpendicular_squared_norm = 0.0;
        for (int j : run_.geometry_memory_qohr_dimensions) {
            const double quality_unit =
                run_.geometry_memory_qohr_g[static_cast<std::size_t>(j)] /
                quality_norm;
            const double perpendicular =
                run_.geometry_memory_qohr_e[static_cast<std::size_t>(j)] -
                random_parallel * quality_unit;
            run_.geometry_memory_qohr_e_perp[static_cast<std::size_t>(j)] =
                perpendicular;
            perpendicular_squared_norm += perpendicular * perpendicular;
        }
        const double perpendicular_norm =
            std::sqrt(perpendicular_squared_norm);
        if (!(perpendicular_norm > 1.0e-14)) {
            ++stats.geometry_memory_qohr_degenerate;
            stats.geometry_memory_qohr_seconds += seconds_since(qohr_start);
            return false;
        }

        const std::string& topology_key =
            run_.fitness[static_cast<std::size_t>(target_index)].topology_key;
        const std::vector<double>* family_similarities =
            this->geometry_memory_qohr_family_similarities(topology_key);
        const double family_threshold =
            options.geometry_memory_qohr_family_similarity_threshold;
        double total_weight = 0.0;
        double pressure = 0.0;
        double maximum_quality_weight = 0.0;
        bool family_weighted = false;
        for (std::size_t memory_index = 0;
             memory_index < run_.geometry_memory_scaled.size();
             ++memory_index) {
            const auto& memory = run_.geometry_memory_scaled[memory_index];
            const double memory_quality_weight = quality_weight(std::max(
                target_merit,
                options.geometry_memory_merits[memory_index]));
            if (!(memory_quality_weight > 0.0)) {
                continue;
            }
            maximum_quality_weight = std::max(
                maximum_quality_weight,
                memory_quality_weight);
            const double effective =
                this->geometry_memory_effective_distance(target, memory);
            if (!std::isfinite(effective)) {
                continue;
            }
            const double inverse = 1.0 / (1.0 + effective * effective);
            const double geometry_weight = inverse * inverse;
            double family_signal = 0.0;
            if (family_similarities != nullptr) {
                const double similarity = (*family_similarities)[memory_index];
                if (family_threshold >= 1.0 - 1.0e-12) {
                    family_signal = similarity >= 1.0 - 1.0e-12 ? 1.0 : 0.0;
                } else {
                    // Start turning before the contract boundary. The requested
                    // threshold is the half-pressure point and exact equality is
                    // full pressure; for t=0.90 the linear ramp is [0.80, 1.00].
                    const double family_onset =
                        std::max(0.0, 2.0 * family_threshold - 1.0);
                    family_signal = std::clamp(
                        (similarity - family_onset) /
                            std::max(1.0 - family_onset, 1.0e-12),
                        0.0,
                        1.0);
                }
            }
            const double unweighted_family_pressure = std::clamp(
                options.geometry_memory_qohr_family_weight * family_signal,
                0.0,
                1.0);
            const double family_pressure =
                memory_quality_weight * unweighted_family_pressure;
            if (family_pressure > 0.0) {
                family_weighted = true;
            }
            pressure = std::max(
                pressure,
                memory_quality_weight *
                    (geometry_weight +
                     (1.0 - geometry_weight) * unweighted_family_pressure));
            const double weight = memory_quality_weight * geometry_weight +
                family_pressure;
            double history_squared_norm = 0.0;
            for (int j : run_.geometry_memory_qohr_dimensions) {
                const double range =
                    run_.geometry_memory_qohr_ranges[static_cast<std::size_t>(j)];
                const double delta =
                    (target[j] - memory[static_cast<std::size_t>(j)]) / range;
                history_squared_norm += delta * delta;
            }
            const double history_norm = std::sqrt(history_squared_norm);
            if (!(history_norm > 1.0e-14) || !(weight > 0.0)) {
                continue;
            }
            for (int j : run_.geometry_memory_qohr_dimensions) {
                const double range =
                    run_.geometry_memory_qohr_ranges[static_cast<std::size_t>(j)];
                run_.geometry_memory_qohr_history[static_cast<std::size_t>(j)] +=
                    weight *
                    (target[j] - memory[static_cast<std::size_t>(j)]) /
                    (range * history_norm);
            }
            total_weight += weight;
        }
        double history_squared_norm = 0.0;
        if (total_weight > 0.0) {
            for (int j : run_.geometry_memory_qohr_dimensions) {
                const double history =
                    run_.geometry_memory_qohr_history[static_cast<std::size_t>(j)] /
                    total_weight;
                run_.geometry_memory_qohr_history[static_cast<std::size_t>(j)] = history;
                history_squared_norm += history * history;
            }
        }
        const double history_norm = std::sqrt(history_squared_norm);
        if (!(history_norm > 1.0e-14) || !(pressure > 0.0)) {
            if (!(maximum_quality_weight > 0.0)) {
                ++stats.geometry_memory_qohr_quality_gated;
            } else {
                ++stats.geometry_memory_qohr_degenerate;
            }
            stats.geometry_memory_qohr_seconds += seconds_since(qohr_start);
            return false;
        }

        double history_quality_dot = 0.0;
        for (int j : run_.geometry_memory_qohr_dimensions) {
            const double history_unit =
                run_.geometry_memory_qohr_history[static_cast<std::size_t>(j)] /
                history_norm;
            run_.geometry_memory_qohr_history[static_cast<std::size_t>(j)] =
                history_unit;
            history_quality_dot += history_unit *
                run_.geometry_memory_qohr_g[static_cast<std::size_t>(j)] /
                quality_norm;
        }
        double orthogonal_history_squared_norm = 0.0;
        for (int j : run_.geometry_memory_qohr_dimensions) {
            const double quality_unit =
                run_.geometry_memory_qohr_g[static_cast<std::size_t>(j)] /
                quality_norm;
            const double orthogonal_history =
                run_.geometry_memory_qohr_history[static_cast<std::size_t>(j)] -
                history_quality_dot * quality_unit;
            run_.geometry_memory_qohr_direction[static_cast<std::size_t>(j)] =
                orthogonal_history;
            orthogonal_history_squared_norm +=
                orthogonal_history * orthogonal_history;
        }
        const double orthogonal_history_norm =
            std::sqrt(orthogonal_history_squared_norm);
        if (!(orthogonal_history_norm > 1.0e-14)) {
            ++stats.geometry_memory_qohr_degenerate;
            stats.geometry_memory_qohr_seconds += seconds_since(qohr_start);
            return false;
        }

        const double rotation = std::clamp(
            options.geometry_memory_qohr_max_rotation * pressure,
            0.0,
            1.0);
        double blended_squared_norm = 0.0;
        for (int j : run_.geometry_memory_qohr_dimensions) {
            const double orthogonal_history_unit =
                run_.geometry_memory_qohr_direction[static_cast<std::size_t>(j)] /
                orthogonal_history_norm;
            run_.geometry_memory_qohr_history[static_cast<std::size_t>(j)] =
                orthogonal_history_unit;
            const double blended =
                (1.0 - rotation) *
                    run_.geometry_memory_qohr_e_perp[static_cast<std::size_t>(j)] /
                    perpendicular_norm +
                rotation * orthogonal_history_unit;
            run_.geometry_memory_qohr_direction[static_cast<std::size_t>(j)] = blended;
            blended_squared_norm += blended * blended;
        }
        const double blended_norm = std::sqrt(blended_squared_norm);
        if (!(blended_norm > 1.0e-14)) {
            ++stats.geometry_memory_qohr_degenerate;
            stats.geometry_memory_qohr_seconds += seconds_since(qohr_start);
            return false;
        }

        const double minimum_parallel =
            -std::clamp(
                options.geometry_memory_qohr_backtrack_fraction,
                0.0,
                1.0) *
            f * quality_norm;
        const double clipped_parallel =
            std::max(random_parallel, minimum_parallel);
        double base_squared_norm = 0.0;
        double rotated_squared_norm = 0.0;
        double history_projection_before = 0.0;
        double history_projection_after = 0.0;
        for (int j : run_.geometry_memory_qohr_dimensions) {
            const double quality_unit =
                run_.geometry_memory_qohr_g[static_cast<std::size_t>(j)] /
                quality_norm;
            const double base_step =
                f * run_.geometry_memory_qohr_g[static_cast<std::size_t>(j)] +
                run_.geometry_memory_qohr_e[static_cast<std::size_t>(j)];
            const double rotated_step =
                f * run_.geometry_memory_qohr_g[static_cast<std::size_t>(j)] +
                clipped_parallel * quality_unit +
                perpendicular_norm *
                    run_.geometry_memory_qohr_direction[static_cast<std::size_t>(j)] /
                    blended_norm;
            step[static_cast<std::size_t>(j)] = rotated_step;
            base_squared_norm += base_step * base_step;
            rotated_squared_norm += rotated_step * rotated_step;
            const double history_unit =
                run_.geometry_memory_qohr_history[static_cast<std::size_t>(j)];
            history_projection_before += base_step * history_unit;
            history_projection_after += rotated_step * history_unit;
        }
        ++stats.geometry_memory_qohr_applied;
        stats.geometry_memory_qohr_quality_weight_sum +=
            maximum_quality_weight;
        if (family_weighted) {
            ++stats.geometry_memory_qohr_family_weighted;
        }
        stats.geometry_memory_qohr_rotation_sum += rotation;
        stats.geometry_memory_qohr_quality_projection_before_sum +=
            f * quality_norm + random_parallel;
        stats.geometry_memory_qohr_quality_projection_after_sum +=
            f * quality_norm + clipped_parallel;
        stats.geometry_memory_qohr_history_projection_before_sum +=
            history_projection_before;
        stats.geometry_memory_qohr_history_projection_after_sum +=
            history_projection_after;
        if (base_squared_norm > 1.0e-28) {
            stats.geometry_memory_qohr_step_norm_ratio_sum +=
                std::sqrt(rotated_squared_norm / base_squared_norm);
        } else {
            stats.geometry_memory_qohr_step_norm_ratio_sum += 1.0;
        }
        stats.geometry_memory_qohr_seconds += seconds_since(qohr_start);
        return true;
    }

bool AdaptiveSession::geometry_memory_guide_is_near(const PBestGuideSelection& guide, PBestGuideStats& stats) {
        if (!options.geometry_memory_pbest_soft ||
            !run_.geometry_memory_enabled || !guide.has_guide ||
            guide.source == PBestGuideSource::Self) {
            return false;
        }
        const double* scaled = nullptr;
        if (guide.population_index >= 0 && guide.population_index < run_.pop_size) {
            if (run_.geometry_memory_population_near_cache.size() ==
                    static_cast<std::size_t>(run_.pop_size) &&
                run_.geometry_memory_population_near_cache[
                    static_cast<std::size_t>(guide.population_index)] >= 0) {
                return run_.geometry_memory_population_near_cache[
                    static_cast<std::size_t>(guide.population_index)] != 0;
            }
            scaled = run_.population.data() +
                static_cast<std::size_t>(guide.population_index) * run_.dim;
        } else if (guide.scaled_values &&
                   guide.scaled_values->size() == static_cast<std::size_t>(run_.dim)) {
            scaled = guide.scaled_values->data();
        }
        if (scaled == nullptr) {
            return false;
        }
        const auto distance_start = Clock::now();
        const bool memory_near = this->geometry_memory_min_effective_distance(scaled) < 1.0;
        stats.geometry_memory_distance_seconds += seconds_since(distance_start);
        ++stats.geometry_memory_checked;
        if (guide.population_index >= 0 && guide.population_index < run_.pop_size &&
            run_.geometry_memory_population_near_cache.size() ==
                static_cast<std::size_t>(run_.pop_size)) {
            run_.geometry_memory_population_near_cache[
                static_cast<std::size_t>(guide.population_index)] =
                memory_near ? 1 : 0;
        }
        return memory_near;
    }

PBestGuideSelection AdaptiveSession::apply_geometry_memory_pbest_soft(const PBestGuideSelection& original, const std::vector<int>& order, int pbest_count, PBestGuideStats& stats) {
        if (!this->geometry_memory_guide_is_near(original, stats)) {
            return original;
        }
        ++stats.geometry_memory_hit;
        if (order.empty()) {
            ++stats.geometry_memory_rescue;
            return original;
        }
        const int limit = std::clamp(pbest_count, 1, static_cast<int>(order.size()));
        std::vector<int> alternatives;
        alternatives.reserve(static_cast<std::size_t>(limit));
        for (int rank = 0; rank < limit; ++rank) {
            const int candidate = order[static_cast<std::size_t>(rank)];
            if (candidate < 0 || candidate >= run_.pop_size ||
                candidate == original.population_index) {
                continue;
            }
            const PBestGuideSelection alternative =
                this->population_pbest_guide(candidate, PBestGuideSource::GlobalPopulation);
            if (!this->geometry_memory_guide_is_near(alternative, stats)) {
                alternatives.push_back(candidate);
            }
        }
        if (alternatives.empty()) {
            ++stats.geometry_memory_rescue;
            return original;
        }
        const int selected = std::min(
            static_cast<int>(alternatives.size()) - 1,
            static_cast<int>(run_.uni(run_.rng) * alternatives.size()));
        ++stats.geometry_memory_redirect;
        return this->population_pbest_guide(
            alternatives[static_cast<std::size_t>(selected)],
            PBestGuideSource::GlobalPopulation);
    }

} } // namespace optiland::optimizer_detail
