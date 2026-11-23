/*
 * Schedule adaptive glass-proximity tolerance updates and evaluate their readiness conditions.
 * Epoch and focus policies share run_.adaptive_glass_tolerance with the active merit configuration.
 * Keep trigger timing, focus-bridge conditions and tolerance changes synchronized with rescoring.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

bool AdaptiveSession::glass_proximity_enabled() {
        return !owner.merit_config_.glass_proximity.empty();
    }

double AdaptiveSession::current_glass_tolerance() {
        double current = std::numeric_limits<double>::quiet_NaN();
        for (const GlassProximityConstraint& constraint : owner.merit_config_.glass_proximity) {
            if (!std::isfinite(constraint.tolerance)) {
                continue;
            }
            if (!std::isfinite(current)) {
                current = constraint.tolerance;
            } else {
                current = std::max(current, constraint.tolerance);
            }
        }
        return current;
    }

std::string AdaptiveSession::canonical_adaptive_glass_tolerance_trigger_mode() {
        std::string mode = options.adaptive_glass_tolerance_trigger_mode;
        std::transform(
            mode.begin(),
            mode.end(),
            mode.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        for (char& c : mode) {
            if (c == '-' || std::isspace(static_cast<unsigned char>(c))) {
                c = '_';
            }
        }
        if (mode == "focus_clear" || mode == "focus_clears") {
            return std::string("focus_clear");
        }
        return std::string("warmup");
    }

bool AdaptiveSession::adaptive_glass_tolerance_update_trigger_ready(int outer) {
        const std::string mode =
            this->canonical_adaptive_glass_tolerance_trigger_mode();
        if (mode == "focus_clear") {
            return run_.active_focus_stop_clear_count >=
                   this->adaptive_glass_tolerance_focus_clear_target();
        }
        return outer >
               std::max(0, options.adaptive_glass_tolerance_warmup_epochs);
    }

bool AdaptiveSession::adaptive_glass_tolerance_bridge_active() {
        return run_.adaptive_glass_tolerance.enabled &&
               this->canonical_adaptive_glass_tolerance_trigger_mode() == "focus_clear" &&
               run_.active_focus_stop_clear_count >=
                   this->adaptive_glass_tolerance_focus_clear_target();
    }

void AdaptiveSession::sync_adaptive_glass_tolerance_trigger_state(int outer) {
        run_.adaptive_glass_tolerance.trigger_mode =
            this->canonical_adaptive_glass_tolerance_trigger_mode();
        run_.adaptive_glass_tolerance.focus_clear_count =
            run_.active_focus_stop_clear_count;
        run_.adaptive_glass_tolerance.focus_clear_target =
            run_.adaptive_glass_tolerance.trigger_mode == "focus_clear"
                ? this->adaptive_glass_tolerance_focus_clear_target()
                : 0;
        run_.adaptive_glass_tolerance.trigger_ready =
            this->adaptive_glass_tolerance_update_trigger_ready(outer);
        run_.adaptive_glass_tolerance.bridge_active =
            this->adaptive_glass_tolerance_bridge_active();
        run_.adaptive_glass_tolerance.focus_frozen =
            this->adaptive_glass_tolerance_focus_bridge_frozen();
        run_.adaptive_glass_tolerance.stop_when_ready =
            options.adaptive_glass_tolerance_stop_when_ready;
    }

double AdaptiveSession::glass_distance_for_scaled(const std::vector<double>& scaled) {
        if (scaled.size() != static_cast<std::size_t>(run_.dim) ||
            owner.merit_config_.glass_proximity.empty()) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        const std::vector<double> values =
            scaled_to_physical(owner.variables_, scaled, run_.internal_mode);
        LensSystem candidate_system =
            apply_mixed_variables(owner.base_, owner.variables_, values, owner.glass_search_);
        double max_distance = 0.0;
        bool any_distance = false;
        for (const GlassProximityConstraint& constraint : owner.merit_config_.glass_proximity) {
            if (constraint.points.empty() ||
                constraint.index >= candidate_system.surfaces.size()) {
                continue;
            }
            const Material& material =
                candidate_system.surfaces[constraint.index].material_after;
            if (!std::isfinite(material.nd) || !std::isfinite(material.vd)) {
                continue;
            }
            const double nd_scale =
                std::max(std::abs(constraint.nd_scale), 1.0e-12);
            const double vd_scale =
                std::max(std::abs(constraint.vd_scale), 1.0e-12);
            double nearest = std::numeric_limits<double>::infinity();
            for (const GlassProximityPoint& point : constraint.points) {
                if (!std::isfinite(point.nd) || !std::isfinite(point.vd)) {
                    continue;
                }
                const double dn = (material.nd - point.nd) / nd_scale;
                const double dv = (material.vd - point.vd) / vd_scale;
                nearest = std::min(nearest, std::sqrt(dn * dn + dv * dv));
            }
            if (std::isfinite(nearest)) {
                max_distance = std::max(max_distance, nearest);
                any_distance = true;
            }
        }
        return any_distance
                   ? max_distance
                   : std::numeric_limits<double>::quiet_NaN();
    }

double AdaptiveSession::glass_distance_metric(std::vector<double> distances) {
        distances.erase(
            std::remove_if(
                distances.begin(),
                distances.end(),
                [](double value) { return !std::isfinite(value); }),
            distances.end());
        if (distances.empty()) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        const double quantile = options.adaptive_glass_tolerance_quantile;
        if (std::isfinite(quantile) && quantile > 0.0 && quantile < 1.0) {
            std::sort(distances.begin(), distances.end());
            const double pos = quantile * static_cast<double>(distances.size() - 1U);
            const std::size_t lo = static_cast<std::size_t>(std::floor(pos));
            const std::size_t hi = static_cast<std::size_t>(std::ceil(pos));
            const double weight = pos - static_cast<double>(lo);
            return distances[lo] * (1.0 - weight) + distances[hi] * weight;
        }
        const double sum =
            std::accumulate(distances.begin(), distances.end(), 0.0);
        return sum / static_cast<double>(distances.size());
    }

bool AdaptiveSession::adaptive_glass_tolerance_ready() {
        if (!run_.adaptive_glass_tolerance.enabled) {
            return true;
        }
        if (!run_.adaptive_glass_tolerance.trigger_ready) {
            return false;
        }
        const double max_for_early_stop =
            options.adaptive_glass_tolerance_early_stop_max;
        if (!std::isfinite(max_for_early_stop) || max_for_early_stop <= 0.0) {
            return true;
        }
        return std::isfinite(run_.adaptive_glass_tolerance.current) &&
               run_.adaptive_glass_tolerance.current <= max_for_early_stop + 1.0e-12;
    }

bool AdaptiveSession::adaptive_glass_tolerance_stop_ready() {
        return run_.adaptive_glass_tolerance.enabled &&
               run_.adaptive_glass_tolerance.stop_when_ready &&
               run_.adaptive_glass_tolerance.bridge_active &&
               this->adaptive_glass_tolerance_ready();
    }

void AdaptiveSession::update_adaptive_glass_tolerance(int outer, int generation) {
        run_.adaptive_glass_tolerance.updated = false;
        run_.adaptive_glass_tolerance.sample_count = 0;
        run_.adaptive_glass_tolerance.elite_count = 0;
        run_.adaptive_glass_tolerance.previous = run_.adaptive_glass_tolerance.current;
        run_.adaptive_glass_tolerance.target = std::numeric_limits<double>::quiet_NaN();
        run_.adaptive_glass_tolerance.metric = std::numeric_limits<double>::quiet_NaN();
        this->sync_adaptive_glass_tolerance_trigger_state(outer);
        if (!run_.adaptive_glass_tolerance.enabled) {
            return;
        }
        run_.adaptive_glass_tolerance.current = this->current_glass_tolerance();
        run_.adaptive_glass_tolerance.previous = run_.adaptive_glass_tolerance.current;
        const auto order = sorted_fitness_indices(run_.fitness, options);
        if (order.empty()) {
            return;
        }
        const double elite_fraction = std::clamp(
            options.adaptive_glass_tolerance_elite_fraction,
            1.0 / static_cast<double>(std::max(1, run_.pop_size)),
            1.0);
        const int elite_count = std::clamp(
            static_cast<int>(std::ceil(elite_fraction * static_cast<double>(run_.pop_size))),
            1,
            run_.pop_size);
        run_.adaptive_glass_tolerance.elite_count = elite_count;
        std::vector<double> distances;
        distances.reserve(static_cast<std::size_t>(elite_count));
        for (int rank = 0; rank < elite_count && rank < static_cast<int>(order.size()); ++rank) {
            const int index = order[static_cast<std::size_t>(rank)];
            if (index < 0 || index >= run_.pop_size) {
                continue;
            }
            const double distance = this->glass_distance_for_scaled(
                population_row(run_.population, index, run_.dim));
            if (std::isfinite(distance)) {
                distances.push_back(distance);
            }
        }
        run_.adaptive_glass_tolerance.sample_count =
            static_cast<int>(distances.size());
        run_.adaptive_glass_tolerance.metric = this->glass_distance_metric(std::move(distances));
        if (!run_.adaptive_glass_tolerance.trigger_ready ||
            !std::isfinite(run_.adaptive_glass_tolerance.current) ||
            !std::isfinite(run_.adaptive_glass_tolerance.metric)) {
            return;
        }
        const double floor_value = std::max(
            0.0,
            std::isfinite(options.adaptive_glass_tolerance_floor)
                ? options.adaptive_glass_tolerance_floor
                : 0.05);
        const double margin = std::max(
            0.0,
            std::isfinite(options.adaptive_glass_tolerance_margin)
                ? options.adaptive_glass_tolerance_margin
                : 0.0);
        double target = std::max(floor_value, run_.adaptive_glass_tolerance.metric - margin);
        const double decay = std::clamp(
            std::isfinite(options.adaptive_glass_tolerance_decay)
                ? options.adaptive_glass_tolerance_decay
                : 1.0,
            0.0,
            1.0);
        if (decay > 0.0 && decay < 1.0) {
            target = std::min(
                target,
                std::max(floor_value, run_.adaptive_glass_tolerance.current * decay));
        }
        target = std::min(target, run_.adaptive_glass_tolerance.current);
        run_.adaptive_glass_tolerance.target = target;
        const double smoothing = std::clamp(
            std::isfinite(options.adaptive_glass_tolerance_smoothing)
                ? options.adaptive_glass_tolerance_smoothing
                : 1.0,
            0.0,
            1.0);
        const double next =
            run_.adaptive_glass_tolerance.current +
            smoothing * (target - run_.adaptive_glass_tolerance.current);
        const double clamped_next = std::max(floor_value, std::min(next, run_.adaptive_glass_tolerance.current));
        if (!(clamped_next < run_.adaptive_glass_tolerance.current - 1.0e-12)) {
            return;
        }
        for (GlassProximityConstraint& constraint : owner.merit_config_.glass_proximity) {
            if (std::isfinite(constraint.tolerance)) {
                constraint.tolerance = std::min(constraint.tolerance, clamped_next);
            }
        }
        run_.adaptive_glass_tolerance.current = this->current_glass_tolerance();
        run_.adaptive_glass_tolerance.updated = true;

        const std::vector<double> previous_best_scaled = run_.best_scaled;
        run_.fitness = this->evaluate_population_fitness(
            run_.population,
            run_.pop_size,
            generation,
            "adaptive_glass_tolerance");
        this->sync_merits();
        this->reset_best_from_population_and_incumbent(previous_best_scaled, generation);

        

        for (BasinElite& basin : run_.basins) {
            this->rescore_basin_fitness(basin);
        }
        for (TraceableRefreshArchiveEntry& entry : run_.traceable_refresh_archive) {
            this->rescore_cached_fitness(entry, "adaptive_glass_tolerance_refresh_archive");
        }
        for (TraceableRefreshArchiveEntry& entry : run_.pending_traceable_refresh_archive) {
            this->rescore_cached_fitness(entry, "adaptive_glass_tolerance_pending_refresh_archive");
        }
        for (TraceableRefreshArchiveEntry& entry : run_.initial_traceable_seed_pool) {
            this->rescore_cached_fitness(entry, "adaptive_glass_tolerance_initial_seed_pool");
        }
        run_.active_basin_lifecycle.clear();
        run_.active_niche_membership_reset_pending = true;
        run_.active_lifecycle_mating_stalled_cache.clear();
        run_.active_lifecycle_mating_suppressed_cache.clear();
    }

} } // namespace optiland::optimizer_detail
