#pragma once
#include "adaptive_types.hpp"

namespace optiland { namespace optimizer_detail {
struct AdaptiveRunState {

    // Active Focus
    bool release_stopped_active_focus_ready;
    std::vector<std::string > active_focus_family_keys;
    std::unordered_map<std::string, ActiveFocusLifecycleState> active_focus_lifecycle;
    std::vector<ActiveFocusRotationEntry> active_focus_rotation_history;
    int last_active_focus_ready_count;
    int last_active_focus_cooldown_family_count;
    int last_active_focus_stopped_family_count;
    int active_focus_stop_clear_count;
    int epoch_active_focus_remap_count;
    int epoch_active_focus_reset_count;
    int epoch_active_focus_selection_count;
    int epoch_active_focus_rotation_blocked_count;
    int epoch_active_focus_rotation_fallback_count;
    int epoch_active_focus_soft_fill_candidate_count;
    int epoch_active_focus_soft_fill_move_count;
    int epoch_active_focus_soft_fill_anchor_count;
    int epoch_active_focus_stop_release_candidate_count;
    int epoch_active_focus_stop_release_move_count;
    double last_active_focus_rel_improvement;

    // Policy
    Clock::time_point run_start;
    int dim;
    int sizing_dim;
    optiland::InternalScalingMode internal_mode;
    optiland::DifferentialEvolutionOptions de_options;
    int configured_multiplier;
    int initial_pop_size;
    int min_pop_size;
    int outer_epochs;
    int inner_generations;
    int max_total_generations;
    bool variant_uses_jade;
    bool variant_uses_de;
    bool variant_uses_lpsr;
    double classic_f_min;
    double classic_f_max;
    double default_gene_mutation_rate;
    int effective_ga_tournament_size;
    double effective_ga_crossover_rate;
    double effective_ga_mutation_rate;
    double effective_ga_mutation_scale_start;
    double effective_ga_mutation_scale_end;
    double effective_pso_inertia;
    double effective_pso_cognitive;
    double effective_pso_social;
    double effective_pso_initial_velocity_fraction;
    double effective_pso_max_velocity_fraction;
    double effective_pso_boundary_damping;
    double effective_sa_step_scale_start;
    double effective_sa_step_scale_end;
    double effective_sa_temperature_start;
    double effective_sa_temperature_end;
    double effective_sa_temperature_power;
    optiland::DeTimingStats timing;
    const char* detailed_trial_timing_env;
    bool detailed_trial_timing;
    Clock::time_point phase_start;
    bool descriptor_enabled;
    bool descriptor_select_enabled;
    bool descriptor_full_enabled;
    std::mt19937 rng;
    std::uniform_real_distribution<double> uni;
    std::normal_distribution<double> normal;
    bool used_cuda;
    int pop_size;
    std::vector<double> initial_values;
    std::vector<double> initial_scaled;
    std::vector<JointConstraintAnchorState> joint_constraint_anchors;
    int next_similarity_family_id;
    int best_idx;
    std::vector<double> best_scaled;
    int current_outer_epoch;
    double de_seconds;
    int total_de_generations;
    int lpsr_reduction_count;
    int lpsr_removed_total;
    double mu_f;
    double mu_cr;
    int last_active_local_cohort_family_count;
    int last_active_local_cohort_anchor_count;
    int last_active_local_cohort_total_limit;
    int last_active_local_cohort_per_family;
    int last_active_local_cohort_selected_count;
    int last_active_local_cohort_max_family_size;
    int epoch_efl_sign_priority_promoted_trial_count;
    int epoch_efl_sign_priority_blocked_trial_count;
    int epoch_cra_worsening_guard_blocked_trial_count;
    int epoch_joint_constraint_anchor_activated_count;
    int epoch_joint_constraint_anchor_updated_count;
    int epoch_joint_constraint_anchor_blocked_cra_count;
    int epoch_joint_constraint_anchor_blocked_distortion_count;
    int epoch_joint_constraint_anchor_blocked_both_count;
    bool plateau_pulse_used;

    // Polish
    optiland::PolishOptions polish_options;
    int requested_polish_count;
    int polish_refill_max_extra;
    int max_polish_attempts;
    double polish_seconds;
    int polish_runs;
    int polish_improvements;

    // Population
    bool variant_uses_archive;
    bool population_cuda_locked;
    std::vector<double> population;
    std::vector<std::vector<double> > external_archive;
    std::vector<std::string > external_archive_keys;

    // Evaluation
    bool scalar_merit_fast_path;
    bool cuda_score_summary_fast_path;
    int evaluation_certificate_mode;
    bool evaluation_certificate_allowed;
    std::vector<optiland::EvaluationCertificate> evaluation_batch_certificates;
    std::vector<optiland::CandidateFitness> fitness;
    std::vector<double> merits;
    optiland::CandidateFitness best_fitness;

    // Glass Tolerance
    double effective_sa_glass_mutation_rate;
    AdaptiveGlassToleranceState adaptive_glass_tolerance;

    // Basins
    bool topology_similarity_enabled;
    bool topology_family_similarity_enabled;
    PortableMutex topology_timing_mutex;
    std::vector<optiland::BasinElite> basins;
    int next_basin_id;
    bool basin_uses_distance_embedding;
    bool basin_distance_structure_only;
    std::vector<int> basin_distance_columns;
    std::unordered_map<long long unsigned int, CachedTopologySimilarity> topology_similarity_cache;
    std::unordered_map<long long unsigned int, int> topology_shape_diff_cache;
    std::unordered_map<std::string, CachedParsedTopologyKey> parsed_topology_similarity_cache;
    unsigned int next_parsed_topology_key_id;

