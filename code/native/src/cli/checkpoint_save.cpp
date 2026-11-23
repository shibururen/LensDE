// Materialize, optionally rescore, validate, and conditionally commit one checkpoint candidate.
// Optimization callbacks share this path for direct progress and prevalidated adaptive boundaries.
// Preserve root-policy scope, aperture updates, merit comparisons, and commit/rejection diagnostics.

#include "cli_internal.hpp"

namespace lensde { namespace cli { namespace detail {

bool save_checkpoint(
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& values,
    const GlassSearchContext& glass_search,
    const MeritConfig& merit,
    const std::string& out_dir,
    const std::string& phase,
    int step,
    const Score& best_score,
    const MaterializationTraceContract& trace_contract,
    bool inherit_optimizer_root_policy,
    const CheckpointAdaptiveGlassState* adaptive_glass_state ,
    const HybridTraceEvent* active_trace_event ,
    bool apertures_already_materialized ,
    const LensSystem* prevalidated_system ,
    const Score* prevalidated_score ,
    CheckpointEvaluationMode evaluation_mode ,
    bool dependency_diagnostics ,
    LensSystem* validated_system_out ,
    Score* validated_score_out ) {
    if (values.size() != variables.size()) {
        return false;
    }
    const auto checkpoint_started = std::chrono::steady_clock::now();
    auto elapsed_since = [](const auto& started) {
        return std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started).count();
    };
    const AsphereRootPolicy incoming_root_policy = asphere_root_policy();
    const AsphereRootPolicy checkpoint_root_policy =
        inherit_optimizer_root_policy ||
                incoming_root_policy == AsphereRootPolicy::SlopeDomainTopologySeed ||
                incoming_root_policy ==
                    AsphereRootPolicy::UnifiedCertifiedExperimental
            ? incoming_root_policy
            : AsphereRootPolicy::CertifiedFirstRoot;
    const ScopedAsphereRootPolicy strict_root_scope(checkpoint_root_policy);
    const bool reuse_prevalidated =
        prevalidated_system != nullptr && prevalidated_score != nullptr;
    const auto materialize_started = std::chrono::steady_clock::now();
    LensSystem checkpoint = reuse_prevalidated
                                ? *prevalidated_system
                                : apply_mixed_variables(base, variables, values, glass_search);
    checkpoint.asphere_root_policy =
        asphere_root_policy_token(checkpoint_root_policy);
    const double materialize_seconds = elapsed_since(materialize_started);
    const auto aperture_started = std::chrono::steady_clock::now();
    if (!reuse_prevalidated && merit.dynamic_apertures && !apertures_already_materialized) {
        update_clear_apertures_from_trace(
            checkpoint,
            clear_aperture_update_rings(merit),
            "gaussian_ring_arm",
            merit.sampling_seed,
            checkpoint.ray_aiming.mode,
            merit.effective_clear_aperture_margin(),
            true,
            merit.clear_aperture_arms,
            false,
            false,
            merit.vignetting_boundary_support_tolerance,
            merit.vignetting_boundary_support_selectivity_slope,
            merit.vignetting_boundary_support_contract ==
                VignettingBoundarySupportContract::GlobalTrimRealizability,
            merit.vignetting_physical_cut_match_enabled,
            merit.vignetting_optimization_enabled());
    } else if (!reuse_prevalidated && !merit.dynamic_apertures) {
        sync_stop_aperture_to_system(checkpoint);
    }
    const double aperture_seconds = elapsed_since(aperture_started);
    const bool checkpoint_recomputed =
        !reuse_prevalidated &&
        (merit.dynamic_apertures ||
        incoming_root_policy == AsphereRootPolicy::FastRecover ||
        incoming_root_policy == AsphereRootPolicy::SlopeDomainTopologySeed ||
        incoming_root_policy ==
            AsphereRootPolicy::UnifiedCertifiedExperimental);
    const auto rescore_started = std::chrono::steady_clock::now();
    const Score static_rescore = reuse_prevalidated
                                     ? *prevalidated_score
                                     : (checkpoint_recomputed
                                            ? evaluate_merit(checkpoint, merit)
                                            : best_score);
    const double rescore_seconds = elapsed_since(rescore_started);
    if (dependency_diagnostics) {
        write_checkpoint_dependency_snapshot(
            base,
            checkpoint,
            values,
            out_dir,
            phase,
            step,
            reuse_prevalidated ? "reuse_prevalidated" : "full_static_rescore",
            true);
    }
    const bool adaptive_glass_checkpoint =
        adaptive_glass_state != nullptr &&
        adaptive_glass_state->seen &&
        adaptive_glass_state->enabled;
    const Score checkpoint_score =
        adaptive_glass_checkpoint ? best_score : static_rescore;
    const std::string checkpoint_score_path = join_path(out_dir, "checkpoint_score.json");
    const double previous_checkpoint_merit =
        read_score_merit_from_json(checkpoint_score_path);
    const bool checkpoint_materialization_valid =
        score_is_materialization_valid(static_rescore, merit, trace_contract);
    const bool checkpoint_not_better =
        !adaptive_glass_checkpoint &&
        std::isfinite(previous_checkpoint_merit) &&
        (!std::isfinite(checkpoint_score.merit) ||
         previous_checkpoint_merit <= checkpoint_score.merit + 1.0e-12);
    const bool checkpoint_rejected =
        !checkpoint_materialization_valid || checkpoint_not_better;
    double commit_seconds = 0.0;
    auto write_timing = [&](std::ostream& out) {
        out << ",\"timing_seconds\":{";
        out << "\"materialize\":" << materialize_seconds;
        out << ",\"aperture_update\":" << aperture_seconds;
        out << ",\"static_rescore\":" << rescore_seconds;
        out << ",\"commit\":" << commit_seconds;
        out << ",\"total\":" << elapsed_since(checkpoint_started);
        out << "}";
    };
    if (checkpoint_rejected) {
        // A rejected CPU/static rescore is diagnostically important: without
        // this row a CUDA/CPU disagreement looks like the progress callback
        // simply stopped running.
        std::ofstream diag(
            join_path(out_dir, "checkpoint_diagnostics.jsonl"),
            std::ios::app);
        if (diag) {
            diag << "{\"phase\":\"" << phase << "\",\"step\":" << step;
            diag << ",\"checkpoint_root_policy\":\""
                 << asphere_root_policy_token(checkpoint_root_policy) << "\"";
            diag << ",\"checkpoint_root_policy_inherited\":"
                 << (inherit_optimizer_root_policy ? "true" : "false");
            diag << ",\"checkpoint_evaluation_mode\":\""
                 << checkpoint_evaluation_mode_token(evaluation_mode) << "\"";
            diag << ",\"checkpoint_score_source\":\""
                 << (reuse_prevalidated ? "prevalidated_input" : "static_checkpoint_rescore")
                 << "\"";
            diag << ",\"prevalidated_input_reused\":"
                 << (reuse_prevalidated ? "true" : "false");
            diag << ",\"checkpoint_recomputed\":"
                 << (checkpoint_recomputed ? "true" : "false");
            diag << ",\"accepted\":false,\"rejection_reason\":\""
                 << (!checkpoint_materialization_valid
                         ? "invalid_static_materialization"
                         : "not_better_than_checkpoint")
                 << "\"";
            diag << ",\"static_materialization_valid\":"
                 << (checkpoint_materialization_valid ? "true" : "false");
            diag << ",\"materialization_trace_contract\":\""
                 << (trace_contract.allow_near_trace_infeasible ? "near" : "strict")
                 << "\"";
            diag << ",\"trace_coverage_epsilon\":"
                 << (trace_contract.allow_near_trace_infeasible
                         ? std::max(0.0, trace_contract.trace_coverage_epsilon)
                         : 0.0);
            diag << ",\"previous_checkpoint_merit\":";
            write_json_number_or_null(diag, previous_checkpoint_merit);
            if (adaptive_glass_state != nullptr) {
                diag << ",\"adaptive_glass_tolerance\":";
                write_checkpoint_adaptive_glass_state_json(diag, *adaptive_glass_state);
            }
            diag << ",\"incoming_best_score\":";
            write_score_checkpoint_summary_compact(diag, best_score);
            diag << ",\"checkpoint_score\":";
            write_score_checkpoint_summary_compact(diag, checkpoint_score);
            write_checkpoint_active_trace_fields(diag, active_trace_event);
            diag << ",\"checkpoint_minus_incoming_merit\":";
            if (std::isfinite(checkpoint_score.merit) && std::isfinite(best_score.merit)) {
                write_json_number_or_null(diag, checkpoint_score.merit - best_score.merit);
            } else {
                diag << "null";
            }
            write_timing(diag);
            diag << "}\n";
        }
        return false;
    }
    if (validated_system_out != nullptr) {
        *validated_system_out = checkpoint;
    }
    if (validated_score_out != nullptr) {
        *validated_score_out = static_rescore;
    }
    const auto commit_started = std::chrono::steady_clock::now();
    save_system_json(checkpoint, join_path(out_dir, "checkpoint_system.json"));
    save_score_json(checkpoint_score, checkpoint_score_path);
    std::ofstream meta(join_path(out_dir, "checkpoint_meta.json"));
    if (meta) {
        meta << "{\n";
        meta << "  \"phase\": \"" << phase << "\",\n";
        meta << "  \"step\": " << step << ",\n";
        meta << "  \"merit\": ";
        write_json_number_or_null(meta, checkpoint_score.merit);
        meta << ",\n  \"checkpoint_root_policy\": \""
             << asphere_root_policy_token(checkpoint_root_policy) << "\"";
        meta << ",\n  \"checkpoint_root_policy_inherited\": "
             << (inherit_optimizer_root_policy ? "true" : "false");
        meta << ",\n  \"checkpoint_evaluation_mode\": \""
             << checkpoint_evaluation_mode_token(evaluation_mode) << "\"";
        meta << ",\n  \"checkpoint_score_source\": \""
             << (reuse_prevalidated
                     ? "prevalidated_input"
                     : (adaptive_glass_checkpoint
                            ? "adaptive_optimizer_score"
                            : "static_checkpoint_rescore"))
             << "\"";
        meta << ",\n  \"prevalidated_input_reused\": "
             << (reuse_prevalidated ? "true" : "false");
        meta << ",\n  \"checkpoint_recomputed\": "
             << ((!adaptive_glass_checkpoint && checkpoint_recomputed) ? "true" : "false");
        meta << ",\n  \"dynamic_apertures\": " << (merit.dynamic_apertures ? "true" : "false");
        meta << ",\n  \"static_materialization_valid\": "
             << (checkpoint_materialization_valid ? "true" : "false");
        meta << ",\n  \"edge_thickness_publication_policy\": \"finite_soft_merit\"";
        meta << ",\n  \"materialization_trace_contract\": \""
             << (trace_contract.allow_near_trace_infeasible ? "near" : "strict")
             << "\"";
        meta << ",\n  \"trace_coverage_epsilon\": "
             << (trace_contract.allow_near_trace_infeasible
                     ? std::max(0.0, trace_contract.trace_coverage_epsilon)
                     : 0.0);
        if (adaptive_glass_state != nullptr) {
            meta << ",\n  \"adaptive_glass_tolerance\": ";
            write_checkpoint_adaptive_glass_state_json(meta, *adaptive_glass_state);
        }
        meta << ",\n  \"incoming_best_score\": ";
        write_score_checkpoint_summary(meta, best_score, "  ");
        meta << ",\n  \"checkpoint_score\": ";
        write_score_checkpoint_summary(meta, checkpoint_score, "  ");
        if (adaptive_glass_checkpoint) {
            meta << ",\n  \"static_rescore_score\": ";
            write_score_checkpoint_summary(meta, static_rescore, "  ");
            meta << ",\n  \"static_rescore_minus_optimizer_merit\": ";
            if (std::isfinite(static_rescore.merit) && std::isfinite(best_score.merit)) {
                write_json_number_or_null(meta, static_rescore.merit - best_score.merit);
            } else {
                meta << "null";
            }
        }
        meta << ",\n  \"checkpoint_minus_incoming_merit\": ";
        if (std::isfinite(checkpoint_score.merit) && std::isfinite(best_score.merit)) {
            write_json_number_or_null(meta, checkpoint_score.merit - best_score.merit);
        } else {
            meta << "null";
        }
        meta << "\n}\n";
    }
    commit_seconds = elapsed_since(commit_started);
    std::ofstream diag(join_path(out_dir, "checkpoint_diagnostics.jsonl"), std::ios::app);
    if (diag) {
        diag << "{\"phase\":\"" << phase << "\",\"step\":" << step;
        diag << ",\"checkpoint_root_policy\":\""
             << asphere_root_policy_token(checkpoint_root_policy) << "\"";
        diag << ",\"checkpoint_root_policy_inherited\":"
             << (inherit_optimizer_root_policy ? "true" : "false");
        diag << ",\"checkpoint_evaluation_mode\":\""
             << checkpoint_evaluation_mode_token(evaluation_mode) << "\"";
        diag << ",\"checkpoint_score_source\":\""
             << (reuse_prevalidated
                     ? "prevalidated_input"
                     : (adaptive_glass_checkpoint
                            ? "adaptive_optimizer_score"
                            : "static_checkpoint_rescore"))
             << "\"";
        diag << ",\"prevalidated_input_reused\":"
             << (reuse_prevalidated ? "true" : "false");
        diag << ",\"checkpoint_recomputed\":"
             << ((!adaptive_glass_checkpoint && checkpoint_recomputed) ? "true" : "false");
        diag << ",\"accepted\":true,\"static_materialization_valid\":"
             << (checkpoint_materialization_valid ? "true" : "false");
        diag << ",\"materialization_trace_contract\":\""
             << (trace_contract.allow_near_trace_infeasible ? "near" : "strict")
             << "\"";
        diag << ",\"trace_coverage_epsilon\":"
             << (trace_contract.allow_near_trace_infeasible
                     ? std::max(0.0, trace_contract.trace_coverage_epsilon)
                     : 0.0);
        diag << ",\"previous_checkpoint_merit\":";
        write_json_number_or_null(diag, previous_checkpoint_merit);
        if (adaptive_glass_state != nullptr) {
            diag << ",\"adaptive_glass_tolerance\":";
            write_checkpoint_adaptive_glass_state_json(diag, *adaptive_glass_state);
        }
        diag << ",\"incoming_best_score\":";
        write_score_checkpoint_summary_compact(diag, best_score);
        diag << ",\"checkpoint_score\":";
        write_score_checkpoint_summary_compact(diag, checkpoint_score);
        write_checkpoint_active_trace_fields(diag, active_trace_event);
        if (adaptive_glass_checkpoint) {
            diag << ",\"static_rescore_score\":";
            write_score_checkpoint_summary_compact(diag, static_rescore);
            diag << ",\"static_rescore_minus_optimizer_merit\":";
            if (std::isfinite(static_rescore.merit) && std::isfinite(best_score.merit)) {
                write_json_number_or_null(diag, static_rescore.merit - best_score.merit);
            } else {
                diag << "null";
            }
        }
        diag << ",\"checkpoint_minus_incoming_merit\":";
        if (std::isfinite(checkpoint_score.merit) && std::isfinite(best_score.merit)) {
            write_json_number_or_null(diag, checkpoint_score.merit - best_score.merit);
        } else {
            diag << "null";
        }
        write_timing(diag);
        diag << "}\n";
    }
    return true;
}

bool append_checkpoint_diagnostic_event(
    const std::string& out_dir,
    const std::string& phase,
    const HybridTraceEvent& event) {
    if (out_dir.empty()) {
        return false;
    }
    std::ofstream diag(join_path(out_dir, "checkpoint_diagnostics.jsonl"), std::ios::app);
    if (!diag) {
        return false;
    }
    diag << "{\"phase\":\"" << phase << "\",\"step\":" << event.epoch;
    diag << ",\"event\":\"" << event.event << "\"";
    diag << ",\"diagnostic_only\":true";
    CheckpointAdaptiveGlassState adaptive_glass_state;
    update_checkpoint_adaptive_glass_state(adaptive_glass_state, event);
    diag << ",\"adaptive_glass_tolerance\":";
    write_checkpoint_adaptive_glass_state_json(diag, adaptive_glass_state);
    diag << ",\"incumbent_best_score\":";
    write_score_checkpoint_summary_compact(diag, event.best_score);
    diag << ",\"population_best_score\":";
    write_score_checkpoint_summary_compact(diag, event.population_best_score);
    diag << ",\"active_focus_best_merit\":";
    write_json_number_or_null(diag, event.active_focus_best_merit);
    diag << ",\"active_focus_best_key\":\""
         << json_escape_local(event.active_focus_best_key) << "\"";
    diag << ",\"active_focus_best_niche_index\":"
         << event.active_focus_best_niche_index;
    diag << ",\"active_focus_best_population_index\":"
         << event.active_focus_best_population_index;
    diag << ",\"active_focus_best_member_count\":"
         << event.active_focus_best_member_count;
    diag << ",\"active_niche_hard_cap_release_count\":"
         << event.active_niche_hard_cap_release_count;
    diag << ",\"active_niche_best_merits\":";
    write_active_niche_best_merits_json(diag, event.active_niche_best_merits);
    diag << ",\"population_best_values\":[";
    for (std::size_t i = 0; i < event.population_best_values.size(); ++i) {
        if (i > 0) {
            diag << ",";
        }
        write_json_number_or_null(diag, event.population_best_values[i]);
    }
    diag << "]";
    diag << ",\"feasible_count\":" << event.feasible_count;
    diag << ",\"trace_feasible_count\":" << event.trace_feasible_count;
    diag << ",\"constraint_feasible_count\":" << event.constraint_feasible_count;
    diag << ",\"first_order_feasible_count\":" << event.first_order_feasible_count;
    diag << ",\"population_elite_count\":" << event.population_elite_count;
    diag << ",\"population_elite_mean_merit\":";
    write_json_number_or_null(diag, event.population_elite_mean_merit);
    diag << ",\"population_elite_median_merit\":";
    write_json_number_or_null(diag, event.population_elite_median_merit);
    diag << ",\"population_elite_worst_merit\":";
    write_json_number_or_null(diag, event.population_elite_worst_merit);
    diag << ",\"basin_elite_count\":" << event.basin_elite_count;
    diag << ",\"basin_elite_mean_merit\":";
    write_json_number_or_null(diag, event.basin_elite_mean_merit);
    diag << ",\"basin_elite_median_merit\":";
    write_json_number_or_null(diag, event.basin_elite_median_merit);
    diag << ",\"basin_elite_worst_merit\":";
    write_json_number_or_null(diag, event.basin_elite_worst_merit);
    diag << ",\"active_plateau_tracked_family_count\":"
         << event.active_plateau_tracked_family_count;
    diag << ",\"active_plateau_active_niche_tracked_count\":"
         << event.active_plateau_active_niche_tracked_count;
    diag << ",\"active_plateau_stalled_family_count\":"
         << event.active_plateau_stalled_family_count;
    diag << ",\"active_plateau_cooldown_family_count\":"
         << event.active_plateau_cooldown_family_count;
    diag << ",\"active_plateau_retired_family_count\":"
         << event.active_plateau_retired_family_count;
    diag << ",\"active_plateau_evict_count\":"
         << event.active_plateau_evict_count;
    diag << ",\"active_plateau_retire_clear_count\":"
         << event.active_plateau_retire_clear_count;
    diag << ",\"active_plateau_evict_candidate_count\":"
         << event.active_plateau_evict_candidate_count;
    diag << ",\"active_plateau_evict_cap_count\":"
         << event.active_plateau_evict_cap_count;
    diag << ",\"active_plateau_evict_distance_min\":";
    write_json_number_or_null(diag, event.active_plateau_evict_distance_min);
    diag << ",\"active_plateau_evict_distance_median\":";
    write_json_number_or_null(diag, event.active_plateau_evict_distance_median);
    diag << ",\"active_plateau_evict_distance_max\":";
    write_json_number_or_null(diag, event.active_plateau_evict_distance_max);
    diag << ",\"active_plateau_refill_basin_count\":"
         << event.active_plateau_refill_basin_count;
    diag << ",\"active_plateau_refill_population_count\":"
         << event.active_plateau_refill_population_count;
    diag << ",\"active_plateau_refill_refresh_count\":"
         << event.active_plateau_refill_refresh_count;
    diag << ",\"active_plateau_refill_initial_seed_count\":"
         << event.active_plateau_refill_initial_seed_count;
    diag << ",\"active_plateau_refill_generated_count\":"
         << event.active_plateau_refill_generated_count;
    diag << ",\"active_plateau_refill_lhs_count\":"
         << event.active_plateau_refill_lhs_count;
    diag << ",\"active_plateau_refill_clone_fallback_count\":"
         << event.active_plateau_refill_clone_fallback_count;
    diag << ",\"active_plateau_refill_rejected_trace_count\":"
         << event.active_plateau_refill_rejected_trace_count;
    diag << ",\"active_plateau_refill_rejected_retired_count\":"
         << event.active_plateau_refill_rejected_retired_count;
    diag << ",\"active_plateau_retired_trial_reject_count\":"
         << event.active_plateau_retired_trial_reject_count;
    diag << ",\"population_best_previous_merit\":";
    write_json_number_or_null(diag, event.population_best_previous_merit);
    diag << ",\"population_best_improvement\":";
    write_json_number_or_null(diag, event.population_best_improvement);
    diag << ",\"population_best_improved\":"
         << (event.population_best_improved ? "true" : "false");
    diag << ",\"population_best_vs_incumbent_merit_delta\":";
    write_json_number_or_null(diag, event.population_best_vs_incumbent_merit_delta);
    diag << ",\"population_diversity_tracked\":"
         << (event.population_diversity_tracked ? "true" : "false");
    diag << ",\"population_distance_to_best_count\":"
         << event.population_distance_to_best_count;
    diag << ",\"population_mean_euclidean_distance_to_best\":";
    write_json_number_or_null(diag, event.population_mean_euclidean_distance_to_best);
    diag << "}\n";
    return true;
}

} } }
