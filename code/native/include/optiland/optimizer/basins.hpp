#pragma once
#include "optiland/optimizer/scaling.hpp"
#include "optiland/optimizer/generation.hpp"
#include "optiland/optimizer/fitness.hpp"
#include <limits>
#include <string>
#include <vector>

namespace optiland {
struct BasinArchiveOptions {
    bool enabled = true;
    int max_basins = 8;
    double radius = 0.05;
    bool include_near_feasible = true;
    bool polish_basins = true;
    int output_count = 8;
    DistanceMetric distance_metric = DistanceMetric::BoundNormalized;
    // Include conic terms when structure distance is used to distinguish
    // geometry basins; the default preserves the historical structure metric.
    bool distance_include_conic = false;
    double air_thickness_distance_cap = 25.0;
    bool active_diversity = false;
    int active_count = 4;
    std::string active_selection_mode = "diversity";
    double protected_fraction = 0.15;
    bool active_quality_gate = true;
    bool active_require_trace_feasible = true;
    double active_min_edge = 0.0;
    double active_max_merit_ratio = 3.0;
    double active_max_merit_delta = 125.0;
    int active_max_slots_per_family = 1;
    std::string active_mating_match_mode = "exact_key";
    double active_family_similarity_threshold = 0.80;
    int active_family_max_shape_diff = 3;
    bool active_family_parameter_merge_enabled = false;
    double active_family_parameter_merge_distance = 0.15;
    DistanceMetric active_family_parameter_merge_metric =
        DistanceMetric::StructureBoundNormalized;
    bool active_archive_only = false;
    bool active_slot_strict_frozen_family = false;
    bool active_local_cohort = false;
    int active_local_cohort_size = 0;
    double active_local_cohort_fraction = 0.35;
    double active_local_cohort_max_distance = 0.0;
    bool active_mating_quota = false;
    bool active_niche_tracking = false;
    int active_niche_start_epoch = 1;
    bool active_niche_survival = false;
    int active_niche_min_quota = 0;
    int active_niche_target_quota = 0;
    double active_niche_family_cap_fraction = 0.15;
    int active_niche_repair_interval = 25;
    double active_niche_repair_fraction = 0.03;
    bool active_niche_rebalance = false;
    int active_niche_rebalance_interval = 25;
    double active_niche_rebalance_fraction = 0.05;
    bool active_niche_soft_fill = false;
    int active_niche_elite_seed_count = 1;
    bool active_focus_rebalance = false;
    int active_focus_start_epoch = 1;
    bool active_focus_initial_best_family = true;
    int active_focus_family_count = 1;
    double active_focus_population_fraction = 0.55;
    int active_focus_min_quota = 0;
    bool active_focus_soft_fill = false;
    int active_focus_elite_seed_count = 1;
    int active_monitor_family_cap = 0;
    double active_focus_stop_rel_improvement = 0.20;
    double active_focus_ready_fraction = 0.70;
    int active_focus_cooldown_epochs = 2;
    int active_focus_stop_after_clears = 0;
    bool active_focus_rotation = false;
    int active_focus_rotation_history_epochs = 8;
    double active_focus_rotation_similarity_threshold = 0.80;
    bool active_focus_stop_release = true;
    double active_focus_stop_release_fraction = 1.0;
    bool active_refresh_rebalance = false;
    double active_refresh_max_family_fraction = 0.0;
    double active_refresh_min_local_fraction = 0.80;
    bool active_plateau_lifecycle = false;
    bool active_plateau_track_active_niches = false;
    int active_plateau_window = 2;
    double active_plateau_min_rel_improvement = 0.005;
    double active_plateau_local_quota_scale = 0.35;
    double active_plateau_external_probe_rate = 0.15;
    int active_plateau_retire_after = 4;
    int active_plateau_cooldown_epochs = 2;
    int active_plateau_frontier_count = 1;
    double active_plateau_frontier_multiplier = 0.0;
    bool active_plateau_retire_evict = false;
    double active_plateau_retire_evict_fraction = 0.0;
    double active_plateau_retire_evict_max_fraction = 0.20;
    int active_plateau_retire_evict_min = 0;
    bool active_plateau_retire_evict_parameter_only = false;
    double active_plateau_retire_evict_parameter_distance = 0.0;
    bool active_plateau_retire_frontier_on_first_update = false;
    int active_plateau_retire_defer_early_stop_epochs = 0;
    int active_plateau_retire_stop_after_clears = 0;
    bool balanced_refresh = true;
    bool semantic_diversity = true;
    int semantic_max_per_topology = 2;
    TopologyDescriptorOptions topology_descriptor;
};

struct BasinElite {
    int id = 0;
    std::vector<double> scaled_values;
    std::vector<double> distance_values;
    std::vector<double> physical_values;
    CandidateFitness fitness;
    double nearest_distance = std::numeric_limits<double>::infinity();
    int generation = 0;
    int polish_count = 0;
    std::string source;
    std::string topology_key;
    std::string semantic_key;
    std::string descriptor_key;
    std::string descriptor_diagnostic;
    std::vector<std::uint64_t> descriptor_axis_values;
    int similarity_family_id = 0;
    std::string similarity_family_key;
    double similarity_to_family = std::numeric_limits<double>::quiet_NaN();
    bool active_eligible = true;
    std::string active_reject_reason;
};


}  // namespace optiland
