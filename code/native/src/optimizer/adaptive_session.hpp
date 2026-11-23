#pragma once
#include "support.hpp"
#include "adaptive_types.hpp"
#include "adaptive_state.hpp"
#include "adaptive_contexts.hpp"

namespace optiland { namespace optimizer_detail {
class AdaptiveSession final {
public:
    AdaptiveSession(DifferentialEvolution& solver, const AdaptiveHybridDeDlsOptions& settings, ProgressCallback callback)
        : owner(solver), options(settings), progress(std::move(callback)) {}
    OptimizationResult execute();
private:
    DifferentialEvolution& owner;
    const AdaptiveHybridDeDlsOptions& options;
    ProgressCallback progress;
    AdaptiveRunState run_;

    // Active Families
    double active_family_similarity_threshold();
    int active_family_max_shape_diff();
    bool active_family_shape_diff_ok(const std::string& left, const std::string& right);
    bool active_family_parameter_merge_enabled();
    double active_family_parameter_merge_distance();
    bool active_topology_keys_are_similar(const std::string& left, const std::string& right, double* similarity_out = nullptr);
    double active_family_parameter_distance(const std::vector<double>& left, const std::vector<double>& right);
    bool active_family_parameter_rows_match(const std::vector<double>& left, const std::vector<double>& right);
    std::string active_current_family_key_for_basin(const BasinElite& basin);
    int active_basin_slot_budget();
    bool is_active_basin_slot(int index);
    double active_basin_merit_ceiling();
    std::string active_basin_reject_reason(const BasinElite& basin);
    bool active_basin_quality_ok(const BasinElite& basin);
    bool active_keys_match(const std::string& candidate_key, const std::string& target_key, bool allow_similarity);
    bool active_family_match_basins(const BasinElite& candidate, const BasinElite& target, bool allow_similarity);
    bool active_family_match_basin_to_key(const BasinElite& candidate, const std::string& target_key, const std::vector<double>* target_scaled, bool allow_similarity);
    bool active_family_match_population_to_key(const std::string& candidate_key, const std::vector<double>& candidate_scaled, const std::string& target_key, const std::vector<double>* target_scaled, bool allow_similarity);
    std::vector<std::string > active_slot_current_family_keys();
    std::vector<std::string > active_slot_frozen_family_keys();
    long long unsigned int active_slot_offset_for_index(int idx);
    std::string active_slot_frozen_family_key_for_index(int idx);
    bool active_slot_strict_frozen_family_enabled();
    bool active_slot_uses_strict_frozen_family(int idx);
    std::string active_slot_current_family_key_for_offset(std::size_t active_offset);
    std::vector<double> active_slot_scaled_row_for_offset(std::size_t active_offset);
    
    int count_unique_active_family_slots(const std::vector<std::string>& keys, const std::vector<std::vector<double>>& scaled_rows);
    std::vector<int> active_basin_quality_candidate_indices();
    std::vector<int> active_basin_candidate_indices();
    std::vector<int> active_basin_family_candidate_indices();
    std::vector<int> select_active_basin_indices();
    void inject_active_basins();
    int matching_active_basin_slot(const std::vector<double>& scaled);
    void update_active_basin_lifecycle(int epoch);
    std::string active_family_key_for_population_index(int idx, const LocalMatingPools& pools);
    int active_family_anchor_index(const std::vector<ActiveFamilyAnchor>& families, const std::string& key);
    std::vector<ActiveFamilyAnchor> build_active_family_anchors(const LocalMatingPools& pools);
    std::vector<std::string > active_family_keys_from_anchors(const std::vector<ActiveFamilyAnchor>& families);
    std::vector<int> strict_active_slot_population_pool(int idx, const LocalMatingPools& pools, bool allow_similarity);
    std::vector<int> strict_active_slot_archive_pool(int idx, const LocalMatingPools& pools, bool allow_similarity);
    std::vector<std::string > active_family_keys_from_slots(const LocalMatingPools& pools);
    std::vector<std::string > active_family_keys_from_frozen_slots();
    int active_family_index_for_key(const std::string& target_key, const std::vector<std::string>& family_keys);
    ActiveFamilyCoverage active_family_coverage(const LocalMatingPools& pools);
    std::string find_matching_active_family_key(const std::string& key, const std::vector<std::string>& family_keys);
    bool target_matches_active_basin_family(int i, const LocalMatingPools& pools);

