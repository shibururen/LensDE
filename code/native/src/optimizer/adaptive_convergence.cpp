/*
 * Evaluate adaptive topology-discovery and LPSR readiness conditions for early stopping.
 * The epoch controller combines these queries with run_.basins and the configured improvement thresholds.
 * Keep minimum-improvement and horizon semantics aligned with the generation budget.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

int AdaptiveSession::basin_qualified_topology_count() {
        std::string mode = options.early_stop_qualified_topology_mode;
        std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char ch) {
            if (ch == '-' || std::isspace(ch)) {
                return '_';
            }
            return static_cast<char>(std::tolower(ch));
        });
        const bool merit_mode =
            mode == "merit" ||
            mode == "score" ||
            mode == "evaluation" ||
            mode == "evaluation_function" ||
            mode == "objective";
        const bool first_order_or_merit_mode =
            mode == "first_order_or_merit" ||
            mode == "fo_or_merit" ||
            mode == "either";
        const bool first_order_mode = !merit_mode && !first_order_or_merit_mode;
        const double first_order_epsilon =
            options.early_stop_qualified_topology_first_order_epsilon >= 0.0
                ? options.early_stop_qualified_topology_first_order_epsilon
                : options.first_order_epsilon;
        double best_merit = std::numeric_limits<double>::infinity();
        for (const auto& basin : run_.basins) {
            const double merit = basin.fitness.score.merit;
            if (std::isfinite(merit)) {
                best_merit = std::min(best_merit, merit);
            }
        }
        double merit_ceiling = std::numeric_limits<double>::infinity();
        if (std::isfinite(best_merit)) {
            const double ratio = options.early_stop_qualified_topology_merit_ratio;
            if (std::isfinite(ratio) && ratio > 0.0) {
                const double floor = options.early_stop_qualified_topology_merit_floor;
                const double ratio_ceiling = std::max(best_merit, best_merit * ratio);
                merit_ceiling = std::min(
                    merit_ceiling,
                    (std::isfinite(floor) && floor > 0.0)
                        ? std::max(ratio_ceiling, floor)
                        : ratio_ceiling);
            }
            const double delta = options.early_stop_qualified_topology_merit_delta;
            if (std::isfinite(delta) && delta > 0.0) {
                merit_ceiling = std::min(merit_ceiling, best_merit + delta);
            }
        }
        const double absolute_merit_max =
            options.early_stop_qualified_topology_merit_max;
        if (std::isfinite(absolute_merit_max) && absolute_merit_max > 0.0) {
            merit_ceiling = std::min(merit_ceiling, absolute_merit_max);
        }
        std::vector<std::string> keys;
        for (const auto& basin : run_.basins) {
            const bool first_order_near =
                std::isfinite(basin.fitness.first_order_violation) &&
                basin.fitness.first_order_violation <= first_order_epsilon;
            const bool first_order_ok =
                basin.fitness.first_order_feasible || first_order_near;
            const double merit = basin.fitness.score.merit;
            const bool merit_ok =
                std::isfinite(merit) &&
                (!std::isfinite(merit_ceiling) || merit <= merit_ceiling);
            bool qualified = first_order_ok;
            if (merit_mode) {
                qualified = merit_ok;
            } else if (first_order_or_merit_mode) {
                qualified = first_order_ok || merit_ok;
            } else if (!first_order_mode) {
                qualified = first_order_ok;
            }
            if (!qualified) {
                continue;
            }
            std::string key = this->basin_key(basin);
            if (key.empty()) {
                key = "__unknown__";
            }
            if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
                keys.push_back(std::move(key));
            }
        }
        return static_cast<int>(keys.size());
    }

double AdaptiveSession::early_stop_minimum_improvement(double previous) {
        const double min_abs = std::max(0.0, options.early_stop_min_abs_improve);
        const double min_rel = std::max(0.0, options.early_stop_min_rel_improve);
        if (!std::isfinite(previous)) {
            return min_abs;
        }
        const double relative_threshold = std::abs(previous) * min_rel;
        return options.early_stop_use_min_improvement_threshold
                   ? std::min(min_abs, relative_threshold)
                   : std::max(min_abs, relative_threshold);
    }

bool AdaptiveSession::early_stop_lpsr_ready() {
        if (!run_.variant_uses_lpsr) {
            return true;
        }
        const double min_progress =
            std::max(0.0, options.early_stop_min_lpsr_progress);
        if (min_progress > 0.0 &&
            this->lpsr_progress_fraction() + 1.0e-12 < min_progress) {
            return false;
        }
        return true;
    }

} } // namespace optiland::optimizer_detail
