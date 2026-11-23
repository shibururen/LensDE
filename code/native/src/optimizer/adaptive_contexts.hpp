#pragma once
#include "adaptive_types.hpp"

namespace optiland { namespace optimizer_detail {
struct RunEpochDeGenerationsContext {
    int& outer;
    PBestGuideStats& epoch_pbest_stats;
    DeMatingStats& epoch_mating_stats;
    int& epoch_de_generations;
    int& epoch_accepted_trials;
    int& epoch_retired_trial_reject_count;
    const bool& family_escape_configured;
    const bool& exact_escape_configured;
    int& exact_escape_resident_generations;
    int& exact_escape_pulse_remaining;
    int& exact_escape_cooldown_remaining;
    std::string& exact_escape_resident_key;
    int& family_escape_stagnant_generations;
    int& family_escape_pulse_remaining;
    int& family_escape_cooldown_remaining;
    double& family_escape_reference_merit;
    std::vector<DeferredNicheCandidate>& deferred_niche_candidates;
};

struct GenerateTrialPopulationContext {
    PBestGuideStats& epoch_pbest_stats;
    DeMatingStats& epoch_mating_stats;
    const std::vector<int>& order;
    const int& pbest_count;
    int& geometry_memory_novelty_guide_index;
    double& geometry_memory_novelty_guide_similarity;
    const bool& geometry_memory_novelty_escape_active;
    std::vector<double>& generation_best_scaled;
    LocalMatingPools& local_mating_pools;
    ActiveMatingQuotaState& active_mating_quota;
    std::vector<double>& trials;
    std::vector<double>& sampled_f;
    std::vector<double>& sampled_cr;
    std::vector<double>& geometry_memory_qohr_step;
    
    
};

struct SelectTrialPopulationContext {
    int& outer;
    PBestGuideStats& epoch_pbest_stats;
    int& epoch_retired_trial_reject_count;
    std::vector<DeferredNicheCandidate>& deferred_niche_candidates;
    ActiveNicheGenerationState& active_niche_state;
    int& active_niche_floor_blocked;
    int& active_niche_family_cap_blocked;
    std::vector<double>& trials;
    std::vector<double>& sampled_f;
    std::vector<double>& sampled_cr;
    std::vector<optiland::CandidateFitness>& trial_fitness;
    