    // Active Focus
    bool active_focus_soft_fill_enabled();
    int adaptive_glass_tolerance_focus_clear_target();
    bool adaptive_glass_tolerance_focus_bridge_frozen();
    bool active_focus_key_matches_current(const std::string& key);
    bool active_focus_key_in_cooldown(const std::string& key);
    int count_active_focus_cooldown_families();
    bool active_focus_rotation_enabled();
    double active_focus_rotation_similarity_threshold();
    bool active_focus_rotation_keys_match(const std::string& left, const std::string& right);
    void prune_active_focus_rotation_history();
    bool active_focus_key_recently_rotated(const std::string& key);
    void remember_active_focus_rotation_key(const std::string& key, int epoch);
    bool active_focus_epoch_enabled();
    bool active_focus_rebalance_enabled();
    bool active_focus_key_already_selected(const std::vector<std::string>& keys, const std::string& candidate);
    void merge_active_focus_lifecycle_state(ActiveFocusLifecycleState& target, const ActiveFocusLifecycleState& source);
    void remap_active_focus_lifecycle_state(const std::string& old_key, const std::string& new_key);
    void remap_active_focus_keys_to_families(const std::vector<std::string>& family_keys);
    int resolved_active_focus_target_quota();
    std::pair<int, double> active_focus_family_best_and_count(const std::string& focus_key);
    void update_active_focus_lifecycle(int epoch);
    int release_stopped_active_focus_families(const std::vector<std::string>& stopped_keys, int epoch);
    void configure_focus_niches(ActiveNicheGenerationState& state, const bool& focus_enabled);
    void select_focus_families(ActiveNicheGenerationState& state, const bool& focus_bridge_frozen, const int& focus_count);

    // Active Lifecycle
    int active_plateau_retired_unique_count();
    void refresh_active_plateau_retire_clear_count();
    void clear_active_lifecycle_match_caches();
    bool active_plateau_lifecycle_enabled();
    std::string active_lifecycle_key_for_basin(const BasinElite& basin);
    double active_plateau_retire_match_distance();
    bool active_plateau_retire_filter_enabled();
    bool active_plateau_retired_key_matches(const std::string& key);
    double active_plateau_nearest_retired_anchor_distance(const std::vector<double>& scaled);
    bool active_plateau_row_matches_retired(const std::vector<double>& scaled, const std::string& key);
    bool active_plateau_fitness_matches_retired(const std::vector<double>& scaled, const CandidateFitness& candidate);
    bool active_plateau_basin_matches_retired(const BasinElite& basin);
    bool add_active_plateau_retired_filter(const std::string& key, const std::vector<double>& scaled);
    bool active_lifecycle_key_in_cooldown(const std::string& key);
    bool active_lifecycle_basin_in_cooldown(const BasinElite& basin);
    bool active_lifecycle_key_stalled(const std::string& key);
    bool active_lifecycle_mating_key_stalled(const std::string& key);
    bool active_lifecycle_mating_key_suppressed(const std::string& key);
    bool active_lifecycle_recently_drifted_key(const std::string& key);
    std::vector<int> active_plateau_tracking_candidate_indices();
    void evict_active_plateau_retired_keys(const std::vector<std::string>& retired_keys, const std::vector<std::vector<double>>& retired_scaled_rows, int epoch);
    bool force_retire_initial_active_plateau_frontier(int epoch);
    std::string refresh_archive_retired_group_key(const std::vector<double>& scaled, const CandidateFitness& fitness);
    PBestGuideSelection mark_active_plateau_pbest_suppressed(PBestGuideSelection guide, bool global_rescue);
    MicroRetireDecision add_micro_retired_basin(int& outer, std::vector<std::string >& micro_retired_keys, std::vector<std::vector<double> >& micro_retired_scaled_rows, const bool& micro_retire_allow_similarity, const BasinElite& retired_basin);

