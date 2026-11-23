/*
 * Initialize adaptive convergence history, diagnostics, geometry-memory mutation and active focus.
 * These final setup phases consume the initial scored state before run_search_epochs starts.
 * Preserve initial snapshot/observer boundaries and RNG/distribution setup relative to the first epoch.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

void AdaptiveSession::initialize_convergence() {
run_.early_stop_history = std::vector<EarlyStopPoint>{};
    run_.early_stop_best_topology_count = 0;
    
    run_.early_stop_metric = this->normalize_metric_token(options.early_stop_metric.empty()
                                   ? "basin_unique_topology_elite_mean_merit"
                                   : options.early_stop_metric);
    run_.early_stop_topology_discovery = run_.early_stop_metric == "topology_discovery" ||
        run_.early_stop_metric == "family_discovery" ||
        run_.early_stop_metric == "topology_count" ||
        run_.early_stop_metric == "family_count";
    run_.early_stop_basin_discovery = run_.early_stop_metric == "basin_discovery" ||
        run_.early_stop_metric == "archive_basin_discovery" ||
        run_.early_stop_metric == "basin_count" ||
        run_.early_stop_metric == "archive_basin_count";
    run_.early_stop_best_merit = run_.early_stop_metric == "merit" ||
        run_.early_stop_metric == "best_merit" ||
        run_.early_stop_metric == "best";
    run_.early_stop_discovery_count = run_.early_stop_topology_discovery || run_.early_stop_basin_discovery;

    if (options.early_stop_enabled &&
        options.early_stop_include_initial_point &&
        run_.early_stop_best_merit) {
        run_.early_stop_history.push_back({0, 0, run_.best_fitness.score.merit});
    }

    

    
    
    
    
    
    
    

    
    
}

void AdaptiveSession::initialize_diagnostics() {
run_.last_pbest_stats = PBestGuideStats{};

    
    run_.last_mating_stats = DeMatingStats{};
    run_.last_active_refresh_rebalance_family_count = 0;
    run_.last_active_refresh_rebalance_family_limit = 0;
    run_.last_active_refresh_rebalance_max_family_size = 0;
    run_.last_active_refresh_rebalance_candidate_count = 0;
    run_.last_active_refresh_rebalance_selected_count = 0;
    run_.last_active_local_cohort_family_count = 0;
    run_.last_active_local_cohort_anchor_count = 0;
    run_.last_active_local_cohort_total_limit = 0;
    run_.last_active_local_cohort_per_family = 0;
    run_.last_active_local_cohort_selected_count = 0;
    run_.last_active_local_cohort_max_family_size = 0;
    
    run_.last_active_niche_stats = ActiveNicheStats{};
    run_.last_active_niche_merit_snapshots = std::vector<optiland::ActiveNicheMeritSnapshot>{};
    run_.epoch_efl_sign_priority_promoted_trial_count = 0;
    run_.epoch_efl_sign_priority_blocked_trial_count = 0;
    run_.epoch_cra_worsening_guard_blocked_trial_count = 0;
    run_.epoch_joint_constraint_anchor_activated_count = 0;
    run_.epoch_joint_constraint_anchor_updated_count = 0;
    run_.epoch_joint_constraint_anchor_blocked_cra_count = 0;
    run_.epoch_joint_constraint_anchor_blocked_distortion_count = 0;
    run_.epoch_joint_constraint_anchor_blocked_both_count = 0;

    

    run_.research_snapshot_env = std::getenv("OPTILAND_CHECKPOINT_SNAPSHOT_DIR");
    run_.research_snapshot_dir = !options.checkpoint_snapshot_dir.empty()
            ? options.checkpoint_snapshot_dir
            : (run_.research_snapshot_env == nullptr ? std::string{} :
               std::string(run_.research_snapshot_env));
    if (!run_.research_snapshot_dir.empty()) {
        std::ofstream(
            research_snapshot_path(run_.research_snapshot_dir, "snapshot_index.jsonl"),
            std::ios::trunc);
        std::ofstream(
            research_snapshot_path(run_.research_snapshot_dir, "candidate_events.jsonl"),
            std::ios::trunc);
    }
    

    if (progress) {
        progress(0, run_.best_fitness.score, scaled_to_physical(owner.variables_, run_.best_scaled, run_.internal_mode));
    }
    this->emit_research_snapshot(0);
    // LENSDE_REPLAY_INITIAL
    this->emit_trace(
        "adaptive_initial",
        0,
        run_.de_seconds,
        run_.best_fitness.score.merit,
        std::numeric_limits<double>::quiet_NaN(),
        0,
        0,
        0,
        0,
        0,
        run_.used_cuda);

    

    

    

    
    

    
    
    
    
    
    
    
}

void AdaptiveSession::initialize_geometry_memory_mutation() {
run_.geometry_memory_population_near_cache = std::vector<signed char>{};
    run_.geometry_memory_difference_index_cache = std::vector<int>{};
    run_.geometry_memory_key_trigger_cache = std::unordered_map<std::string, std::vector<unsigned char> >{};
    run_.geometry_memory_key_max_similarity_cache = std::unordered_map<std::string, double>{};
    run_.geometry_memory_qohr_family_similarity_cache = std::unordered_map<std::string, std::vector<double> >{};
    run_.geometry_memory_exact_escape_active = false;
    run_.geometry_memory_family_escape_active = false;
    
    
    
    
    
    
    run_.geometry_memory_qohr_dimensions = std::vector<int>{};
    run_.geometry_memory_qohr_ranges = std::vector<double>(
        static_cast<std::size_t>(run_.dim),
        0.0);
    if (options.geometry_memory_qohr_mutation) {
        run_.geometry_memory_qohr_dimensions.reserve(static_cast<std::size_t>(run_.dim));
        for (int j = 0; j < run_.dim; ++j) {
            const auto& var = owner.variables_[static_cast<std::size_t>(j)];
            const bool geometry_variable =
                var.kind == VariableKind::Radius ||
                var.kind == VariableKind::Curvature ||
                var.kind == VariableKind::Thickness ||
                var.kind == VariableKind::Conic ||
                var.kind == VariableKind::AsphereCoeff;
            if (!geometry_variable || is_glass_choice_variable(var)) {
                continue;
            }
            const auto [lo, hi] = internal_bounds(var, run_.internal_mode);
            const double range = hi - lo;
            if (std::isfinite(range) && range > 1.0e-15) {
                run_.geometry_memory_qohr_dimensions.push_back(j);
                run_.geometry_memory_qohr_ranges[static_cast<std::size_t>(j)] = range;
            }
        }
    }
    run_.geometry_memory_qohr_g = std::vector<double>(
        static_cast<std::size_t>(run_.dim),
        0.0);
    run_.geometry_memory_qohr_e = std::vector<double>(
        static_cast<std::size_t>(run_.dim),
        0.0);
    run_.geometry_memory_qohr_e_perp = std::vector<double>(
        static_cast<std::size_t>(run_.dim),
        0.0);
    run_.geometry_memory_qohr_history = std::vector<double>(
        static_cast<std::size_t>(run_.dim),
        0.0);
    run_.geometry_memory_qohr_direction = std::vector<double>(
        static_cast<std::size_t>(run_.dim),
        0.0);
    
    
    
    
    
    
    
    
    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    
    
    

    
    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    
}

void AdaptiveSession::initialize_active_focus() {
run_.release_stopped_active_focus_ready = false;

    

    

    

    

    

    

    run_.release_stopped_active_focus_ready = true;

    

    

    

    

    

    

    

    run_.early_stop_tail_active = false;
    run_.early_stop_tail_remaining = 0;
    run_.weak_refresh_windows = 0;
    run_.plateau_pulse_used = false;

    
}

} } // namespace optiland::optimizer_detail