    std::vector<double>& success_f;
    std::vector<double>& success_cr;
    std::vector<double>& success_delta;
    int& accepted;
    double& selection_family_gate_seconds;
    double& selection_compare_seconds;
    double& selection_update_seconds;
    double& selection_basin_seconds;
    int& audit_trial_same_topology_count;
    int& audit_trial_within_family_count;
    int& audit_trial_cross_family_count;
    int& audit_trial_unknown_topology_count;
    int& audit_accepted_same_topology_count;
    int& audit_accepted_within_family_count;
    int& audit_accepted_cross_family_count;
    int& audit_accepted_unknown_topology_count;
    int& exact_escape_generation_crossing_accepted;
    int& family_escape_generation_crossing_accepted;
    int& family_escape_generation_novelty_accepted;
};

struct ApplyDeferredNicheSurvivalContext {
    std::vector<DeferredNicheCandidate>& deferred_niche_candidates;
    int& gen;
    ActiveNicheGenerationState& active_niche_state;
    int& active_niche_repair_moves;
    std::vector<double>& success_f;
    std::vector<double>& success_cr;
    std::vector<double>& success_delta;
    int& accepted;
};

struct RefineMicroIslandBasinsContext {
    int& outer;
    MicroIslandSummary& summary;
    const int& micro_effective_variable_count;
    const int& resolved_micro_population;
    std::vector<optiland::BasinElite>& source_basins;
    std::vector<std::string >& micro_retired_keys;
    std::vector<std::vector<double> >& micro_retired_scaled_rows;
    const bool& micro_retire_allow_similarity;
};

struct RunStandardPolishPhaseContext {
    std::vector<AdaptivePolishCandidate>& all_polish_candidates;
    std::vector<int>& attempted_polish_population_indices;
    std::vector<std::vector<double> >& attempted_polish_scaled_rows;
    int& epoch_polish_improvements;
    int& epoch_polish_merit_strict_improvements;
    int& epoch_polish_merit_neutral;
    int& epoch_polish_merit_worse;
    bool& epoch_polish_used_cuda;
    const int& epoch_requested_polish_count;
    const int& epoch_max_polish_attempts;
    const bool& run_standard_polish;
};

struct FillStandardPolishAttemptsContext {
    std::vector<AdaptivePolishCandidate>& all_polish_candidates;
    std::vector<int>& attempted_polish_population_indices;
    std::vector<std::vector<double> >& attempted_polish_scaled_rows;
    int& epoch_polish_improvements;
    int& epoch_polish_merit_strict_improvements;
    int& epoch_polish_merit_neutral;
    int& epoch_polish_merit_worse;
    bool& epoch_polish_used_cuda;
    const int& epoch_requested_polish_count;
    const int& epoch_max_polish_attempts;
};

struct RefreshEpochPopulationContext {
    int& outer;
    DeMatingStats& epoch_mating_stats;
    const double& epoch_start_merit;
    int& epoch_de_generations;
    int& epoch_accepted_trials;
    std::vector<int>& attempted_polish_population_indices;
    int& refreshed_count;
    int& refresh_archive_used;
    int& epoch_refresh_effective_count;
    bool& epoch_refresh_adaptive_skipped;
    double& epoch_refresh_acceptance_rate;
    double& epoch_refresh_rel_improvement;
};

struct GenerateEpochRefreshCandidatesContext {
    int& outer;
    DeMatingStats& epoch_mating_stats;
    const double& epoch_start_merit;
    std::vector<int>& attempted_polish_population_indices;
    int& refreshed_count;
    int& refresh_archive_used;
    const int& refresh_target;
    int& effective_refresh_target;
    const double& refresh_acceptance_rate;
    const double& before_refresh_merit;
    const double& refresh_rel_improvement;
    const bool& plateau_pulse;
    optiland::PopulationGenerationOptions& epoch_refresh_generation;
    bool& refresh_adaptive_skipped;
};

struct PrepareSurfaceRefreshContext {
    DeMatingStats& epoch_mating_stats;
    int& effective_refresh_target;
    const std::vector<int>& order;
    const bool& top_half_single_surface_refresh;
    std::vector<int>& protected_indices;
    std::vector<int>& refresh_indices;
    LocalMatingPools& refresh_rebalance_pools;
    ActiveFamilyCoverage& refresh_rebalance_coverage;
    bool& refresh_rebalance_coverage_ready;
    std::vector<int>& refresh_rebalance_target_families;
};

struct RebalanceRefreshFamilySeedsContext {
    DeMatingStats& epoch_mating_stats;
    int& effective_refresh_target;
    const std::vector<int>& order;
    std::vector<int>& protected_indices;
    std::vector<int>& refresh_indices;
    LocalMatingPools& refresh_rebalance_pools;
    ActiveFamilyCoverage& refresh_rebalance_coverage;
    bool& refresh_rebalance_coverage_ready;
    std::vector<int>& refresh_rebalance_target_families;
};

struct ReplaceRefreshSeedSlotsContext {
    int& effective_refresh_target;
    const std::vector<int>& order;
    std::vector<int>& protected_indices;
    std::vector<int>& refresh_indices;
    LocalMatingPools& refresh_rebalance_pools;
    ActiveFamilyCoverage& refresh_rebalance_coverage;
    bool& refresh_rebalance_coverage_ready;
    std::vector<int>& refresh_rebalance_target_families;
    const double& local_fraction;
};

struct ApplyRefreshPopulationContext {
    int& refreshed_count;
    int& refresh_archive_used;
    const double& before_refresh_merit;
    const bool& plateau_pulse;
    optiland::PopulationGenerationOptions& epoch_refresh_generation;
    const Clock::time_point& refresh_start;
    const std::vector<int>& order;
    const bool& top_half_single_surface_refresh;
    std::vector<int>& refresh_indices;
    LocalMatingPools& refresh_rebalance_pools;
    ActiveFamilyCoverage& refresh_rebalance_coverage;
    bool& refresh_rebalance_coverage_ready;
    std::vector<int>& refresh_rebalance_target_families;
};

} }