    // Active Niche
    bool active_niche_soft_fill_enabled();
    bool active_niche_epoch_enabled();
    bool active_niche_tracking_enabled();
    std::vector<std::string > unique_active_niche_keys(std::vector<std::string> keys);
    int resolve_active_niche_min_quota();
    int resolve_active_niche_family_cap(int min_quota);
    int resolve_active_niche_target_quota(int min_quota, int family_cap);
    void refresh_active_niche_membership(const LocalMatingPools& pools, bool force_reset);
    ActiveNicheGenerationState make_active_niche_generation_state();
    int active_niche_family_cap_for(const ActiveNicheGenerationState& state, int niche_id);
    int active_niche_target_quota_for(const ActiveNicheGenerationState& state, int niche_id);
    std::vector<optiland::ActiveNicheMeritSnapshot> build_active_niche_merit_snapshots(const ActiveNicheGenerationState& state);
    void sync_active_niche_state_snapshot(const ActiveNicheGenerationState& state);
    void publish_active_niche_stats(const ActiveNicheGenerationState& state, int floor_blocked, int family_cap_blocked, int deferred_candidates, int repair_moves, int rebalance_candidates, int rebalance_moves);
    int active_niche_index_for_fitness(const CandidateFitness& candidate, const ActiveNicheGenerationState& state);
    void apply_active_niche_membership_change(int population_index, int new_niche_id, ActiveNicheGenerationState& state);
    int release_active_niche_hard_cap_excess(ActiveNicheGenerationState& state);
    int active_soft_fill_population(ActiveNicheGenerationState& state);
    void fill_niche_member_quotas(ActiveNicheGenerationState& state, std::vector<FocusFillCandidate>& candidates, std::vector<char>& used_candidate, std::vector<char>& used_target, const std::vector<int>& protected_indices, int& moves);
    void apply_deferred_niche_survival(const ApplyDeferredNicheSurvivalContext& frame_);
    void rebalance_active_niches(int& gen, ActiveNicheGenerationState& active_niche_state, int& active_niche_rebalance_candidates, int& active_niche_rebalance_moves);
    bool family_soft_fill_enabled(ActiveNicheGenerationState& state, int niche_id);
    int family_soft_fill_target_quota(ActiveNicheGenerationState& state, int niche_id);

    // Avoidance
    bool structural_key_like(const std::string& value);
    bool compute_avoided_topology_key_match(const std::string& key);
    bool avoided_topology_key_matches(const std::string& key);
    ContinuousAvoidMatch compute_avoided_continuous_match(const std::string& key);
    ContinuousAvoidMatch avoided_continuous_structure_match(const std::string& key);
    void apply_topology_avoidance(CandidateFitness& fitness, int generation);

