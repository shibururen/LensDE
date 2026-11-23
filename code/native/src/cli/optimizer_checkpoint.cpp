// Configure the publication trace contract and forward optimizer progress into checkpoint saving.
// OptimizationCommand seeds a validated initial fallback before the optimizer changes geometry.
// Keep local/adaptive near-trace rules and prevalidated-system reuse associated with the right boundary.

#include "optimization_command.hpp"

namespace lensde { namespace cli { namespace detail {

int OptimizationCommand::configure_checkpoint() {
    checkpoint_inherit_asphere_root_policy = has_arg(argc, argv, "--checkpoint-inherit-asphere-root-policy");
    const std::string checkpoint_evaluation_mode_arg = normalized_option_token(
        string_arg(argc, argv, "--checkpoint-evaluation-mode", "fix_candidate"));
    checkpoint_evaluation_mode = CheckpointEvaluationMode::FixCandidate;
    if (checkpoint_evaluation_mode_arg == "current") {
        checkpoint_evaluation_mode = CheckpointEvaluationMode::Current;
    } else if (
        checkpoint_evaluation_mode_arg == "no_mid" ||
        checkpoint_evaluation_mode_arg == "no-mid") {
        checkpoint_evaluation_mode = CheckpointEvaluationMode::NoMid;
    } else if (
        checkpoint_evaluation_mode_arg == "persist_only" ||
        checkpoint_evaluation_mode_arg == "persist-only") {
        checkpoint_evaluation_mode = CheckpointEvaluationMode::PersistOnly;
    } else if (
        checkpoint_evaluation_mode_arg != "fix_candidate" &&
        checkpoint_evaluation_mode_arg != "fix-candidate") {
        std::cerr << "Unknown --checkpoint-evaluation-mode: "
                  << checkpoint_evaluation_mode_arg << "\n";
        return 2;
    }
    checkpoint_dependency_diagnostics = has_arg(argc, argv, "--checkpoint-data-dependency-diagnostics");
    // materialization_trace_contract is owned by this command stage.
    const bool adaptive_near_trace_contract =
        run_adaptive_hybrid &&
        has_arg(argc, argv, "--adaptive-allow-near-trace-infeasible");
    const bool local_near_trace_contract =
        (run_hybrid || run_polish) &&
        has_arg(argc, argv, "--local-allow-near-trace-infeasible");
    materialization_trace_contract.allow_near_trace_infeasible =
        adaptive_near_trace_contract || local_near_trace_contract;
    if (adaptive_near_trace_contract) {
        materialization_trace_contract.trace_coverage_epsilon = std::max(
            0.0,
            double_arg(argc, argv, "--trace-coverage-epsilon", 1.0e-9));
    } else if (local_near_trace_contract) {
        materialization_trace_contract.trace_coverage_epsilon = std::max(
            0.0,
            double_arg(
                argc,
                argv,
                "--local-trace-coverage-epsilon",
                double_arg(argc, argv, "--trace-coverage-epsilon", 1.0e-9)));
    }
    return kContinueCommand;
}

bool OptimizationCommand::checkpoint_progress_with_variables(
            const std::vector<VariableSpec>& checkpoint_variables,
            const std::string& phase,
            int step,
            const Score& best,
            const std::vector<double>& best_values,
            bool apertures_already_materialized ,
            const LensSystem* prevalidated_system ,
            const Score* prevalidated_score ,
            LensSystem* validated_system_out ,
            Score* validated_score_out ) {
    return save_checkpoint(
        lens,
        checkpoint_variables,
        best_values,
        glass_search,
        merit,
        out_dir,
        phase,
        step,
        best,
        materialization_trace_contract,
        checkpoint_inherit_asphere_root_policy,
        latest_adaptive_glass_checkpoint_state.seen
            ? &latest_adaptive_glass_checkpoint_state
            : nullptr,
        latest_checkpoint_trace_event_seen
            ? &latest_checkpoint_trace_event
            : nullptr,
        apertures_already_materialized,
        prevalidated_system,
        prevalidated_score,
        checkpoint_evaluation_mode,
        checkpoint_dependency_diagnostics,
        validated_system_out,
        validated_score_out);
}

void OptimizationCommand::checkpoint_progress(const std::string& phase, int step, const Score& best,
                                       const std::vector<double>& best_values) {
    const std::vector<VariableSpec>& primary_variables = *primary_variables_source;

                checkpoint_progress_with_variables(primary_variables, phase, step, best, best_values);
}

int OptimizationCommand::checkpoint_initial_state() {
    const std::vector<VariableSpec>& primary_variables = *primary_variables_source;
    // The materialized optimizer input is a legitimate fallback whenever
    // it already satisfies the same trace contract as later iterates.  In
    // particular, a local-only DLS pass can move from an admissible 0.95
    // state to an inadmissible trial before its first progress callback;
    // without this seed there is then no validated checkpoint to recover.
    // Use the shared checkpoint path so dynamic apertures, root policy,
    // and static CPU rescoring remain identical to every later checkpoint.
    initial_checkpoint_values = current_variable_values_from_system(
                    optimizer_input_lens,
                    primary_variables);
    complete_de_started = std::chrono::steady_clock::now();
    checkpoint_progress_with_variables(
        primary_variables,
        "initial",
        -1,
        initial_score,
        initial_checkpoint_values,
        input_clear_apertures_materialized || update_input_clear_apertures,
        checkpoint_evaluation_mode == CheckpointEvaluationMode::FixCandidate
            ? &optimizer_input_lens
            : nullptr,
        checkpoint_evaluation_mode == CheckpointEvaluationMode::FixCandidate
            ? &initial_score
            : nullptr);
    if (checkpoint_evaluation_mode == CheckpointEvaluationMode::FixCandidate &&
        score_is_materialization_valid(
            initial_score, merit, materialization_trace_contract)) {
        last_adaptive_validated_system = optimizer_input_lens;
        last_adaptive_validated_score = initial_score;
        last_adaptive_validated_values = initial_checkpoint_values;
        last_adaptive_validation_valid = true;
    }
    return kContinueCommand;
}

} } }
