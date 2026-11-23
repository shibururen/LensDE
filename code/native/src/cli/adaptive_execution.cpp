// Connect adaptive native progress to console output, checkpoints, and validation callbacks.
// OptimizationCommand keeps the latest validated boundary and timing state for final publication.
// Callback order and checkpoint reuse must stay aligned with the optimizer generation being reported.

#include "optimization_command.hpp"

namespace lensde { namespace cli { namespace detail {

int OptimizationCommand::adaptive_console() {
    std::cout << "CUDA evaluator available: "
              << (evaluate_population_cuda_available() ? "yes" : "no") << "\n";
    print_adaptive_population();
    print_adaptive_micro_islands();
    print_adaptive_memory();
    print_adaptive_constraints();
    print_adaptive_generation();
    print_adaptive_basins();
    print_adaptive_niches();
    print_adaptive_plateau();
    print_adaptive_termination();
    if (!initial_population_values.empty()) {
        std::cout << "Initial population seeds: "
                  << initial_population_values.size() << "\n";
    }
    if (checkpoint_evaluation_mode == CheckpointEvaluationMode::FixCandidate) {
        std::cout
            << "CPU checkpoint schedule: adaptive outer-epoch boundaries only; "
               "terminal boundary reused for final publication; exact duplicate "
               "values suppressed\n";
    } else {
        std::cout
            << "CPU checkpoint schedule: every "
            << adaptive_checkpoint_every
            << " cumulative generation(s) across "
            << expected_total_de_generations
            << " planned generation(s), plus generation 0 and terminal/early-stop boundaries; "
               "exact duplicate values suppressed\n";
    }
    return kContinueCommand;
}

int OptimizationCommand::adaptive_callbacks() {
    const std::vector<VariableSpec>& primary_variables = *primary_variables_source;
    if (!out_dir.empty()) {
        std::ofstream(join_path(out_dir, "adaptive_hybrid_trace.jsonl"), std::ios::trunc);
    }
    if (has_arg(argc, argv, "--family-transition-audit")) {
        if (!out_dir.empty()) {
            std::ofstream(
                join_path(out_dir, "family_transition_audit.jsonl"),
                std::ios::trunc);
        }
        adaptive_options.family_transition_audit =
            [&](const FamilyTransitionAuditEvent& event) {
                append_family_transition_audit_jsonl(out_dir, event);
            };
    }
    bool active_plateau_checkpoint_window = false;
    last_adaptive_persisted_values = checkpoint_evaluation_mode == CheckpointEvaluationMode::FixCandidate
                        ? initial_checkpoint_values
                        : std::vector<double>{};
    adaptive_options.trace = [&, active_plateau_checkpoint_window](const HybridTraceEvent& event) mutable {
        latest_checkpoint_trace_event = event;
        latest_checkpoint_trace_event_seen = true;
        append_trace_jsonl(out_dir, "adaptive_hybrid_trace.jsonl", event);
        update_checkpoint_adaptive_glass_state(
            latest_adaptive_glass_checkpoint_state,
            event);
        if (checkpoint_evaluation_mode != CheckpointEvaluationMode::FixCandidate &&
            event.adaptive_glass_tolerance_enabled &&
            event.adaptive_glass_tolerance_updated &&
            !event.best_values.empty()) {
            save_checkpoint(
                lens,
                variables,
                event.best_values,
                glass_search,
                merit,
                out_dir,
                event.event,
                event.de_generations,
                event.best_score,
                materialization_trace_contract,
                checkpoint_inherit_asphere_root_policy,
                &latest_adaptive_glass_checkpoint_state,
                &event,
                false,
                nullptr,
                nullptr,
                checkpoint_evaluation_mode,
                checkpoint_dependency_diagnostics);
        }
        // In the production candidate, a full CPU checkpoint is a
        // boundary operation.  The optimizer emits adaptive_epoch_end
        // after all DE/polish/refresh work for the outer epoch and
        // before its progress callback, so this is the single place
        // where an intermediate result is materialized and rescored.
        if (checkpoint_evaluation_mode == CheckpointEvaluationMode::FixCandidate &&
            event.event == "adaptive_epoch_end" &&
            !event.best_values.empty() &&
            event.best_values != last_adaptive_validated_values) {
            LensSystem validated_system;
            Score validated_score;
            const bool checkpoint_accepted = checkpoint_progress_with_variables(
                primary_variables,
                event.event,
                event.de_generations,
                event.best_score,
                event.best_values,
                false,
                nullptr,
                nullptr,
                &validated_system,
                &validated_score);
            if (checkpoint_accepted && score_is_materialization_valid(
                    validated_score, merit, materialization_trace_contract)) {
                last_adaptive_validated_system = std::move(validated_system);
                last_adaptive_validated_score = validated_score;
                last_adaptive_validated_values = event.best_values;
                last_adaptive_validated_generation = event.de_generations;
                last_adaptive_validation_valid = true;
            }
            last_adaptive_persisted_values = event.best_values;
        }
        if (event.event == "adaptive_plateau_retire_evict") {
            active_plateau_checkpoint_window = true;
        }
        const bool realtime_population_event =
            event.event == "adaptive_initial" ||
            event.event == "adaptive_active_reinject" ||
            event.event == "adaptive_de_end" ||
            event.event == "adaptive_polish_end" ||
            event.event == "adaptive_refresh_end" ||
            event.event == "adaptive_epoch_end" ||
            event.event == "adaptive_plateau_retire_evict" ||
            event.event == "adaptive_early_stop" ||
            event.event == "adaptive_early_stop_tail_end" ||
            event.event == "adaptive_early_stop_deferred_plateau_retire";
        const bool checkpoint_population_event =
            active_plateau_checkpoint_window &&
            (event.event == "adaptive_plateau_retire_evict" ||
             event.event == "adaptive_active_reinject" ||
             event.event == "adaptive_de_end" ||
             event.event == "adaptive_refresh_end" ||
             event.event == "adaptive_epoch_end" ||
             event.event == "adaptive_early_stop" ||
             event.event == "adaptive_early_stop_deferred_plateau_retire");
        if (realtime_population_event || checkpoint_population_event) {
            append_checkpoint_diagnostic_event(out_dir, "adaptive_hybrid", event);
        }
        if (active_plateau_checkpoint_window &&
            (event.event == "adaptive_epoch_end" ||
             event.event == "adaptive_early_stop" ||
             event.event == "adaptive_early_stop_tail_end")) {
            active_plateau_checkpoint_window = false;
        }
    };
    return kContinueCommand;
}

int OptimizationCommand::adaptive_execute() {
    const std::vector<VariableSpec>& primary_variables = *primary_variables_source;
    auto& optimizer = *optimizer_storage;
    int next_adaptive_checkpoint_generation = adaptive_checkpoint_every;
    result = optimizer.run_adaptive_hybrid_de_dls(
        adaptive_options,
        [&](int generation, const Score& best, const std::vector<double>& best_values) {
            std::cout << "  adaptive " << generation << " best merit "
                      << best.merit << " mean spot " << best.mean_spot << "\n";
            if (checkpoint_evaluation_mode == CheckpointEvaluationMode::FixCandidate) {
                // Full validation already ran at adaptive_epoch_end.
                // Generation cadence is intentionally irrelevant to
                // the production checkpoint policy.
                return;
            }
            const bool early_stop_boundary =
                latest_checkpoint_trace_event_seen &&
                latest_checkpoint_trace_event.de_generations == generation &&
                (latest_checkpoint_trace_event.event == "adaptive_early_stop" ||
                 latest_checkpoint_trace_event.event == "adaptive_early_stop_tail_end" ||
                 latest_checkpoint_trace_event.event ==
                     "adaptive_early_stop_deferred_plateau_retire");
            // The progress callback is delivered before the matching
            // adaptive_epoch_end trace event.  Generation count is the
            // authoritative terminal signal here; consulting the trace
            // would misclassify the planned G20/G400/G800 endpoint as
            // an ordinary periodic checkpoint.
            const bool planned_terminal_boundary =
                generation >= expected_total_de_generations;
            const bool terminal_boundary =
                early_stop_boundary || planned_terminal_boundary;
            const bool periodic_checkpoint_due =
                generation >= next_adaptive_checkpoint_generation;
            if (periodic_checkpoint_due) {
                const long long skipped_intervals =
                    (static_cast<long long>(generation) -
                     next_adaptive_checkpoint_generation) /
                        adaptive_checkpoint_every +
                    1;
                const long long advanced_generation =
                    static_cast<long long>(next_adaptive_checkpoint_generation) +
                    skipped_intervals * adaptive_checkpoint_every;
                next_adaptive_checkpoint_generation = static_cast<int>(
                    std::min<long long>(
                        advanced_generation,
                        std::numeric_limits<int>::max()));
            }
            const bool checkpoint_due =
                generation == 0 || terminal_boundary || periodic_checkpoint_due;
            if (checkpoint_due) {
                const bool middle_periodic_checkpoint =
                    periodic_checkpoint_due && generation > 0 && !terminal_boundary;
                const bool persist_only =
                    (checkpoint_evaluation_mode ==
                         CheckpointEvaluationMode::PersistOnly &&
                     middle_periodic_checkpoint) ||
                    (checkpoint_evaluation_mode ==
                         CheckpointEvaluationMode::FixCandidate &&
                     (generation == 0 || terminal_boundary));
                const bool skip_checkpoint =
                    checkpoint_evaluation_mode ==
                        CheckpointEvaluationMode::NoMid &&
                    middle_periodic_checkpoint;
                const bool duplicate_persisted_values =
                    !last_adaptive_persisted_values.empty() &&
                    best_values == last_adaptive_persisted_values;
                const bool duplicate_validated_values =
                    !last_adaptive_validated_values.empty() &&
                    best_values == last_adaptive_validated_values;
                if (persist_only && !duplicate_persisted_values) {
                    persist_unverified_checkpoint_state(
                        lens,
                        primary_variables,
                        best_values,
                        glass_search,
                        out_dir,
                        "adaptive_hybrid",
                        generation,
                        best,
                    checkpoint_evaluation_mode,
                    checkpoint_dependency_diagnostics);
                    last_adaptive_persisted_values = best_values;
                } else if (
                    !persist_only && !skip_checkpoint &&
                    !duplicate_validated_values) {
                    checkpoint_progress(
                        "adaptive_hybrid", generation, best, best_values);
                    last_adaptive_persisted_values = best_values;
                    last_adaptive_validated_values = best_values;
                }
            }
        });
    de_used_cuda = result.used_cuda;
    return kContinueCommand;
}

} } }