    // Basins
    void assign_topology_summary_for_system(CandidateFitness& fitness, const LensSystem& candidate_system);
    std::pair<std::string, std::string > topology_keys_for_physical(const std::vector<double>& values);
    void assign_topology_keys_for_physical(CandidateFitness& fitness, const std::vector<double>& values);
    void assign_topology_keys_for_physical_row(CandidateFitness& fitness, const double* values, std::size_t count);
    std::string topology_key_for_physical(const std::vector<double>& values);
    std::string topology_key_for_scaled(const std::vector<double>& scaled);
    std::pair<std::string, std::string > topology_keys_for_scaled(const std::vector<double>& scaled);
    std::vector<double> basin_distance_values_for_scaled(const std::vector<double>& scaled_values);
    double basin_distance_between(const std::vector<double>& left, const std::vector<double>& right);
    const std::string& basin_key(const BasinElite& basin);
    const std::string& basin_descriptor_key(const BasinElite& basin);
    const std::string& basin_diversity_key(const BasinElite& basin);
    const std::string& fitness_diversity_key(const CandidateFitness& candidate);
    const std::string& basin_similarity_family_key(const BasinElite& basin);
    double topology_similarity_threshold();
    const CachedParsedTopologyKey& parsed_topology_key_for_similarity(const std::string& key);
    long long unsigned int topology_pair_cache_key(std::uint32_t first, std::uint32_t second);
    bool topology_keys_are_similar(const std::string& left, const std::string& right, double* similarity_out = nullptr);
    bool basin_matches_candidate_diversity(const BasinElite& basin, const std::string& candidate_key, double* similarity_out = nullptr);
    std::string basin_family_group_key(const BasinElite& basin);
    void sort_basins();
    int basin_topology_count();
    std::vector<int> all_basin_indices();
    double basin_topology_distance(const std::string& a, const std::string& b);
    double basin_scaled_distance(const BasinElite& a, const BasinElite& b);
    std::vector<int> select_basin_representative_indices(const std::vector<int>& candidate_indices, int requested_limit, int max_per_topology, double same_key_min_distance, bool trace_feasible_first);
    std::vector<int> select_basin_descriptor_representative_indices(const std::vector<int>& candidate_indices, int requested_limit, int max_per_axis_value, double soft_min_distance, bool trace_feasible_first);
    void trim_basins_to_limit(int requested_limit, bool strict_family_cap = false);
    void trim_basins();
    int full_lazy_basin_trim_limit();
    BasinConsiderResult consider_basin(const std::vector<double>& scaled, const CandidateFitness& candidate_fitness, const std::string& source, bool polished);
    bool active_diversity_enabled();
    bool global_refill_blocked_basin(const BasinElite& basin);
    void rescore_basin_fitness(BasinElite& basin);
    PBestGuideSelection select_basin_archive_guide(int i);
    

    // Convergence
    int basin_qualified_topology_count();
    double early_stop_minimum_improvement(double previous);
    bool early_stop_lpsr_ready();

    // Diagnostics
    std::string normalize_metric_token(std::string text);
    double trace_metric_value(const HybridTraceEvent& trace);
    optiland::HybridTraceEvent emit_trace(const std::string& event, int epoch, double phase_seconds, double epoch_start_merit, double before_merit, int event_de_generations, int event_polish_runs, int event_polish_improvements, int event_refreshed_count, int event_accepted_trials, bool event_used_cuda, int event_polish_merit_strict_improvements = 0, int event_polish_merit_neutral = 0, int event_polish_merit_worse = 0, int event_refresh_archive_used = 0, int event_refresh_effective_count = -1, bool event_refresh_adaptive_skipped = false, double event_refresh_acceptance_rate =
                              std::numeric_limits<double>::quiet_NaN(), double event_refresh_rel_improvement =
                              std::numeric_limits<double>::quiet_NaN(), int event_micro_island_runs = 0, int event_micro_island_improvements = 0, int event_micro_island_archived = 0, int event_micro_island_seed_count = 0, double event_micro_island_seconds = 0.0, double event_micro_island_de_seconds = 0.0, double event_micro_island_polish_seconds = 0.0, int event_micro_island_early_stopped = 0, int event_micro_island_retired = 0, const std::vector<MicroIslandTraceRecord>* event_micro_island_records = nullptr);
    void emit_research_snapshot(int generation);

