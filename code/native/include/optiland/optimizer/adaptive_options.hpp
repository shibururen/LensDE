#pragma once
#include "optiland/optimizer/de_options.hpp"
#include "optiland/optimizer/dls_options.hpp"
#include "optiland/optimizer/basins.hpp"
#include "optiland/optimizer/trace.hpp"
#include "optiland/optimizer/de_strategies.hpp"
#include "optiland/optimizer/generation.hpp"
#include "optiland/optimizer/scaling.hpp"
#include <string>
#include <vector>

namespace optiland {
struct AdaptiveHybridDeDlsOptions {
    DifferentialEvolutionOptions de;
    PolishOptions polish;
    AdaptiveDeVariant variant = AdaptiveDeVariant::JadePBestArchiveLpsrFeas;
    ConstraintHandlingMode constraint_handling = ConstraintHandlingMode::FeasibilityRules;
    int max_evaluations = 0;
    int outer_epochs = 6;
    int parent_count = 8;
    int polish_candidate_count = 8;
    int polish_refill_max_extra = -1;
    int polish_workers = 0;
    bool parallel_polish = true;
    bool batched_polish = false;
    bool polish_enabled = true;
    bool polish_topology_balanced = false;
    bool rollback = true;
    bool polish_accept_merit_only = false;
    bool micro_island_enabled = false;
    bool micro_island_replace_polish = true;
    int micro_island_count = 0;
    double micro_island_popsize = 0.0;
    int micro_island_population = 96;
    int micro_island_outer_epochs = 2;
    int micro_island_de_generations = 24;
    double micro_island_seed_scale = 0.65;
    int micro_island_polish_candidate_count = -1;
    int micro_island_polish_refill_max_extra = -1;
    int micro_island_survivor_count = 2;
    int micro_island_family_seed_limit = 24;
    int micro_island_refresh_archive_seed_limit = 24;
    bool micro_island_early_stop_enabled = false;
    bool micro_island_retire_on_early_stop = true;
    int micro_island_early_stop_min_epochs = 3;
    int micro_island_early_stop_window = 1;
    double micro_island_early_stop_min_rel_improve = 0.01;
    double micro_island_early_stop_min_abs_improve = 0.01;
    bool inject_best = true;
    double refresh_fraction = 0.05;
    // Keep the initial DE trajectory intact before the first periodic refresh.
    // A value of 0 preserves the historical behavior.
    int refresh_warmup_generations = 0;
    double pbest_fraction = 0.10;
    PBestGuideScope pbest_scope = PBestGuideScope::Legacy;
    double pbest_basin_rate = 0.70;
    double pbest_same_key_rate = 0.20;
    double pbest_global_rate = 0.05;
    bool pbest_global_fallback = true;
    bool pbest_basin_nearest_fallback = false;
    // Search-only checkpoint memory. Fitness/merit is unchanged: when the selected
    // pbest guide lies inside every active geometry radius, prefer a geometry-novel
    // guide from the same top-pbest quality pool and retain the original as rescue.
    bool geometry_memory_pbest_soft = false;
    // Add a bounded current-minus-history vector to ordinary continuous-geometry
    // DE mutation whenever the target (or its pbest guide) is memory-near.
    // This changes proposals only; merit and trial acceptance remain untouched.
    bool geometry_memory_difference_mutation = false;
    std::vector<std::vector<double>> geometry_memory_values;
    std::vector<std::string> geometry_memory_topology_keys;
    std::vector<double> geometry_memory_merits;
    double geometry_memory_full_radius = 0.10;
    double geometry_memory_skeleton_radius = 0.10;
    double geometry_memory_asphere_radius = 0.16;
    double geometry_memory_difference_strength = 0.50;
    double geometry_memory_same_key_weight = 2.0;
    bool geometry_memory_difference_near_only = true;
    bool geometry_memory_difference_select_nearest = false;
    bool geometry_memory_difference_random_population_anchor = false;
    bool geometry_memory_difference_key_trigger = false;
    double geometry_memory_difference_key_similarity_threshold = 1.0;
    int geometry_memory_difference_key_max_shape_diff = 0;
    bool geometry_memory_difference_orient_outward = false;
    // Quality-orthogonal history rotation (QOHR). In bound-normalized
    // continuous-geometry coordinates, rotate the ordinary JADE random
    // differential only inside the subspace orthogonal to current-to-pbest.
    // Prior rank1 geometry supplies a smooth outward direction; no history
    // vector is added and ordinary selection remains unchanged.
    bool geometry_memory_qohr_mutation = false;
    double geometry_memory_qohr_max_rotation = 0.50;
    double geometry_memory_qohr_backtrack_fraction = 0.25;
    double geometry_memory_qohr_family_weight = 0.0;
    double geometry_memory_qohr_family_similarity_threshold = 0.90;
    int geometry_memory_qohr_family_max_shape_diff = -1;
    double geometry_memory_qohr_quality_full_ratio = 1.10;
    double geometry_memory_qohr_quality_off_ratio = 1.50;
    // Event-driven exact-Key escape. The unpenalized population merit leader
    // must remain in historical exact Keys for the configured residence before
    // a bounded pulse enables exact-Key history differences and novelty exits.
    bool geometry_memory_exact_escape_pulse = false;
    int geometry_memory_exact_escape_residence_generations = 10;
    int geometry_memory_exact_escape_pulse_generations = 10;
    int geometry_memory_exact_escape_cooldown_generations = 50;
    // A2g-G: retain exact-Key and geometry-near intervention, but pulse the
    // non-exact family-Key arm only after best-merit stagnation. During a pulse,
    // redirect family-matched targets toward the most history-novel member of a
    // bounded-quality population pool. Acceptance/merit remain unchanged.
    bool geometry_memory_family_escape_pulse = false;
    int geometry_memory_family_escape_stagnation_generations = 50;
    double geometry_memory_family_escape_min_relative_improvement = 0.005;
    int geometry_memory_family_escape_pulse_generations = 50;
    int geometry_memory_family_escape_cooldown_generations = 50;
    double geometry_memory_novelty_guide_quality_fraction = 0.30;
    bool geometry_memory_plateau_novelty_guide_all_targets = false;
    // During a bounded family-stagnation pulse, keep the evaluation budget
    // fixed but raise DE step/crossover floors to restore trial diversity.
    bool geometry_memory_plateau_parameter_reheat = false;
    double geometry_memory_plateau_reheat_f_floor = 0.75;
    double geometry_memory_plateau_reheat_cr_floor = 0.90;
    bool geometry_memory_plateau_geometry_kick = false;
    double geometry_memory_plateau_geometry_kick_rate = 0.10;
    double geometry_memory_plateau_geometry_kick_scale = 0.08;
    bool geometry_memory_family_escape_novelty_acceptance = false;
    double geometry_memory_family_escape_novelty_max_merit_ratio = 2.0;
    double geometry_memory_family_escape_novelty_min_similarity_drop = 0.05;
    int geometry_memory_family_escape_novelty_max_per_generation = 1;
    bool track_population_diversity = false;
    DeMatingScope de_mating_scope = DeMatingScope::Global;
    double de_mating_local_rate = 0.85;
    int de_mating_min_pool = 8;
    bool de_mating_strict_local = false;
    double archive_size_ratio = 1.0;
    PopulationGenerationOptions refresh_generation;
    // One stronger half-population handoff after two weak 400-generation windows.
    // Disabled unless explicitly requested by the experiment command.
    bool refresh_plateau_pulse_once = false;
    double mu_f = 0.5;
    double mu_cr = 0.8;
    double adaptation_rate = 0.10;
    double f_cauchy_scale = 0.10;
    double cr_normal_sigma = 0.10;
    bool use_lpsr = true;
    int initial_popsize_multiplier = 15;
    // Deprecated compatibility switch. Absolute population requests are now
    // always honored (with the algorithmic minimum of five individuals).
    bool allow_small_initial_population = false;
    int minimum_popsize_multiplier = 4;
    int minimum_population_size = 20;
    int lpsr_horizon_generations = 0;
    bool require_trace_feasible = true;
    bool allow_near_trace_infeasible = false;
    // Default survival policy for CUDA ray-path structural failures: a
    // structural-failure trial cannot replace a parent that has no such
    // fields, even if its aggregate trace_violation is smaller. Use
    // --adaptive-no-hard-reject-structural-failures only for an intentional
    // legacy-policy comparison.
    bool hard_reject_structural_failures = true;
    // Default lexicographic DE survival rule. Once trace/structural safety is
    // comparable, an EFL sign matching MeritConfig::target_efl outranks an
    // opposite/unknown sign. Candidates with the same sign class keep the
    // existing constraint/merit ordering. It can be disabled explicitly for
    // baseline/ablation runs.
    bool efl_sign_priority = true;
    // Optional DE survival guard for small-CRA searches. Inside
    // target+tolerance the normal merit ordering is unchanged. A parent inside
    // the guard cannot be replaced by a trial outside it; when both are
    // outside, a trial that worsens CRA by more than the deadband is rejected.
    // The guard is deliberately opt-in so existing runs remain unchanged.
    bool cra_worsening_guard_enabled = false;
    double cra_worsening_guard_target_deg = 0.0;
    double cra_worsening_guard_tolerance_deg = 0.0;
    double cra_worsening_guard_deadband_deg = 0.0;
    // Optional parent-lineage guard for preserving an already discovered
    // joint CRA/distortion bridge while its image quality is refined. A slot
    // latches an anchor only after both metrics enter their activation window.
    // Normal candidate ordering remains in force inside the anchor loss
    // budgets; a would-be accepted trial outside either budget is vetoed.
    bool joint_constraint_anchor_guard_enabled = false;
    double joint_constraint_anchor_cra_target_deg = 0.0;
    double joint_constraint_anchor_cra_tolerance_deg = 0.0;
    double joint_constraint_anchor_cra_loss_budget_deg = 0.0;
    double joint_constraint_anchor_distortion_target = 0.0;
    double joint_constraint_anchor_distortion_tolerance = 0.0;
    double joint_constraint_anchor_distortion_loss_budget = 0.0;
    bool treat_first_order_as_feasibility = true;
    double feasibility_epsilon = 0.0;
    double trace_coverage_epsilon = 1.0e-9;
    double constraint_epsilon = 1.0e-9;
    double first_order_epsilon = 1.0e-9;
    BasinArchiveOptions basins;
    bool topology_summary_enabled = true;
    bool early_stop_enabled = false;
    std::string early_stop_metric = "basin_unique_topology_elite_mean_merit";
    int early_stop_min_epochs = 3;
    int early_stop_window = 1;
    int early_stop_min_topologies = 4;
    double early_stop_min_rel_improve = 0.03;
    double early_stop_min_abs_improve = 0.01;
    // Opt-in baseline at generation zero.  This makes an N-generation
    // plateau window exact instead of waiting for one extra epoch before the
    // first comparison.
    bool early_stop_include_initial_point = false;
    // The historical policy uses the stricter (larger) of the relative and
    // absolute improvement thresholds.  A final convergence tail may opt into
    // the smaller threshold without changing earlier exploration stages.
    bool early_stop_use_min_improvement_threshold = false;
    double early_stop_min_lpsr_progress = 0.0;
    int early_stop_tail_epochs = 0;
    bool early_stop_require_topology_plateau = false;
    double early_stop_best_merit_max = 0.0;
    bool early_stop_use_qualified_topologies = false;
    std::string early_stop_qualified_topology_mode = "first_order";
    double early_stop_qualified_topology_first_order_epsilon = -1.0;
    double early_stop_qualified_topology_merit_ratio = 3.0;
    double early_stop_qualified_topology_merit_floor = 0.0;
    double early_stop_qualified_topology_merit_delta = 125.0;
    double early_stop_qualified_topology_merit_max = 0.0;
    bool adaptive_glass_tolerance_enabled = false;
    std::string adaptive_glass_tolerance_trigger_mode = "warmup";
    int adaptive_glass_tolerance_focus_clear_target = 0;
    bool adaptive_glass_tolerance_stop_when_ready = false;
    bool adaptive_glass_tolerance_bridge_freeze_focus = true;
    int adaptive_glass_tolerance_warmup_epochs = 2;
    double adaptive_glass_tolerance_elite_fraction = 0.20;
    double adaptive_glass_tolerance_quantile = 0.0;
    double adaptive_glass_tolerance_margin = 0.50;
    double adaptive_glass_tolerance_floor = 0.05;
    double adaptive_glass_tolerance_smoothing = 0.75;
    double adaptive_glass_tolerance_decay = 0.75;
    double adaptive_glass_tolerance_early_stop_max = 0.50;
    std::vector<std::string> avoided_topology_keys;
    // Exact/prefix-only history.  These keys participate in duplicate
    // rejection but are intentionally excluded from structural-family
    // similarity matching, so a high-quality historical topology does not
    // suppress distinct neighbours in the same optical family.
    std::vector<std::string> avoided_topology_exact_only_keys;
    double avoided_topology_penalty = 0.0;
    int avoided_topology_start_generation = 0;
    double avoided_topology_similarity_threshold = 0.0;
    int avoided_topology_max_shape_diff = -1;
    bool avoided_topology_continuous_structure = false;
    double avoided_topology_continuous_power_cosine_min = 0.99;
    double avoided_topology_continuous_power_distance_max = 0.18;
    double avoided_topology_continuous_curvature_cosine_min = 0.97;
    double avoided_topology_continuous_curvature_distance_max = 0.25;
    double avoided_topology_continuous_thickness_distance_max = 0.10;
    double avoided_topology_continuous_gap_distance_max = 0.10;
    // Sign flips below this fraction of the vector RMS are treated as weak
    // features; stronger curvature/power sign flips keep structures distinct.
    double avoided_topology_continuous_significance_fraction = 0.08;
    // A single weighted parameter score is used only when the raw curvature
    // sign skeleton is not exactly equal. Legacy per-axis thresholds above
    // remain parse-compatible but are no longer conjunctive match gates.
    double avoided_topology_parameter_auxiliary_similarity_min = 0.90;
    double family_transition_similarity_threshold = 0.90;
    int family_transition_max_shape_diff = 2;
    FamilyTransitionAuditCallback family_transition_audit;
    bool write_adaptive_trace = true;
    HybridTraceCallback trace;
    InternalScalingMode internal_scaling = InternalScalingMode::BoundNormalized;
    // Per-run research snapshots. Empty preserves the historical environment
    // variable behavior and therefore does not change the production default.
    std::string checkpoint_snapshot_dir;
};


}  // namespace optiland
