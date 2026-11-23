/*
 * Assemble and publish one adaptive trace record from the completed phase state.
 * The session delegates record fields to trace-assembly units and sends the result to the caller callback.
 * Keep callback timing and record assembly order aligned with the observed optimizer boundary.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

optiland::HybridTraceEvent AdaptiveSession::emit_trace(const std::string& event, int epoch, double phase_seconds, double epoch_start_merit, double before_merit, int event_de_generations, int event_polish_runs, int event_polish_improvements, int event_refreshed_count, int event_accepted_trials, bool event_used_cuda, int event_polish_merit_strict_improvements, int event_polish_merit_neutral, int event_polish_merit_worse, int event_refresh_archive_used, int event_refresh_effective_count, bool event_refresh_adaptive_skipped, double event_refresh_acceptance_rate, double event_refresh_rel_improvement, int event_micro_island_runs, int event_micro_island_improvements, int event_micro_island_archived, int event_micro_island_seed_count, double event_micro_island_seconds, double event_micro_island_de_seconds, double event_micro_island_polish_seconds, int event_micro_island_early_stopped, int event_micro_island_retired, const std::vector<MicroIslandTraceRecord>* event_micro_island_records) {
        if (run_.descriptor_full_enabled &&
            options.basins.max_basins > 0 &&
            static_cast<int>(run_.basins.size()) > options.basins.max_basins) {
            this->trim_basins_to_limit(options.basins.max_basins);
        }
        int feasible_count = 0;
        int trace_feasible_count = 0;
        int constraint_feasible_count = 0;
        int first_order_feasible_count = 0;
        this->count_trace_feasibility(feasible_count, trace_feasible_count, constraint_feasible_count, first_order_feasible_count);
        HybridTraceEvent trace;
        this->assemble_trace_identity(trace, event, epoch, event_de_generations);
        this->assemble_trace_refresh(trace, event_refreshed_count, event_accepted_trials, event_refresh_archive_used, event_refresh_effective_count, event_refresh_adaptive_skipped, event_refresh_acceptance_rate, event_refresh_rel_improvement);
        this->assemble_trace_constraint_guards(trace);
        this->assemble_trace_active_basins(trace);
        this->assemble_trace_active_mating_quota(trace);
        this->assemble_trace_active_niches(trace);
        this->assemble_trace_active_focus(trace);
        this->assemble_trace_local_cohorts(trace);
        this->assemble_trace_active_plateau(trace);
        this->assemble_trace_descriptors(trace);
        this->assemble_trace_population_merits(trace, epoch, feasible_count, trace_feasible_count, constraint_feasible_count, first_order_feasible_count);
        this->assemble_trace_glass_tolerance(trace);
        this->assemble_trace_basin_merits(trace);
        this->assemble_trace_mutation_controls(trace);
        this->assemble_trace_geometry_memory(trace);
        this->assemble_trace_mating(trace);
        this->assemble_trace_best_metrics(trace);
        this->assemble_trace_cumulative_timing(trace, phase_seconds);
        this->assemble_trace_polish(trace, event_polish_runs, event_polish_improvements, event_polish_merit_strict_improvements, event_polish_merit_neutral, event_polish_merit_worse);
        this->assemble_trace_micro_islands(trace, event_micro_island_runs, event_micro_island_improvements, event_micro_island_archived, event_micro_island_seed_count, event_micro_island_seconds, event_micro_island_de_seconds, event_micro_island_polish_seconds, event_micro_island_early_stopped, event_micro_island_retired, event_micro_island_records);
        this->assemble_trace_detailed_timing(trace);
        this->assemble_trace_terminal_state(trace, epoch_start_merit, before_merit, event_used_cuda);
        if (options.trace) {
            options.trace(trace);
        }
        return trace;
    }

} } // namespace optiland::optimizer_detail