    // Trace assembly helpers: split_adaptive_diagnostics.py
    void count_trace_feasibility(int& feasible_count, int& trace_feasible_count, int& constraint_feasible_count, int& first_order_feasible_count);
    void assemble_trace_identity(HybridTraceEvent& trace, const std::string& event, int epoch, int event_de_generations);
    void assemble_trace_refresh(HybridTraceEvent& trace, int event_refreshed_count, int event_accepted_trials, int event_refresh_archive_used, int event_refresh_effective_count, bool event_refresh_adaptive_skipped, double event_refresh_acceptance_rate, double event_refresh_rel_improvement);
    void assemble_trace_constraint_guards(HybridTraceEvent& trace);
    void assemble_trace_active_basins(HybridTraceEvent& trace);
    void assemble_trace_active_mating_quota(HybridTraceEvent& trace);
    void assemble_trace_active_niches(HybridTraceEvent& trace);
    void assemble_trace_active_focus(HybridTraceEvent& trace);
    void assemble_trace_local_cohorts(HybridTraceEvent& trace);
    void assemble_trace_active_plateau(HybridTraceEvent& trace);
    void assemble_trace_descriptors(HybridTraceEvent& trace);
    void assemble_trace_population_merits(HybridTraceEvent& trace, int epoch, const int& feasible_count, const int& trace_feasible_count, const int& constraint_feasible_count, const int& first_order_feasible_count);
    void assemble_trace_glass_tolerance(HybridTraceEvent& trace);
    void assemble_trace_basin_merits(HybridTraceEvent& trace);
    void assemble_trace_mutation_controls(HybridTraceEvent& trace);
    void assemble_trace_geometry_memory(HybridTraceEvent& trace);
    void assemble_trace_mating(HybridTraceEvent& trace);
    void assemble_trace_best_metrics(HybridTraceEvent& trace);
    void assemble_trace_cumulative_timing(HybridTraceEvent& trace, double phase_seconds);
    void assemble_trace_polish(HybridTraceEvent& trace, int event_polish_runs, int event_polish_improvements, int event_polish_merit_strict_improvements, int event_polish_merit_neutral, int event_polish_merit_worse);
    void assemble_trace_micro_islands(HybridTraceEvent& trace, int event_micro_island_runs, int event_micro_island_improvements, int event_micro_island_archived, int event_micro_island_seed_count, double event_micro_island_seconds, double event_micro_island_de_seconds, double event_micro_island_polish_seconds, int event_micro_island_early_stopped, int event_micro_island_retired, const std::vector<MicroIslandTraceRecord>* event_micro_island_records);
    void assemble_trace_detailed_timing(HybridTraceEvent& trace);
    void assemble_trace_terminal_state(HybridTraceEvent& trace, double epoch_start_merit, double before_merit, bool event_used_cuda);

    // Epochs
    void run_search_epochs();

    // Evaluation
    optiland::CandidateFitness score_fitness_for_scaled(const std::vector<double>& scaled, int generation, int population_index, const std::string& source, bool polished);
    optiland::CandidateFitness score_fitness_for_scaled_row(const double* scaled_row, int generation, int population_index, const std::string& source, bool polished);
    optiland::CandidateFitness evaluate_fitness_for_scaled(const std::vector<double>& scaled, int generation, int population_index, const std::string& source, bool polished);
    optiland::CandidateFitness scalar_fitness_from_merit(double merit_value, int generation, int population_index, const std::string& source);
    std::vector<optiland::CandidateFitness> evaluate_population_fitness(const std::vector<double>& rows, int rows_count, int generation, const std::string& source);
    void sync_merits();
    const std::string& fitness_descriptor_key(const CandidateFitness& candidate);
    bool global_refill_blocked_fitness(const CandidateFitness& candidate);
    void rescore_cached_fitness(TraceableRefreshArchiveEntry& entry, const std::string& fallback_source);
    MeritStats merit_stats(std::vector<double> values);
    int raw_merit_best_index();

    // Generation
    void run_epoch_de_generations(const RunEpochDeGenerationsContext& frame_);
    void generate_trial_population(const GenerateTrialPopulationContext& frame_);
    void select_trial_population(const SelectTrialPopulationContext& frame_);