    // Geometry Memory
    bool geometry_memory_enabled;
    double geometry_memory_qohr_reference_merit;
    std::vector<std::vector<double> > geometry_memory_scaled;
    std::vector<ParsedStructuralTopologyKey> geometry_memory_parsed_keys;
    std::vector<signed char> geometry_memory_population_near_cache;
    std::vector<int> geometry_memory_difference_index_cache;
    std::unordered_map<std::string, std::vector<unsigned char> > geometry_memory_key_trigger_cache;
    std::unordered_map<std::string, double> geometry_memory_key_max_similarity_cache;
    std::unordered_map<std::string, std::vector<double> > geometry_memory_qohr_family_similarity_cache;
    bool geometry_memory_exact_escape_active;
    bool geometry_memory_family_escape_active;
    std::vector<int> geometry_memory_qohr_dimensions;
    std::vector<double> geometry_memory_qohr_ranges;
    std::vector<double> geometry_memory_qohr_g;
    std::vector<double> geometry_memory_qohr_e;
    std::vector<double> geometry_memory_qohr_e_perp;
    std::vector<double> geometry_memory_qohr_history;
    std::vector<double> geometry_memory_qohr_direction;

    // Avoidance
    std::unordered_map<std::string, bool> avoided_topology_match_cache;
    std::unordered_map<std::string, ContinuousAvoidMatch> avoided_continuous_match_cache;
    long long unsigned int avoided_topology_key_count;
    PortableMutex avoided_topology_match_cache_mutex;
    PortableMutex avoided_continuous_match_cache_mutex;
    std::unordered_set<std::string > avoided_topology_key_set;
    std::vector<ParsedStructuralTopologyKey> avoided_structural_topology_keys;
    std::vector<ContinuousStructureSignature> avoided_continuous_structure_signatures;

    // Active Families
    std::vector<int> active_basin_slots;
    std::vector<std::string > active_basin_slot_origin_keys;
    std::vector<std::string > active_basin_slot_family_keys;
    std::unordered_map<std::string, ActiveBasinLifecycleState> active_basin_lifecycle;

    // Active Niche
    std::vector<int> population_niche_id;
    std::vector<std::string > active_niche_family_keys;
    bool active_niche_membership_reset_pending;
    int last_active_plateau_active_niche_tracked_count;
    int epoch_active_niche_hard_cap_release_count;
    int epoch_active_niche_soft_fill_candidate_count;
    int epoch_active_niche_soft_fill_move_count;
    int epoch_active_niche_soft_fill_anchor_count;
    ActiveNicheStats last_active_niche_stats;
    std::vector<optiland::ActiveNicheMeritSnapshot> last_active_niche_merit_snapshots;

    // Active Lifecycle
    std::unordered_map<std::string, bool> active_lifecycle_mating_stalled_cache;
    std::unordered_map<std::string, bool> active_lifecycle_mating_suppressed_cache;
    int last_active_plateau_new_cooldown_count;
    int last_active_plateau_retired_family_count;
    int last_active_plateau_evict_count;
    int last_active_plateau_evict_candidate_count;
    int last_active_plateau_evict_cap_count;
    double last_active_plateau_evict_distance_min;
    double last_active_plateau_evict_distance_median;
    double last_active_plateau_evict_distance_max;
    int last_active_plateau_refill_basin_count;
    int last_active_plateau_refill_population_count;
    int last_active_plateau_refill_refresh_count;
    int last_active_plateau_refill_initial_seed_count;
    int last_active_plateau_refill_generated_count;
    int last_active_plateau_refill_lhs_count;
    int last_active_plateau_refill_clone_fallback_count;
    int last_active_plateau_refill_rejected_trace_count;
    int last_active_plateau_refill_rejected_retired_count;
    int last_active_plateau_retired_trial_reject_count;
    int active_plateau_retire_defer_until_epoch;
    int active_plateau_retire_clear_count;
    bool active_plateau_initial_frontier_retired;
    std::vector<std::string > active_plateau_retired_filter_keys;
    std::vector<std::vector<double> > active_plateau_retired_filter_scaled_rows;
    int retired_refresh_archive_keep_per_family;

    // Refresh
    std::vector<TraceableRefreshArchiveEntry> traceable_refresh_archive;
    std::vector<TraceableRefreshArchiveEntry> pending_traceable_refresh_archive;
    long long int pending_traceable_refresh_archive_seen;
    int last_active_refresh_rebalance_family_count;
    int last_active_refresh_rebalance_family_limit;
    int last_active_refresh_rebalance_max_family_size;
    int last_active_refresh_rebalance_candidate_count;
    int last_active_refresh_rebalance_selected_count;
    int weak_refresh_windows;

    // Mating
    std::vector<TraceableRefreshArchiveEntry> initial_traceable_seed_pool;
    DeMatingStats last_mating_stats;

    // Diagnostics
    double last_trace_population_best_merit;
    const char* research_snapshot_env;
    std::string research_snapshot_dir;

    // Convergence
    std::vector<EarlyStopPoint> early_stop_history;
    int early_stop_best_topology_count;
    std::string early_stop_metric;
    bool early_stop_topology_discovery;
    bool early_stop_basin_discovery;
    bool early_stop_best_merit;
    bool early_stop_discovery_count;
    bool early_stop_tail_active;
    int early_stop_tail_remaining;

    // Pbest
    PBestGuideStats last_pbest_stats;
};
} } // namespace optiland::optimizer_detail
