/*
 * Normalize convergence-metric names and extract their values from adaptive trace records.
 * Initialization and epoch stopping use the same metric interpretation and incumbent-merit convention.
 * Preserve token aliases and missing/nonfinite-value handling when extending diagnostic metrics.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

std::string AdaptiveSession::normalize_metric_token(std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
            if (ch == '-' || std::isspace(ch)) {
                return '_';
            }
            return static_cast<char>(std::tolower(ch));
        });
        return text;
    }

double AdaptiveSession::trace_metric_value(const HybridTraceEvent& trace) {
        if (run_.early_stop_metric == "population_elite_mean_merit" ||
            run_.early_stop_metric == "population_mean" ||
            run_.early_stop_metric == "elite_mean") {
            return trace.population_elite_mean_merit;
        }
        if (run_.early_stop_metric == "population_elite_median_merit" ||
            run_.early_stop_metric == "population_median" ||
            run_.early_stop_metric == "elite_median") {
            return trace.population_elite_median_merit;
        }
        if (run_.early_stop_metric == "basin_elite_mean_merit" ||
            run_.early_stop_metric == "basin_mean") {
            return trace.basin_elite_mean_merit;
        }
        if (run_.early_stop_metric == "basin_elite_median_merit" ||
            run_.early_stop_metric == "basin_median") {
            return trace.basin_elite_median_merit;
        }
        if (run_.early_stop_metric == "basin_unique_topology_elite_median_merit" ||
            run_.early_stop_metric == "unique_topology_median") {
            return trace.basin_unique_topology_elite_median_merit;
        }
        if (run_.early_stop_best_merit) {
            return trace.best_score.merit;
        }
        return trace.basin_unique_topology_elite_mean_merit;
    }

} } // namespace optiland::optimizer_detail
