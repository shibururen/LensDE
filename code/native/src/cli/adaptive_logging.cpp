// Print the adaptive run configuration by population, Memory, constraints, basins, and termination.
// These helpers describe options already assembled by OptimizationCommand before execution.
// Formatting must not introduce new defaults or mutate the scientific options being reported.

#include "optimization_command.hpp"

namespace lensde { namespace cli { namespace detail {

void OptimizationCommand::print_adaptive_population() {
    std::cout
        << "Variables: "
        << variables.size()
        << ", adaptive population: "
        << (adaptive_options.de.use_popsize_multiplier
         ? adaptive_options.initial_popsize_multiplier *
               population_sizing_dimension(variables)
         : adaptive_options.de.population_size)
        << ", min population: "
        << std::max(adaptive_options.minimum_population_size,
             adaptive_options.minimum_popsize_multiplier *
                 population_sizing_dimension(variables))
        << ", hybrid outer: "
        << adaptive_options.outer_epochs
        << ", de inner generations: "
        << adaptive_options.de.max_generations
        << ", DLS candidates: "
        << adaptive_options.polish_candidate_count
        << ", DLS refill max extra: "
        << (adaptive_options.polish_refill_max_extra >= 0
         ? adaptive_options.polish_refill_max_extra
         : adaptive_options.polish_candidate_count)
        << ", DLS iterations: "
        << adaptive_options.polish.max_iterations
        << ", DLS residuals: "
        << adaptive_options.polish.residual_mode
        << ", DLS polish: "
        << (adaptive_options.polish_enabled ? "enabled" : "disabled")
        << ", DLS selection: "
        << (adaptive_options.polish_topology_balanced
         ? "topology_balanced"
         : "merit");
}

void OptimizationCommand::print_adaptive_micro_islands() {
    std::cout
        << ", micro-island: "
        << (adaptive_options.micro_island_enabled ? "enabled" : "disabled")
        << ", micro-island mode: "
        << (adaptive_options.micro_island_replace_polish
         ? "replace_polish"
         : "keep_polish")
        << ", micro-island count/popsize/pop/outer/gen/survivors: "
        << adaptive_options.micro_island_count
        << "/"
        << adaptive_options.micro_island_popsize
        << "/"
        << adaptive_options.micro_island_population
        << "/"
        << adaptive_options.micro_island_outer_epochs
        << "/"
        << adaptive_options.micro_island_de_generations
        << "/"
        << adaptive_options.micro_island_survivor_count
        << ", micro-island seed scale: "
        << adaptive_options.micro_island_seed_scale
        << ", micro-island DLS candidates/refill: "
        << adaptive_options.micro_island_polish_candidate_count
        << "/"
        << adaptive_options.micro_island_polish_refill_max_extra
        << ", micro-island early stop: "
        << (adaptive_options.micro_island_early_stop_enabled ? "yes" : "no")
        << " min/window/rel/abs="
        << adaptive_options.micro_island_early_stop_min_epochs
        << "/"
        << adaptive_options.micro_island_early_stop_window
        << "/"
        << adaptive_options.micro_island_early_stop_min_rel_improve
        << "/"
        << adaptive_options.micro_island_early_stop_min_abs_improve
        << ", retire-on-stop: "
        << (adaptive_options.micro_island_retire_on_early_stop ? "yes" : "no")
        << ", batched DLS polish: "
        << (adaptive_options.batched_polish ? "yes" : "no")
        << ", DLS accept: "
        << (!adaptive_options.rollback
         ? "no_rollback"
         : (adaptive_options.polish_accept_merit_only
                ? "merit_only"
                : "candidate_better"))
        << ", variant: "
        << adaptive_de_variant_name(adaptive_options.variant);
}

void OptimizationCommand::print_adaptive_memory() {
    std::cout
        << ", pbest scope: "
        << pbest_guide_scope_name(adaptive_options.pbest_scope)
        << ", pbest rates basin/same/global: "
        << adaptive_options.pbest_basin_rate
        << "/"
        << adaptive_options.pbest_same_key_rate
        << "/"
        << adaptive_options.pbest_global_rate
        << ", pbest global fallback: "
        << (adaptive_options.pbest_global_fallback ? "yes" : "no")
        << ", pbest basin nearest fallback: "
        << (adaptive_options.pbest_basin_nearest_fallback ? "yes" : "no")
        << ", geometry memory pbest soft/refs: "
        << (adaptive_options.geometry_memory_pbest_soft ? "yes" : "no")
        << "/"
        << adaptive_options.geometry_memory_values.size()
        << ", geometry memory radii full/skeleton/asphere: "
        << adaptive_options.geometry_memory_full_radius
        << "/"
        << adaptive_options.geometry_memory_skeleton_radius
        << "/"
        << adaptive_options.geometry_memory_asphere_radius
        << ", geometry memory exact escape pulse/residence/burst/cooldown: "
        << (adaptive_options.geometry_memory_exact_escape_pulse
         ? "yes"
         : "no")
        << "/"
        << adaptive_options
        .geometry_memory_exact_escape_residence_generations
        << "/"
        << adaptive_options
        .geometry_memory_exact_escape_pulse_generations
        << "/"
        << adaptive_options
        .geometry_memory_exact_escape_cooldown_generations
        << ", geometry memory family escape pulse/stagnation/burst/cooldown/quality: "
        << (adaptive_options.geometry_memory_family_escape_pulse ? "yes" : "no")
        << "/"
        << adaptive_options.geometry_memory_family_escape_stagnation_generations
        << "/"
        << adaptive_options.geometry_memory_family_escape_pulse_generations
        << "/"
        << adaptive_options.geometry_memory_family_escape_cooldown_generations
        << "/"
        << adaptive_options.geometry_memory_novelty_guide_quality_fraction
        << ", plateau novelty guide all targets: "
        << (adaptive_options
             .geometry_memory_plateau_novelty_guide_all_targets
         ? "yes"
         : "no")
        << ", plateau parameter reheat/F-floor/CR-floor: "
        << (adaptive_options.geometry_memory_plateau_parameter_reheat
         ? "yes"
         : "no")
        << "/"
        << adaptive_options.geometry_memory_plateau_reheat_f_floor
        << "/"
        << adaptive_options.geometry_memory_plateau_reheat_cr_floor
        << ", plateau geometry kick/rate/scale: "
        << (adaptive_options.geometry_memory_plateau_geometry_kick
         ? "yes"
         : "no")
        << "/"
        << adaptive_options.geometry_memory_plateau_geometry_kick_rate
        << "/"
        << adaptive_options.geometry_memory_plateau_geometry_kick_scale
        << ", family escape novelty acceptance/merit-ratio/sim-drop/quota: "
        << (adaptive_options.geometry_memory_family_escape_novelty_acceptance
         ? "yes"
         : "no")
        << "/"
        << adaptive_options.geometry_memory_family_escape_novelty_max_merit_ratio
        << "/"
        << adaptive_options.geometry_memory_family_escape_novelty_min_similarity_drop
        << "/"
        << adaptive_options.geometry_memory_family_escape_novelty_max_per_generation
        << ", DE mating scope: "
        << de_mating_scope_name(adaptive_options.de_mating_scope)
        << ", DE mating local rate/min pool: "
        << adaptive_options.de_mating_local_rate
        << "/"
        << adaptive_options.de_mating_min_pool
        << ", DE mating strict local pbest: "
        << (adaptive_options.de_mating_strict_local ? "yes" : "no");
}

void OptimizationCommand::print_adaptive_constraints() {
    std::cout
        << ", constraint handling: "
        << constraint_handling_name(adaptive_options.constraint_handling)
        << ", EFL sign priority/target: "
        << (adaptive_options.efl_sign_priority ? "yes" : "no")
        << "/"
        << merit.target_efl
        << ", CRA worsening guard target/tolerance/deadband: "
        << (adaptive_options.cra_worsening_guard_enabled ? "yes/" : "no/")
        << adaptive_options.cra_worsening_guard_target_deg
        << "/"
        << adaptive_options.cra_worsening_guard_tolerance_deg
        << "/"
        << adaptive_options.cra_worsening_guard_deadband_deg
        << ", joint constraint anchor CRA target/tol/loss: "
        << (adaptive_options.joint_constraint_anchor_guard_enabled
         ? "yes/"
         : "no/")
        << adaptive_options.joint_constraint_anchor_cra_target_deg
        << "/"
        << adaptive_options.joint_constraint_anchor_cra_tolerance_deg
        << "/"
        << adaptive_options.joint_constraint_anchor_cra_loss_budget_deg
        << ", distortion target/tol/loss: "
        << adaptive_options.joint_constraint_anchor_distortion_target
        << "/"
        << adaptive_options.joint_constraint_anchor_distortion_tolerance
        << "/"
        << adaptive_options.joint_constraint_anchor_distortion_loss_budget
        << ", search purpose: "
        << adaptive_search_purpose
        << ", first-order hard gate: "
        << (adaptive_options.treat_first_order_as_feasibility ? "yes" : "no")
        << ", adaptive DE CUDA: "
        << (adaptive_options.de.use_cuda ? "yes" : "no")
        << ", glass search: "
        << (glass_search.enabled ? "yes" : "no");
}

void OptimizationCommand::print_adaptive_generation() {
    std::cout
        << ", refresh fraction: "
        << adaptive_options.refresh_fraction
        << ", initial generation: "
        << population_generation_mode_name(adaptive_options.de.initial_generation.mode)
        << ", refresh generation: "
        << population_generation_mode_name(adaptive_options.refresh_generation.mode)
        << ", refresh trace gate: "
        << (adaptive_options.allow_near_trace_infeasible
         ? "near"
         : (adaptive_options.refresh_generation.trace_feasible_gate ? "yes" : "no"))
        << ", refresh seed pool: "
        << adaptive_options.refresh_generation.seed_pool_values.size()
        << " @ "
        << adaptive_options.refresh_generation.seed_pool_fraction
        << ", seed perturb: "
        << adaptive_options.refresh_generation.seed_pool_perturbation
        << ", refresh trace archive: "
        << (adaptive_options.refresh_generation.trace_archive_enabled ? "yes" : "no")
        << " @ "
        << adaptive_options.refresh_generation.trace_archive_fraction
        << ", archive cap: "
        << adaptive_options.refresh_generation.trace_archive_capacity
        << ", adaptive refresh: "
        << (adaptive_options.refresh_generation.adaptive_trigger_enabled ? "yes" : "no")
        << ", max fraction: "
        << adaptive_options.refresh_generation.adaptive_max_fraction;
}

void OptimizationCommand::print_adaptive_basins() {
    std::cout
        << ", basin max: "
        << adaptive_options.basins.max_basins
        << ", basin radius: "
        << adaptive_options.basins.radius
        << ", basin distance: "
        << distance_metric_name(adaptive_options.basins.distance_metric)
        << ", basin air cap: "
        << adaptive_options.basins.air_thickness_distance_cap
        << ", active basin diversity: "
        << (adaptive_options.basins.active_diversity ? "yes" : "no")
        << ", active basins: "
        << adaptive_options.basins.active_count
        << ", active basin selection: "
        << adaptive_options.basins.active_selection_mode
        << ", basin protected fraction: "
        << adaptive_options.basins.protected_fraction
        << ", active basin quality gate: "
        << (adaptive_options.basins.active_quality_gate ? "yes" : "no")
        << ", active min edge: "
        << adaptive_options.basins.active_min_edge
        << ", active merit ratio/delta: "
        << adaptive_options.basins.active_max_merit_ratio
        << "/"
        << adaptive_options.basins.active_max_merit_delta
        << ", active slots/family: "
        << adaptive_options.basins.active_max_slots_per_family
        << ", active mating match: "
        << adaptive_options.basins.active_mating_match_mode
        << ", active family similarity threshold: "
        << adaptive_options.basins.active_family_similarity_threshold
        << ", active family max shape diff: "
        << adaptive_options.basins.active_family_max_shape_diff
        << ", active archive-only: "
        << (adaptive_options.basins.active_archive_only ? "yes" : "no")
        << ", active slot strict frozen family: "
        << (adaptive_options.basins.active_slot_strict_frozen_family ? "yes" : "no")
        << ", active local cohort: "
        << (adaptive_options.basins.active_local_cohort ? "yes" : "no")
        << " @ size/fraction/max-distance "
        << adaptive_options.basins.active_local_cohort_size
        << "/"
        << adaptive_options.basins.active_local_cohort_fraction
        << "/"
        << adaptive_options.basins.active_local_cohort_max_distance
        << ", active mating quota: "
        << (adaptive_options.basins.active_mating_quota ? "yes" : "no");
}

void OptimizationCommand::print_adaptive_niches() {
    std::cout
        << ", active niche tracking/survival: "
        << (adaptive_options.basins.active_niche_tracking ? "yes" : "no")
        << "/"
        << (adaptive_options.basins.active_niche_survival ? "yes" : "no")
        << " start-epoch "
        << adaptive_options.basins.active_niche_start_epoch
        << ", active niche q_min/q_target/cap/repair/rebalance: "
        << adaptive_options.basins.active_niche_min_quota
        << "/"
        << adaptive_options.basins.active_niche_target_quota
        << "/"
        << adaptive_options.basins.active_niche_family_cap_fraction
        << "/"
        << adaptive_options.basins.active_niche_repair_interval
        << "@"
        << adaptive_options.basins.active_niche_repair_fraction
        << "/"
        << (adaptive_options.basins.active_niche_rebalance ? "yes" : "no")
        << "@"
        << adaptive_options.basins.active_niche_rebalance_interval
        << "@"
        << adaptive_options.basins.active_niche_rebalance_fraction
        << " soft-fill/seeds "
        << (adaptive_options.basins.active_niche_soft_fill ? "yes" : "no")
        << "/"
        << adaptive_options.basins.active_niche_elite_seed_count
        << ", active focus rebalance: "
        << (adaptive_options.basins.active_focus_rebalance ? "yes" : "no")
        << " @ start/best-family/families/pop-frac/focus-min/monitor-cap "
        << adaptive_options.basins.active_focus_start_epoch
        << "/"
        << (adaptive_options.basins.active_focus_initial_best_family ? "yes" : "no")
        << "/"
        << adaptive_options.basins.active_focus_family_count
        << "/"
        << adaptive_options.basins.active_focus_population_fraction
        << "/"
        << adaptive_options.basins.active_focus_min_quota
        << "/"
        << adaptive_options.basins.active_monitor_family_cap
        << " soft-fill/seeds "
        << (adaptive_options.basins.active_focus_soft_fill ? "yes" : "no")
        << "/"
        << adaptive_options.basins.active_focus_elite_seed_count
        << " stop/ready/cooldown "
        << adaptive_options.basins.active_focus_stop_rel_improvement
        << "/"
        << adaptive_options.basins.active_focus_ready_fraction
        << "/"
        << adaptive_options.basins.active_focus_cooldown_epochs
        << " stop-after-clears "
        << adaptive_options.basins.active_focus_stop_after_clears
        << " rotation "
        << (adaptive_options.basins.active_focus_rotation ? "yes" : "no")
        << "@"
        << adaptive_options.basins.active_focus_rotation_history_epochs
        << "@"
        << adaptive_options.basins.active_focus_rotation_similarity_threshold
        << " stop-release "
        << (adaptive_options.basins.active_focus_stop_release ? "yes" : "no")
        << "@"
        << adaptive_options.basins.active_focus_stop_release_fraction;
}

void OptimizationCommand::print_adaptive_plateau() {
    std::cout
        << ", active refresh rebalance: "
        << (adaptive_options.basins.active_refresh_rebalance ? "yes" : "no")
        << " @ family fraction/local trigger "
        << adaptive_options.basins.active_refresh_max_family_fraction
        << "/"
        << adaptive_options.basins.active_refresh_min_local_fraction
        << ", active plateau lifecycle: "
        << (adaptive_options.basins.active_plateau_lifecycle ? "yes" : "no")
        << " @ window/min-rel/quota/probe/retire/cooldown "
        << adaptive_options.basins.active_plateau_window
        << "/"
        << adaptive_options.basins.active_plateau_min_rel_improvement
        << "/"
        << adaptive_options.basins.active_plateau_local_quota_scale
        << "/"
        << adaptive_options.basins.active_plateau_external_probe_rate
        << "/"
        << adaptive_options.basins.active_plateau_retire_after
        << "/"
        << adaptive_options.basins.active_plateau_cooldown_epochs
        << " track-active="
        << (adaptive_options.basins.active_plateau_track_active_niches
         ? "yes"
         : "no")
        << ", frontier count/multiplier "
        << adaptive_options.basins.active_plateau_frontier_count
        << "/"
        << adaptive_options.basins.active_plateau_frontier_multiplier
        << ", retire evict "
        << (adaptive_options.basins.active_plateau_retire_evict ? "yes" : "no")
        << " @ fraction/max-fraction/min/param-only/param-distance/force-first/defer/stop-clears "
        << adaptive_options.basins.active_plateau_retire_evict_fraction
        << "/"
        << adaptive_options.basins.active_plateau_retire_evict_max_fraction
        << "/"
        << adaptive_options.basins.active_plateau_retire_evict_min
        << "/"
        << (adaptive_options.basins.active_plateau_retire_evict_parameter_only
         ? "yes"
         : "no")
        << "/"
        << adaptive_options.basins.active_plateau_retire_evict_parameter_distance
        << "/"
        << (adaptive_options.basins.active_plateau_retire_frontier_on_first_update
         ? "yes"
         : "no")
        << "/"
        << adaptive_options.basins.active_plateau_retire_defer_early_stop_epochs
        << "/"
        << adaptive_options.basins.active_plateau_retire_stop_after_clears
        << ", basin balanced refresh: "
        << (adaptive_options.basins.balanced_refresh ? "yes" : "no")
        << ", basin structural topology diversity: "
        << (adaptive_options.basins.semantic_diversity ? "yes" : "no")
        << ", basin max/structural topology: "
        << adaptive_options.basins.semantic_max_per_topology
        << ", topology diversity mode: "
        << topology_diversity_mode_name(
        adaptive_options.basins.topology_descriptor.mode)
        << ", descriptor windows: "
        << adaptive_options.basins.topology_descriptor.window_lmin
        << "-"
        << adaptive_options.basins.topology_descriptor.window_lmax
        << ", descriptor weak tau: "
        << adaptive_options.basins.topology_descriptor.weak_tau_low
        << "/"
        << adaptive_options.basins.topology_descriptor.weak_tau_high
        << ", topology similarity threshold: "
        << adaptive_options.basins.topology_descriptor.similarity_threshold;
}

void OptimizationCommand::print_adaptive_termination() {
    std::cout
        << ", early stop: "
        << (adaptive_options.early_stop_enabled ? "yes" : "no")
        << ", early stop metric: "
        << adaptive_options.early_stop_metric
        << ", early stop min epochs: "
        << adaptive_options.early_stop_min_epochs
        << ", early stop window: "
        << adaptive_options.early_stop_window
        << ", early stop min rel improve: "
        << adaptive_options.early_stop_min_rel_improve
        << ", early stop min abs improve: "
        << adaptive_options.early_stop_min_abs_improve
        << ", LPSR horizon generations: "
        << adaptive_options.lpsr_horizon_generations
        << ", early stop min LPSR progress/tail: "
        << adaptive_options.early_stop_min_lpsr_progress
        << "/"
        << adaptive_options.early_stop_tail_epochs
        << ", early stop best merit max: "
        << adaptive_options.early_stop_best_merit_max
        << ", early stop qualified topologies: "
        << (adaptive_options.early_stop_use_qualified_topologies ? "yes" : "no")
        << ", early stop qualified mode: "
        << adaptive_options.early_stop_qualified_topology_mode
        << ", early stop qualified epsilon: "
        << adaptive_options.early_stop_qualified_topology_first_order_epsilon
        << ", early stop qualified merit ratio/floor/delta/max: "
        << adaptive_options.early_stop_qualified_topology_merit_ratio
        << "/"
        << adaptive_options.early_stop_qualified_topology_merit_floor
        << "/"
        << adaptive_options.early_stop_qualified_topology_merit_delta
        << "/"
        << adaptive_options.early_stop_qualified_topology_merit_max
        << ", internal scaling: "
        << internal_scaling_name(adaptive_options.internal_scaling)
        << "\n";
}

} } }