    // Geometry Memory
    double geometry_memory_coordinate(const VariableSpec& var, double internal);
    double geometry_memory_effective_distance(const double* scaled, const std::vector<double>& memory);
    double geometry_memory_min_effective_distance(const double* scaled);
    double geometry_memory_key_max_similarity(const std::string& topology_key);
    bool geometry_memory_key_is_exact(const std::string& topology_key);
    bool geometry_memory_key_is_family_matched(const std::string& topology_key);
    int choose_geometry_memory_difference_index(const double* scaled, const std::string& topology_key, PBestGuideStats& stats);
    int population_geometry_memory_difference_index(int population_index, PBestGuideStats& stats);
    int select_geometry_memory_difference_index(int target_index, const PBestGuideSelection& pbest_guide, PBestGuideStats& stats);
    const std::vector<double>* geometry_memory_qohr_family_similarities(const std::string& topology_key);
    bool build_geometry_memory_qohr_step(int target_index, const PBestGuideSelection& pbest_guide, int r0, int r1, int r2_pool, double f, std::vector<double>& step, PBestGuideStats& stats);
    bool geometry_memory_guide_is_near(const PBestGuideSelection& guide, PBestGuideStats& stats);
    PBestGuideSelection apply_geometry_memory_pbest_soft(const PBestGuideSelection& original, const std::vector<int>& order, int pbest_count, PBestGuideStats& stats);

    // Glass Tolerance
    bool glass_proximity_enabled();
    double current_glass_tolerance();
    std::string canonical_adaptive_glass_tolerance_trigger_mode();
    bool adaptive_glass_tolerance_update_trigger_ready(int outer);
    bool adaptive_glass_tolerance_bridge_active();
    void sync_adaptive_glass_tolerance_trigger_state(int outer);
    double glass_distance_for_scaled(const std::vector<double>& scaled);
    double glass_distance_metric(std::vector<double> distances);
    bool adaptive_glass_tolerance_ready();
    bool adaptive_glass_tolerance_stop_ready();
    void update_adaptive_glass_tolerance(int outer, int generation);

    // Initialization
    void configure_runtime();
    void initialize_topology_avoidance();
    void initialize_population_evaluation();
    void initialize_basin_archive();
    void initialize_basin_lifecycle();
    void initialize_search_dynamics();
    void initialize_convergence();
    void initialize_diagnostics();
    void initialize_geometry_memory_mutation();
    void initialize_active_focus();

    // Mating
    bool active_mating_allow_similarity();
    DeMatingSelection draw_global_de_mating(int i, int pbest_population_index);
    DeMatingSelection draw_external_probe_de_mating(int i, int pbest_population_index, const LocalMatingPools& pools, const std::string& family_key);
    LocalMatingPools build_local_mating_pools();
    int draw_population_pool_index_excluding(const std::vector<int>& pool, std::initializer_list<int> excluded);
    const std::vector<int>& local_population_pool(LocalMatingPools& pools, const std::string& key, bool allow_similarity);
    const std::vector<int>& local_archive_pool(LocalMatingPools& pools, const std::string& key, bool allow_similarity);
    std::vector<int> population_pool_in_global_order(const std::vector<int>& candidates, const std::vector<int>& order);
    const std::vector<int>& local_sorted_population_pbest_pool(LocalMatingPools& pools, const std::string& key, bool allow_similarity, const std::vector<int>& order);
    ActiveMatingQuotaState make_active_mating_quota_state(const LocalMatingPools& pools);
    std::string select_local_mating_key(int i, const PBestGuideSelection& pbest_guide, const LocalMatingPools& pools, DeMatingScope scope);
    int draw_local_r2_pool(const std::vector<int>& population_pool, const std::vector<int>& archive_pool, std::initializer_list<int> excluded);
    DeMatingSelection try_draw_local_de_mating(int i, const PBestGuideSelection& pbest_guide, LocalMatingPools& pools, DeMatingScope scope, DeMatingStats& stats);
    DeMatingPlan make_de_mating_plan(int i, LocalMatingPools& pools, ActiveMatingQuotaState& active_quota, DeMatingStats& stats);
    DeMatingSelection select_de_mating(int i, const PBestGuideSelection& pbest_guide, LocalMatingPools& pools, const DeMatingPlan& plan, DeMatingStats& stats);
    BasinRefreshParentPool make_basin_refresh_parent_pool(const bool& top_half_single_surface_refresh, LocalMatingPools& refresh_rebalance_pools, ActiveFamilyCoverage& refresh_rebalance_coverage, bool& refresh_rebalance_coverage_ready);
    void add_population_pool_index(LocalMatingPools& pools, const std::string& key, int idx);

