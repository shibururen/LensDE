/*
 * Score hybrid scaled rows, update the incumbent and publish hybrid phase trace records.
 * DE, polish and refresh share the session population/merit vectors and best-score state.
 * Preserve row/merit alignment and incumbent tie handling before emitting progress or trace data.
 */
#include "hybrid_session.hpp"

namespace optiland { namespace optimizer_detail {

Score HybridSession::evaluate_score_for_scaled(const std::vector<double>& scaled) {
        const std::vector<double> values = scaled_to_physical(variables_, scaled, internal_mode);
        return evaluate_merit(
            prepare_for_merit(apply_mixed_variables(base_, variables_, values, glass_search_), merit_config_),
            merit_config_);
    }

std::pair<std::vector<double>, Score> HybridSession::update_best_from_population() {
        const auto rescore_start = Clock::now();
        const auto best_it = std::min_element(merits.begin(), merits.end());
        const int best_idx = static_cast<int>(std::distance(merits.begin(), best_it));
        std::vector<double> candidate_scaled = population_row(population, best_idx, dim);
        Score candidate_score = evaluate_score_for_scaled(candidate_scaled);
        merits[static_cast<std::size_t>(best_idx)] = candidate_score.merit;
        timing.best_rescore_seconds += seconds_since(rescore_start);
        return std::pair<std::vector<double>, Score>{
            std::move(candidate_scaled),
            std::move(candidate_score),
        };
    }

void HybridSession::emit_hybrid_trace(
        const std::string& event,
        int epoch,
        double phase_seconds,
        double epoch_start_merit,
        double before_merit,
        const Score& score,
        const std::vector<double>& scaled,
        int event_de_generations,
        int event_polish_runs,
        int event_polish_improvements,
        int event_refreshed_count,
        bool event_used_cuda,
        int event_polish_merit_strict_improvements,
        int event_polish_merit_neutral,
        int event_polish_merit_worse) {
        if (!options.trace) {
            return;
        }
        HybridTraceEvent trace;
        trace.event = event;
        trace.epoch = epoch;
        trace.population_size = pop_size;
        trace.variable_count = dim;
        trace.de_generations = total_de_generations;
        trace.epoch_de_generations = event_de_generations;
        trace.parent_count = parent_count;
        trace.refresh_count = refresh_count;
        trace.refreshed_count = event_refreshed_count;
        trace.refresh_effective_count = refresh_count;
        trace.phase_seconds = phase_seconds;
        trace.cumulative_de_seconds = de_seconds;
        trace.cumulative_polish_seconds = polish_seconds;
        trace.polish_runs = event_polish_runs;
        trace.polish_improvements = event_polish_improvements;
        trace.polish_merit_strict_improvements =
            event_polish_merit_strict_improvements;
        trace.polish_merit_neutral = event_polish_merit_neutral;
        trace.polish_merit_worse = event_polish_merit_worse;
        trace.epoch_start_merit = epoch_start_merit;
        trace.before_merit = before_merit;
        trace.best_score = score;
        trace.best_values = scaled_to_physical(variables_, scaled, internal_mode);
        trace.used_cuda = used_cuda || event_used_cuda;
        options.trace(trace);
    }

} }  // namespace optiland::optimizer_detail