    // Pbest
    int draw_population_index_excluding_list(std::initializer_list<int> excluded);
    PBestGuideSelection no_pbest_guide();
    PBestGuideSelection population_pbest_guide(int index, PBestGuideSource source);
    double pbest_guide_value(const PBestGuideSelection& guide, int dimension_index);
    PBestGuideSelection select_pbest_guide(int i, const std::vector<int>& order, int pbest_count);
    void record_pbest_guide(PBestGuideStats& stats, int i, const PBestGuideSelection& guide);
    PBestGuideSelection select_local_population_pbest_guide(int i, const std::vector<int>& order, LocalMatingPools& pools, DeMatingScope scope);

    // Policy
    int best_index();
    std::vector<int> active_protected_indices();
    bool global_refill_blocked_source(const std::string& source);
    int count_matched_family_keys(const std::vector<std::string>& source_keys, const std::vector<std::string>& target_keys);
    void generate_seed_perturb_refill_rows(int& epoch, int& evict_count, std::vector<ActivePlateauReplacement>& replacements, std::vector<ActivePlateauReplacement>& refill_seed_pool, std::vector<std::vector<double> >& selected_replacement_rows, const double& replacement_min_distance, int target);
    double relative_improvement(double previous, double current);
    double absolute_improvement(double previous, double current);
    int lpsr_horizon_generations();
    double lpsr_progress_fraction();
    int lpsr_target_size();
    double sample_f();
    double sample_classic_f();
    std::string active_target_key_for_index(int idx, const LocalMatingPools& pools);
    std::string active_local_target_key_for_index(int idx, const LocalMatingPools& pools);
    void add_active_local_cohorts(LocalMatingPools& pools, const bool& skip_active_local_cohort);
    void allocate_active_cohort_candidates(LocalMatingPools& pools, std::vector<ActiveFamilyAnchor>& families, const int& family_count);
    bool add_replacement(int& evict_count, std::vector<ActivePlateauReplacement>& replacements, std::vector<std::vector<double> >& selected_replacement_rows, const double& replacement_min_distance, const std::vector<double>& scaled, CandidateFitness candidate, const std::string& source, bool require_trace, bool require_unique, int* accepted_counter);
    bool scaled_rows_equivalent(const std::vector<double>& a, const std::vector<double>& b);
    bool family_is_focus(ActiveNicheGenerationState& state, int niche_id);
    int family_elite_seed_count(ActiveNicheGenerationState& state, int niche_id);
    bool candidate_matches_family(const FocusFillCandidate& candidate, const std::string& family_key);
    int select_target(ActiveNicheGenerationState& state, std::vector<char>& used_target, const std::vector<int>& protected_indices, int niche_id, const std::string& family_key, int candidate_source_index, bool allow_family_replacement);
    bool replace_target(ActiveNicheGenerationState& state, std::vector<char>& used_target, int target, int niche_id, const FocusFillCandidate& candidate, const char* source_tag, bool focus_family);
    
    
    bool protected_index(std::vector<int>& protected_indices, int idx);
    bool replacement_is_duplicate(std::vector<std::vector<double> >& selected_replacement_rows, const double& replacement_min_distance, const std::vector<double>& scaled);
    bool target_protected(const std::vector<int>& protected_indices, int index);
    double local_funnel_perturb_physical(const LensSystem& system, const VariableSpec& var, double value, double scale);
    bool add_unique_scaled_row(std::vector<std::vector<double>>& rows, const std::vector<double>& scaled);
    bool contains_index(const std::vector<int>& values, int needle);
    bool scaled_row_present(const std::vector<std::vector<double>>& rows, const std::vector<double>& scaled);

    // Polish
    std::vector<AdaptivePolishCandidate> select_polish_candidates(int requested_count, const std::vector<int>& excluded_population_indices, const std::vector<std::vector<double>>& excluded_scaled_rows);
    std::vector<AdaptivePolishResult> run_polish_batch(const std::vector<AdaptivePolishCandidate>& polish_candidates);
    MicroIslandSummary run_micro_island_stage(int& outer);
    void refine_micro_island_basins(const RefineMicroIslandBasinsContext& frame_);
    void run_standard_polish_phase(const RunStandardPolishPhaseContext& frame_);
    void fill_standard_polish_attempts(const FillStandardPolishAttemptsContext& frame_);
    std::vector<std::vector<double> > make_micro_local_seed_rows(const LensSystem& system, const std::vector<double>& anchor_physical, int target_seed_rows);
    void accumulate_micro_timing(const DeTimingStats& src);
    void remember_polish_attempt(std::vector<int>& attempted_polish_population_indices, std::vector<std::vector<double> >& attempted_polish_scaled_rows, const AdaptivePolishCandidate& candidate);
    void remember_polish_population_index(std::vector<int>& attempted_polish_population_indices, int population_index);
    void remember_polish_scaled_row(std::vector<std::vector<double> >& attempted_polish_scaled_rows, const std::vector<double>& scaled);

    // Population
    void update_best_from_population();
    bool active_archive_only_enabled();
    void reset_best_from_population_and_incumbent(const std::vector<double>& previous_best_scaled, int generation);
    void trim_external_archive();
    void reduce_population_if_needed();
    int select_same_key_population_index(int i, const std::vector<int>& order, int pbest_count);
    int select_global_population_index(const std::vector<int>& order, int pbest_count, bool avoid_plateau_family);
    bool population_matches_family(int idx, const std::string& family_key);

    // Refresh
    bool refresh_trace_archive_enabled();
    int refresh_trace_archive_capacity();
    std::string refresh_archive_key(const CandidateFitness& fitness);
    void trim_traceable_refresh_archive();
    bool add_traceable_refresh_archive(const std::vector<double>& scaled, CandidateFitness fitness, const std::string& source);
    void stage_traceable_refresh_archive(const std::vector<double>& scaled, CandidateFitness fitness, const std::string& source);
    void drain_traceable_refresh_archive_pending(int target_count);
    std::vector<double> make_refresh_rows(optiland::PopulationGenerationOptions& epoch_refresh_generation, std::vector<int>& refresh_indices, std::vector<int>& refresh_rebalance_target_families, std::vector<int>& refresh_parent_indices, const BasinRefreshParentPool& basin_refresh_parents, int batch, int offset);
    void refresh_epoch_population(const RefreshEpochPopulationContext& frame_);
    void generate_epoch_refresh_candidates(const GenerateEpochRefreshCandidatesContext& frame_);
    void prepare_surface_refresh(const PrepareSurfaceRefreshContext& frame_);
    void rebalance_refresh_family_seeds(const RebalanceRefreshFamilySeedsContext& frame_);
    void replace_refresh_seed_slots(const ReplaceRefreshSeedSlotsContext& frame_);
    void apply_refresh_population(const ApplyRefreshPopulationContext& frame_);
    bool refresh_index_selected(std::vector<int>& refresh_indices, int idx);

    // Result
    OptimizationResult finish_result();
    // Population and basin lookup policies.


    // Population and basin lookup policies.
    bool diversity_keys_match(const std::string& candidate_key, const std::string& target_key, bool allow_similarity);

};
} } // namespace optiland::optimizer_detail
