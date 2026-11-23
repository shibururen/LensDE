// Serialize hybrid/adaptive trace events and the family-transition audit into append-only JSONL.
// Optimizer callbacks provide event state; the field-group writers retain the output schema.
// Keep event order, scientific field meanings, non-finite handling, and numeric precision unchanged.

#include "cli_internal.hpp"

namespace lensde { namespace cli { namespace detail {
namespace {

void write_trace_identity(std::ostream& out, const HybridTraceEvent& event) {
    out << "{";
    out << "\"event\":\"" << json_escape_local(event.event) << "\"";
    out << ",\"epoch\":" << event.epoch;
    out << ",\"population_size\":" << event.population_size;
    out << ",\"variable_count\":" << event.variable_count;
    out << ",\"de_generations\":" << event.de_generations;
    out << ",\"epoch_de_generations\":" << event.epoch_de_generations;
    out << ",\"parent_count\":" << event.parent_count;
    out << ",\"refresh_count\":" << event.refresh_count;
    out << ",\"refreshed_count\":" << event.refreshed_count;
    out << ",\"refresh_archive_size\":" << event.refresh_archive_size;
    out << ",\"refresh_archive_used\":" << event.refresh_archive_used;
    out << ",\"refresh_effective_count\":" << event.refresh_effective_count;
    out << ",\"refresh_adaptive_skipped\":"
        << (event.refresh_adaptive_skipped ? "true" : "false");
    out << ",\"refresh_acceptance_rate\":";
    write_json_number_local(out, event.refresh_acceptance_rate);
    out << ",\"refresh_rel_improvement\":";
    write_json_number_local(out, event.refresh_rel_improvement);
    out << ",\"accepted_trials\":" << event.accepted_trials;
}

void write_trace_constraints(std::ostream& out, const HybridTraceEvent& event) {
    out << ",\"efl_sign_priority_target\":";
    write_json_number_local(out, event.efl_sign_priority_target);
    out << ",\"efl_sign_priority_promoted_trial_count\":"
        << event.efl_sign_priority_promoted_trial_count;
    out << ",\"efl_sign_priority_blocked_trial_count\":"
        << event.efl_sign_priority_blocked_trial_count;
    out << ",\"cra_worsening_guard_upper_deg\":";
    write_json_number_local(out, event.cra_worsening_guard_upper_deg);
    out << ",\"cra_worsening_guard_blocked_trial_count\":"
        << event.cra_worsening_guard_blocked_trial_count;
    out << ",\"joint_constraint_anchor_cra_activation_upper_deg\":";
    write_json_number_local(
        out,
        event.joint_constraint_anchor_cra_activation_upper_deg);
    out << ",\"joint_constraint_anchor_distortion_activation_upper\":";
    write_json_number_local(
        out,
        event.joint_constraint_anchor_distortion_activation_upper);
    out << ",\"joint_constraint_anchor_active_count\":"
        << event.joint_constraint_anchor_active_count;
    out << ",\"joint_constraint_anchor_activated_count\":"
        << event.joint_constraint_anchor_activated_count;
    out << ",\"joint_constraint_anchor_updated_count\":"
        << event.joint_constraint_anchor_updated_count;
    out << ",\"joint_constraint_anchor_blocked_cra_count\":"
        << event.joint_constraint_anchor_blocked_cra_count;
    out << ",\"joint_constraint_anchor_blocked_distortion_count\":"
        << event.joint_constraint_anchor_blocked_distortion_count;
    out << ",\"joint_constraint_anchor_blocked_both_count\":"
        << event.joint_constraint_anchor_blocked_both_count;
    out << ",\"efl_sign_matching_population_count\":"
        << event.efl_sign_matching_population_count;
    out << ",\"efl_sign_opposite_population_count\":"
        << event.efl_sign_opposite_population_count;
    out << ",\"efl_sign_unknown_population_count\":"
        << event.efl_sign_unknown_population_count;
}

void write_trace_basin_slots(std::ostream& out, const HybridTraceEvent& event) {
    out << ",\"archive_size\":" << event.archive_size;
    out << ",\"basin_count\":" << event.basin_count;
    out << ",\"basin_topology_count\":" << event.basin_topology_count;
    out << ",\"basin_qualified_topology_count\":"
        << event.basin_qualified_topology_count;
    out << ",\"active_basin_archive_only\":"
        << (event.active_basin_archive_only ? "true" : "false");
    out << ",\"active_basin_candidate_count\":" << event.active_basin_candidate_count;
    out << ",\"active_basin_slot_count\":" << event.active_basin_slot_count;
    out << ",\"active_basin_injected_family_count\":"
        << event.active_basin_injected_family_count;
    out << ",\"active_basin_current_family_count\":"
        << event.active_basin_current_family_count;
    out << ",\"active_basin_reselected_origin_family_count\":"
        << event.active_basin_reselected_origin_family_count;
    out << ",\"active_basin_reselected_drifted_origin_family_count\":"
        << event.active_basin_reselected_drifted_origin_family_count;
    out << ",\"active_mating_quota_family_count\":"
        << event.active_mating_quota_family_count;
    out << ",\"active_mating_quota_per_family\":"
        << event.active_mating_quota_per_family;
    out << ",\"active_mating_quota_match_count\":"
        << event.active_mating_quota_match_count;
    out << ",\"active_mating_quota_allowed_count\":"
        << event.active_mating_quota_allowed_count;
    out << ",\"active_mating_quota_blocked_count\":"
        << event.active_mating_quota_blocked_count;
    out << ",\"active_mating_quota_max_family_used\":"
        << event.active_mating_quota_max_family_used;
}

void write_trace_niches(std::ostream& out, const HybridTraceEvent& event) {
    out << ",\"active_niche_family_count\":" << event.active_niche_family_count;
    out << ",\"active_niche_member_count\":" << event.active_niche_member_count;
    out << ",\"active_niche_free_count\":" << event.active_niche_free_count;
    out << ",\"active_niche_min_quota\":" << event.active_niche_min_quota;
    out << ",\"active_niche_target_quota\":" << event.active_niche_target_quota;
    out << ",\"active_niche_family_cap\":" << event.active_niche_family_cap;
    out << ",\"active_niche_quota_deficit\":" << event.active_niche_quota_deficit;
    out << ",\"active_niche_below_min_count\":" << event.active_niche_below_min_count;
    out << ",\"active_niche_max_family_size\":" << event.active_niche_max_family_size;
    out << ",\"active_niche_max_family_fraction\":";
    write_json_number_local(out, event.active_niche_max_family_fraction);
    out << ",\"active_niche_occupancy_gini\":";
    write_json_number_local(out, event.active_niche_occupancy_gini);
    out << ",\"active_niche_floor_blocked_count\":"
        << event.active_niche_floor_blocked_count;
    out << ",\"active_niche_family_cap_blocked_count\":"
        << event.active_niche_family_cap_blocked_count;
    out << ",\"active_niche_hard_cap_release_count\":"
        << event.active_niche_hard_cap_release_count;
    out << ",\"active_niche_deferred_candidate_count\":"
        << event.active_niche_deferred_candidate_count;
    out << ",\"active_niche_repair_move_count\":"
        << event.active_niche_repair_move_count;
    out << ",\"active_niche_rebalance_candidate_count\":"
        << event.active_niche_rebalance_candidate_count;
    out << ",\"active_niche_rebalance_move_count\":"
        << event.active_niche_rebalance_move_count;
    out << ",\"active_niche_soft_fill_candidate_count\":"
        << event.active_niche_soft_fill_candidate_count;
    out << ",\"active_niche_soft_fill_move_count\":"
        << event.active_niche_soft_fill_move_count;
    out << ",\"active_niche_soft_fill_anchor_count\":"
        << event.active_niche_soft_fill_anchor_count;
}

void write_trace_focus(std::ostream& out, const HybridTraceEvent& event) {
    out << ",\"active_focus_family_count\":"
        << event.active_focus_family_count;
    out << ",\"active_focus_member_count\":"
        << event.active_focus_member_count;
    out << ",\"active_focus_target_quota\":"
        << event.active_focus_target_quota;
    out << ",\"active_focus_family_cap\":"
        << event.active_focus_family_cap;
    out << ",\"active_monitor_family_cap\":"
        << event.active_monitor_family_cap;
    out << ",\"active_focus_ready_count\":"
        << event.active_focus_ready_count;
    out << ",\"active_focus_cooldown_family_count\":"
        << event.active_focus_cooldown_family_count;
    out << ",\"active_focus_stopped_family_count\":"
        << event.active_focus_stopped_family_count;
    out << ",\"active_focus_stop_clear_count\":"
        << event.active_focus_stop_clear_count;
    out << ",\"active_focus_remap_count\":"
        << event.active_focus_remap_count;
    out << ",\"active_focus_reset_count\":"
        << event.active_focus_reset_count;
    out << ",\"active_focus_selection_count\":"
        << event.active_focus_selection_count;
    out << ",\"active_focus_rotation_history_count\":"
        << event.active_focus_rotation_history_count;
    out << ",\"active_focus_rotation_blocked_count\":"
        << event.active_focus_rotation_blocked_count;
    out << ",\"active_focus_rotation_fallback_count\":"
        << event.active_focus_rotation_fallback_count;
    out << ",\"active_focus_soft_fill_candidate_count\":"
        << event.active_focus_soft_fill_candidate_count;
    out << ",\"active_focus_soft_fill_move_count\":"
        << event.active_focus_soft_fill_move_count;
    out << ",\"active_focus_soft_fill_anchor_count\":"
        << event.active_focus_soft_fill_anchor_count;
    out << ",\"active_focus_stop_release_candidate_count\":"
        << event.active_focus_stop_release_candidate_count;
    out << ",\"active_focus_stop_release_move_count\":"
        << event.active_focus_stop_release_move_count;
    out << ",\"active_focus_best_merit\":";
    write_json_number_local(out, event.active_focus_best_merit);
    out << ",\"active_focus_best_key\":\""
        << json_escape_local(event.active_focus_best_key) << "\"";
    out << ",\"active_focus_best_niche_index\":"
        << event.active_focus_best_niche_index;
    out << ",\"active_focus_best_population_index\":"
        << event.active_focus_best_population_index;
    out << ",\"active_focus_best_member_count\":"
        << event.active_focus_best_member_count;
    out << ",\"active_focus_last_rel_improvement\":";
    write_json_number_local(out, event.active_focus_last_rel_improvement);
    out << ",\"active_niche_best_merits\":";
    write_active_niche_best_merits_json(out, event.active_niche_best_merits);
    out << ",\"active_local_cohort_family_count\":"
        << event.active_local_cohort_family_count;
    out << ",\"active_local_cohort_anchor_count\":"
        << event.active_local_cohort_anchor_count;
    out << ",\"active_local_cohort_total_limit\":"
        << event.active_local_cohort_total_limit;
    out << ",\"active_local_cohort_per_family\":"
        << event.active_local_cohort_per_family;
    out << ",\"active_local_cohort_selected_count\":"
        << event.active_local_cohort_selected_count;
    out << ",\"active_local_cohort_max_family_size\":"
        << event.active_local_cohort_max_family_size;
}

void write_trace_plateau(std::ostream& out, const HybridTraceEvent& event) {
    out << ",\"active_refresh_rebalance_family_count\":"
        << event.active_refresh_rebalance_family_count;
    out << ",\"active_refresh_rebalance_family_limit\":"
        << event.active_refresh_rebalance_family_limit;
    out << ",\"active_refresh_rebalance_max_family_size\":"
        << event.active_refresh_rebalance_max_family_size;
    out << ",\"active_refresh_rebalance_candidate_count\":"
        << event.active_refresh_rebalance_candidate_count;
    out << ",\"active_refresh_rebalance_selected_count\":"
        << event.active_refresh_rebalance_selected_count;
    out << ",\"active_mating_match_mode\":\""
        << json_escape_local(event.active_mating_match_mode) << "\"";
    out << ",\"active_plateau_tracked_family_count\":"
        << event.active_plateau_tracked_family_count;
    out << ",\"active_plateau_active_niche_tracked_count\":"
        << event.active_plateau_active_niche_tracked_count;
    out << ",\"active_plateau_stalled_family_count\":"
        << event.active_plateau_stalled_family_count;
    out << ",\"active_plateau_cooldown_family_count\":"
        << event.active_plateau_cooldown_family_count;
    out << ",\"active_plateau_new_cooldown_count\":"
        << event.active_plateau_new_cooldown_count;
    out << ",\"active_plateau_retired_family_count\":"
        << event.active_plateau_retired_family_count;
    out << ",\"active_plateau_evict_count\":"
        << event.active_plateau_evict_count;
    out << ",\"active_plateau_retire_clear_count\":"
        << event.active_plateau_retire_clear_count;
    out << ",\"active_plateau_evict_candidate_count\":"
        << event.active_plateau_evict_candidate_count;
    out << ",\"active_plateau_evict_cap_count\":"
        << event.active_plateau_evict_cap_count;
    out << ",\"active_plateau_evict_distance_min\":";
    write_json_number_local(out, event.active_plateau_evict_distance_min);
    out << ",\"active_plateau_evict_distance_median\":";
    write_json_number_local(out, event.active_plateau_evict_distance_median);
    out << ",\"active_plateau_evict_distance_max\":";
    write_json_number_local(out, event.active_plateau_evict_distance_max);
    out << ",\"active_plateau_refill_basin_count\":"
        << event.active_plateau_refill_basin_count;
    out << ",\"active_plateau_refill_population_count\":"
        << event.active_plateau_refill_population_count;
    out << ",\"active_plateau_refill_refresh_count\":"
        << event.active_plateau_refill_refresh_count;
    out << ",\"active_plateau_refill_initial_seed_count\":"
        << event.active_plateau_refill_initial_seed_count;
    out << ",\"active_plateau_refill_generated_count\":"
        << event.active_plateau_refill_generated_count;
    out << ",\"active_plateau_refill_lhs_count\":"
        << event.active_plateau_refill_lhs_count;
    out << ",\"active_plateau_refill_clone_fallback_count\":"
        << event.active_plateau_refill_clone_fallback_count;
    out << ",\"active_plateau_refill_rejected_trace_count\":"
        << event.active_plateau_refill_rejected_trace_count;
    out << ",\"active_plateau_refill_rejected_retired_count\":"
        << event.active_plateau_refill_rejected_retired_count;
    out << ",\"active_plateau_retired_trial_reject_count\":"
        << event.active_plateau_retired_trial_reject_count;
    out << ",\"active_plateau_quota_scaled_family_count\":"
        << event.active_plateau_quota_scaled_family_count;
    out << ",\"active_plateau_external_probe_count\":"
        << event.active_plateau_external_probe_count;
    out << ",\"active_plateau_pbest_suppressed_count\":"
        << event.active_plateau_pbest_suppressed_count;
    out << ",\"active_plateau_pbest_global_rescue_count\":"
        << event.active_plateau_pbest_global_rescue_count;
}

void write_trace_population(std::ostream& out, const HybridTraceEvent& event) {
    out << ",\"basin_descriptor_count\":" << event.basin_descriptor_count;
    out << ",\"basin_descriptor_axis_count\":" << event.basin_descriptor_axis_count;
    out << ",\"basin_descriptor_axis_unique_min\":"
        << event.basin_descriptor_axis_unique_min;
    out << ",\"basin_descriptor_axis_unique_max\":"
        << event.basin_descriptor_axis_unique_max;
    out << ",\"basin_descriptor_axis_unique_mean\":";
    write_json_number_local(out, event.basin_descriptor_axis_unique_mean);
    out << ",\"feasible_count\":" << event.feasible_count;
    out << ",\"trace_feasible_count\":" << event.trace_feasible_count;
    out << ",\"constraint_feasible_count\":" << event.constraint_feasible_count;
    out << ",\"first_order_feasible_count\":" << event.first_order_feasible_count;
    out << ",\"population_elite_count\":" << event.population_elite_count;
    out << ",\"population_elite_mean_merit\":";
    write_json_number_local(out, event.population_elite_mean_merit);
    out << ",\"population_elite_median_merit\":";
    write_json_number_local(out, event.population_elite_median_merit);
    out << ",\"population_elite_worst_merit\":";
    write_json_number_local(out, event.population_elite_worst_merit);
    out << ",\"population_best\":";
    write_score_summary_json(out, event.population_best_score);
    out << ",\"population_best_values\":[";
    for (std::size_t i = 0; i < event.population_best_values.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        write_json_number_local(out, event.population_best_values[i]);
    }
    out << "]";
    out << ",\"population_best_previous_merit\":";
    write_json_number_local(out, event.population_best_previous_merit);
    out << ",\"population_best_improvement\":";
    write_json_number_local(out, event.population_best_improvement);
    out << ",\"population_best_improved\":"
        << (event.population_best_improved ? "true" : "false");
    out << ",\"population_best_vs_incumbent_merit_delta\":";
    write_json_number_local(out, event.population_best_vs_incumbent_merit_delta);
    out << ",\"population_diversity_tracked\":"
        << (event.population_diversity_tracked ? "true" : "false");
    out << ",\"population_distance_to_best_count\":"
        << event.population_distance_to_best_count;
    out << ",\"population_mean_euclidean_distance_to_best\":";
    write_json_number_local(out, event.population_mean_euclidean_distance_to_best);
}

void write_trace_material(std::ostream& out, const HybridTraceEvent& event) {
    out << ",\"adaptive_glass_tolerance_enabled\":"
        << (event.adaptive_glass_tolerance_enabled ? "true" : "false");
    out << ",\"adaptive_glass_tolerance_updated\":"
        << (event.adaptive_glass_tolerance_updated ? "true" : "false");
    out << ",\"adaptive_glass_tolerance_ready\":"
        << (event.adaptive_glass_tolerance_ready ? "true" : "false");
    out << ",\"adaptive_glass_tolerance_trigger_ready\":"
        << (event.adaptive_glass_tolerance_trigger_ready ? "true" : "false");
    out << ",\"adaptive_glass_tolerance_bridge_active\":"
        << (event.adaptive_glass_tolerance_bridge_active ? "true" : "false");
    out << ",\"adaptive_glass_tolerance_focus_frozen\":"
        << (event.adaptive_glass_tolerance_focus_frozen ? "true" : "false");
    out << ",\"adaptive_glass_tolerance_stop_when_ready\":"
        << (event.adaptive_glass_tolerance_stop_when_ready ? "true" : "false");
    out << ",\"adaptive_glass_tolerance_trigger_mode\":\""
        << json_escape_local(event.adaptive_glass_tolerance_trigger_mode)
        << "\"";
    out << ",\"adaptive_glass_tolerance_focus_clear_count\":"
        << event.adaptive_glass_tolerance_focus_clear_count;
    out << ",\"adaptive_glass_tolerance_focus_clear_target\":"
        << event.adaptive_glass_tolerance_focus_clear_target;
    out << ",\"adaptive_glass_tolerance_sample_count\":"
        << event.adaptive_glass_tolerance_sample_count;
    out << ",\"adaptive_glass_tolerance_elite_count\":"
        << event.adaptive_glass_tolerance_elite_count;
    out << ",\"adaptive_glass_tolerance_previous\":";
    write_json_number_local(out, event.adaptive_glass_tolerance_previous);
    out << ",\"adaptive_glass_tolerance_current\":";
    write_json_number_local(out, event.adaptive_glass_tolerance_current);
    out << ",\"adaptive_glass_tolerance_target\":";
    write_json_number_local(out, event.adaptive_glass_tolerance_target);
    out << ",\"adaptive_glass_tolerance_metric\":";
    write_json_number_local(out, event.adaptive_glass_tolerance_metric);
    out << ",\"basin_elite_count\":" << event.basin_elite_count;
    out << ",\"basin_elite_mean_merit\":";
    write_json_number_local(out, event.basin_elite_mean_merit);
    out << ",\"basin_elite_median_merit\":";
    write_json_number_local(out, event.basin_elite_median_merit);
    out << ",\"basin_elite_worst_merit\":";
    write_json_number_local(out, event.basin_elite_worst_merit);
    out << ",\"basin_unique_topology_elite_count\":"
        << event.basin_unique_topology_elite_count;
    out << ",\"basin_unique_topology_elite_mean_merit\":";
    write_json_number_local(out, event.basin_unique_topology_elite_mean_merit);
    out << ",\"basin_unique_topology_elite_median_merit\":";
    write_json_number_local(out, event.basin_unique_topology_elite_median_merit);
    out << ",\"basin_unique_topology_elite_worst_merit\":";
    write_json_number_local(out, event.basin_unique_topology_elite_worst_merit);
}

void write_trace_mutation(std::ostream& out, const HybridTraceEvent& event) {
    out << ",\"mu_f\":";
    write_json_number_local(out, event.mu_f);
    out << ",\"mu_cr\":";
    write_json_number_local(out, event.mu_cr);
    out << ",\"ga_tournament_size\":" << event.ga_tournament_size;
    out << ",\"ga_crossover_rate\":";
    write_json_number_local(out, event.ga_crossover_rate);
    out << ",\"ga_mutation_rate\":";
    write_json_number_local(out, event.ga_mutation_rate);
    out << ",\"ga_mutation_scale_start\":";
    write_json_number_local(out, event.ga_mutation_scale_start);
    out << ",\"ga_mutation_scale_end\":";
    write_json_number_local(out, event.ga_mutation_scale_end);
    out << ",\"pso_inertia\":";
    write_json_number_local(out, event.pso_inertia);
    out << ",\"pso_cognitive\":";
    write_json_number_local(out, event.pso_cognitive);
    out << ",\"pso_social\":";
    write_json_number_local(out, event.pso_social);
    out << ",\"pso_initial_velocity_fraction\":";
    write_json_number_local(out, event.pso_initial_velocity_fraction);
    out << ",\"pso_max_velocity_fraction\":";
    write_json_number_local(out, event.pso_max_velocity_fraction);
    out << ",\"pso_boundary_damping\":";
    write_json_number_local(out, event.pso_boundary_damping);
    out << ",\"sa_step_scale_start\":";
    write_json_number_local(out, event.sa_step_scale_start);
    out << ",\"sa_step_scale_end\":";
    write_json_number_local(out, event.sa_step_scale_end);
    out << ",\"sa_temperature_start\":";
    write_json_number_local(out, event.sa_temperature_start);
    out << ",\"sa_temperature_end\":";
    write_json_number_local(out, event.sa_temperature_end);
    out << ",\"sa_temperature_power\":";
    write_json_number_local(out, event.sa_temperature_power);
    out << ",\"sa_glass_mutation_rate\":";
    write_json_number_local(out, event.sa_glass_mutation_rate);
    out << ",\"pbest_guide_self_count\":" << event.pbest_guide_self_count;
    out << ",\"pbest_guide_same_key_count\":" << event.pbest_guide_same_key_count;
    out << ",\"pbest_guide_basin_count\":" << event.pbest_guide_basin_count;
    out << ",\"pbest_guide_global_count\":" << event.pbest_guide_global_count;
    out << ",\"pbest_guide_none_count\":" << event.pbest_guide_none_count;
    out << ",\"pbest_guide_key_match_count\":" << event.pbest_guide_key_match_count;
    out << ",\"pbest_guide_key_mismatch_count\":" << event.pbest_guide_key_mismatch_count;
    out << ",\"pbest_guide_key_unknown_count\":" << event.pbest_guide_key_unknown_count;
}

void write_trace_geometry_memory(std::ostream& out, const HybridTraceEvent& event) {
    out << ",\"geometry_memory_reference_count\":"
        << event.geometry_memory_reference_count;
    out << ",\"geometry_memory_pbest_checked_count\":"
        << event.geometry_memory_pbest_checked_count;
    out << ",\"geometry_memory_pbest_hit_count\":"
        << event.geometry_memory_pbest_hit_count;
    out << ",\"geometry_memory_pbest_redirect_count\":"
        << event.geometry_memory_pbest_redirect_count;
    out << ",\"geometry_memory_pbest_rescue_count\":"
        << event.geometry_memory_pbest_rescue_count;
    out << ",\"geometry_memory_distance_seconds\":";
    write_json_number_local(out, event.geometry_memory_distance_seconds);
    out << ",\"geometry_memory_difference_checked_count\":"
        << event.geometry_memory_difference_checked_count;
    out << ",\"geometry_memory_difference_hit_count\":"
        << event.geometry_memory_difference_hit_count;
    out << ",\"geometry_memory_difference_applied_count\":"
        << event.geometry_memory_difference_applied_count;
    out << ",\"geometry_memory_difference_same_key_count\":"
        << event.geometry_memory_difference_same_key_count;
    out << ",\"geometry_memory_difference_geometry_trigger_count\":"
        << event.geometry_memory_difference_geometry_trigger_count;
    out << ",\"geometry_memory_difference_key_trigger_count\":"
        << event.geometry_memory_difference_key_trigger_count;
    out << ",\"geometry_memory_difference_exact_key_trigger_count\":"
        << event.geometry_memory_difference_exact_key_trigger_count;
    out << ",\"geometry_memory_difference_family_key_trigger_count\":"
        << event.geometry_memory_difference_family_key_trigger_count;
    out << ",\"geometry_memory_difference_outward_flip_count\":"
        << event.geometry_memory_difference_outward_flip_count;
    out << ",\"geometry_memory_difference_capped_count\":"
        << event.geometry_memory_difference_capped_count;
    out << ",\"geometry_memory_difference_seconds\":";
    write_json_number_local(out, event.geometry_memory_difference_seconds);
    out << ",\"geometry_memory_qohr_checked_count\":"
        << event.geometry_memory_qohr_checked_count;
    out << ",\"geometry_memory_qohr_applied_count\":"
        << event.geometry_memory_qohr_applied_count;
    out << ",\"geometry_memory_qohr_degenerate_count\":"
        << event.geometry_memory_qohr_degenerate_count;
    out << ",\"geometry_memory_qohr_quality_gated_count\":"
        << event.geometry_memory_qohr_quality_gated_count;
    out << ",\"geometry_memory_qohr_family_weighted_count\":"
        << event.geometry_memory_qohr_family_weighted_count;
    out << ",\"geometry_memory_qohr_quality_weight_sum\":";
    write_json_number_local(out, event.geometry_memory_qohr_quality_weight_sum);
    out << ",\"geometry_memory_qohr_rotation_sum\":";
    write_json_number_local(out, event.geometry_memory_qohr_rotation_sum);
    out << ",\"geometry_memory_qohr_quality_projection_before_sum\":";
    write_json_number_local(
        out,
        event.geometry_memory_qohr_quality_projection_before_sum);
    out << ",\"geometry_memory_qohr_quality_projection_after_sum\":";
    write_json_number_local(
        out,
        event.geometry_memory_qohr_quality_projection_after_sum);
    out << ",\"geometry_memory_qohr_history_projection_before_sum\":";
    write_json_number_local(
        out,
        event.geometry_memory_qohr_history_projection_before_sum);
    out << ",\"geometry_memory_qohr_history_projection_after_sum\":";
    write_json_number_local(
        out,
        event.geometry_memory_qohr_history_projection_after_sum);
    out << ",\"geometry_memory_qohr_step_norm_ratio_sum\":";
    write_json_number_local(out, event.geometry_memory_qohr_step_norm_ratio_sum);
    out << ",\"geometry_memory_qohr_seconds\":";
    write_json_number_local(out, event.geometry_memory_qohr_seconds);
    out << ",\"geometry_memory_exact_escape_resident_generation_count\":"
        << event.geometry_memory_exact_escape_resident_generation_count;
    out << ",\"geometry_memory_exact_escape_activation_count\":"
        << event.geometry_memory_exact_escape_activation_count;
    out << ",\"geometry_memory_exact_escape_active_generation_count\":"
        << event.geometry_memory_exact_escape_active_generation_count;
    out << ",\"geometry_memory_exact_escape_success_count\":"
        << event.geometry_memory_exact_escape_success_count;
    out << ",\"geometry_memory_exact_escape_timeout_count\":"
        << event.geometry_memory_exact_escape_timeout_count;
    out << ",\"geometry_memory_exact_escape_crossing_trial_count\":"
        << event.geometry_memory_exact_escape_crossing_trial_count;
    out << ",\"geometry_memory_exact_escape_crossing_accepted_count\":"
        << event.geometry_memory_exact_escape_crossing_accepted_count;
    out << ",\"geometry_memory_exact_escape_novelty_eligible_count\":"
        << event.geometry_memory_exact_escape_novelty_eligible_count;
    out << ",\"geometry_memory_exact_escape_novelty_accepted_count\":"
        << event.geometry_memory_exact_escape_novelty_accepted_count;
    out << ",\"geometry_memory_family_escape_activation_count\":"
        << event.geometry_memory_family_escape_activation_count;
    out << ",\"geometry_memory_family_escape_active_generation_count\":"
        << event.geometry_memory_family_escape_active_generation_count;
    out << ",\"geometry_memory_family_escape_success_count\":"
        << event.geometry_memory_family_escape_success_count;
    out << ",\"geometry_memory_family_escape_crossing_trial_count\":"
        << event.geometry_memory_family_escape_crossing_trial_count;
    out << ",\"geometry_memory_family_escape_crossing_accepted_count\":"
        << event.geometry_memory_family_escape_crossing_accepted_count;
    out << ",\"geometry_memory_family_escape_novelty_eligible_count\":"
        << event.geometry_memory_family_escape_novelty_eligible_count;
    out << ",\"geometry_memory_family_escape_novelty_accepted_count\":"
        << event.geometry_memory_family_escape_novelty_accepted_count;
    out << ",\"geometry_memory_novelty_guide_generation_count\":"
        << event.geometry_memory_novelty_guide_generation_count;
    out << ",\"geometry_memory_novelty_guide_unseen_generation_count\":"
        << event.geometry_memory_novelty_guide_unseen_generation_count;
    out << ",\"geometry_memory_novelty_pbest_checked_count\":"
        << event.geometry_memory_novelty_pbest_checked_count;
    out << ",\"geometry_memory_novelty_pbest_redirect_count\":"
        << event.geometry_memory_novelty_pbest_redirect_count;
}

void write_trace_mating(std::ostream& out, const HybridTraceEvent& event) {
    out << ",\"de_mating_local_count\":" << event.de_mating_local_count;
    out << ",\"de_mating_global_count\":" << event.de_mating_global_count;
    out << ",\"de_mating_fallback_count\":" << event.de_mating_fallback_count;
    out << ",\"de_mating_key_unknown_count\":" << event.de_mating_key_unknown_count;
    out << ",\"de_mating_archive_local_count\":" << event.de_mating_archive_local_count;
    out << ",\"de_mating_archive_global_count\":" << event.de_mating_archive_global_count;
    out << ",\"de_mating_local_pool_min\":" << event.de_mating_local_pool_min;
    out << ",\"de_mating_local_pool_max\":" << event.de_mating_local_pool_max;
    out << ",\"de_mating_local_pool_mean\":";
    write_json_number_local(out, event.de_mating_local_pool_mean);
    out << ",\"best_objective\":";
    write_json_number_local(out, event.best_objective);
    out << ",\"best_total_violation\":";
    write_json_number_local(out, event.best_total_violation);
}

void write_trace_polish(std::ostream& out, const HybridTraceEvent& event) {
    out << ",\"phase_seconds\":";
    write_json_number_local(out, event.phase_seconds);
    out << ",\"cumulative_de_seconds\":";
    write_json_number_local(out, event.cumulative_de_seconds);
    out << ",\"cumulative_polish_seconds\":";
    write_json_number_local(out, event.cumulative_polish_seconds);
    out << ",\"polish_runs\":" << event.polish_runs;
    out << ",\"polish_improvements\":" << event.polish_improvements;
    out << ",\"polish_merit_strict_improvements\":"
        << event.polish_merit_strict_improvements;
    out << ",\"polish_merit_neutral\":" << event.polish_merit_neutral;
    out << ",\"polish_merit_worse\":" << event.polish_merit_worse;
    out << ",\"micro_island_runs\":" << event.micro_island_runs;
    out << ",\"micro_island_improvements\":" << event.micro_island_improvements;
    out << ",\"micro_island_archived\":" << event.micro_island_archived;
    out << ",\"micro_island_early_stopped\":"
        << event.micro_island_early_stopped;
    out << ",\"micro_island_retired\":" << event.micro_island_retired;
    out << ",\"micro_island_seed_count\":" << event.micro_island_seed_count;
    out << ",\"micro_island_seconds\":";
    write_json_number_local(out, event.micro_island_seconds);
    out << ",\"micro_island_de_seconds\":";
    write_json_number_local(out, event.micro_island_de_seconds);
    out << ",\"micro_island_polish_seconds\":";
    write_json_number_local(out, event.micro_island_polish_seconds);
    out << ",\"micro_island_records\":[";
    for (std::size_t i = 0; i < event.micro_island_records.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        const MicroIslandTraceRecord& record = event.micro_island_records[i];
        out << "{";
        out << "\"epoch\":" << record.epoch;
        out << ",\"island_index\":" << record.island_index;
        out << ",\"source_basin_id\":" << record.source_basin_id;
        out << ",\"source_key\":\"" << json_escape_local(record.source_key) << "\"";
        out << ",\"source_topology_key\":\""
            << json_escape_local(record.source_topology_key) << "\"";
        out << ",\"source_descriptor_key\":\""
            << json_escape_local(record.source_descriptor_key) << "\"";
        out << ",\"source_similarity_family_id\":"
            << record.source_similarity_family_id;
        out << ",\"source_merit\":";
        write_json_number_local(out, record.source_merit);
        out << ",\"planned_generations\":" << record.planned_generations;
        out << ",\"actual_generations\":" << record.actual_generations;
        out << ",\"early_stopped\":"
            << (record.early_stopped ? "true" : "false");
        out << ",\"retired\":" << (record.retired ? "true" : "false");
        out << ",\"retired_unique\":"
            << (record.retired_unique ? "true" : "false");
        out << ",\"retire_action\":\""
            << json_escape_local(record.retire_action) << "\"";
        out << ",\"retire_key\":\"" << json_escape_local(record.retire_key) << "\"";
        out << ",\"survivor_rank\":" << record.survivor_rank;
        out << ",\"survivor_merit\":";
        write_json_number_local(out, record.survivor_merit);
        out << ",\"survivor_trace_feasible\":"
            << (record.survivor_trace_feasible ? "true" : "false");
        out << ",\"survivor_topology_key\":\""
            << json_escape_local(record.survivor_topology_key) << "\"";
        out << ",\"survivor_descriptor_key\":\""
            << json_escape_local(record.survivor_descriptor_key) << "\"";
        out << ",\"archive_source\":\""
            << json_escape_local(record.archive_source) << "\"";
        out << ",\"basin_archive_action\":\""
            << json_escape_local(record.basin_archive_action) << "\"";
        out << ",\"basin_archive_id\":" << record.basin_archive_id;
        out << ",\"nearest_basin_id\":" << record.nearest_basin_id;
        out << ",\"nearest_basin_distance\":";
        write_json_number_local(out, record.nearest_basin_distance);
        out << ",\"previous_basin_source\":\""
            << json_escape_local(record.previous_basin_source) << "\"";
        out << ",\"population_replaced\":"
            << (record.population_replaced ? "true" : "false");
        out << ",\"population_target_index\":" << record.population_target_index;
        out << "}";
    }
    out << "]";
}

void write_trace_timing(std::ostream& out, const HybridTraceEvent& event) {
    out << ",\"trial_generation_seconds\":";
    write_json_number_local(out, event.trial_generation_seconds);
    out << ",\"trial_mating_pool_seconds\":";
    write_json_number_local(out, event.trial_mating_pool_seconds);
    out << ",\"trial_mating_plan_seconds\":";
    write_json_number_local(out, event.trial_mating_plan_seconds);
    out << ",\"trial_pbest_seconds\":";
    write_json_number_local(out, event.trial_pbest_seconds);
    out << ",\"trial_mating_select_seconds\":";
    write_json_number_local(out, event.trial_mating_select_seconds);
    out << ",\"trial_mutation_seconds\":";
    write_json_number_local(out, event.trial_mutation_seconds);
    out << ",\"topology_similarity_cache_hits\":"
        << event.topology_similarity_cache_hits;
    out << ",\"topology_similarity_cache_misses\":"
        << event.topology_similarity_cache_misses;
    out << ",\"topology_shape_diff_cache_hits\":"
        << event.topology_shape_diff_cache_hits;
    out << ",\"topology_shape_diff_cache_misses\":"
        << event.topology_shape_diff_cache_misses;
    out << ",\"selection_family_gate_seconds\":";
    write_json_number_local(out, event.selection_family_gate_seconds);
    out << ",\"selection_compare_seconds\":";
    write_json_number_local(out, event.selection_compare_seconds);
    out << ",\"selection_update_seconds\":";
    write_json_number_local(out, event.selection_update_seconds);
    out << ",\"selection_basin_seconds\":";
    write_json_number_local(out, event.selection_basin_seconds);
    out << ",\"selection_finalize_seconds\":";
    write_json_number_local(out, event.selection_finalize_seconds);
    out << ",\"basin_consider_seconds\":";
    write_json_number_local(out, event.basin_consider_seconds);
    out << ",\"basin_diversity_match_seconds\":";
    write_json_number_local(out, event.basin_diversity_match_seconds);
    out << ",\"basin_distance_seconds\":";
    write_json_number_local(out, event.basin_distance_seconds);
    out << ",\"basin_make_elite_seconds\":";
    write_json_number_local(out, event.basin_make_elite_seconds);
    out << ",\"basin_trim_seconds\":";
    write_json_number_local(out, event.basin_trim_seconds);
    out << ",\"basin_archive_stage_seconds\":";
    write_json_number_local(out, event.basin_archive_stage_seconds);
    out << ",\"basin_new_count\":" << event.basin_new_count;
    out << ",\"basin_replace_count\":" << event.basin_replace_count;
    out << ",\"basin_trim_count\":" << event.basin_trim_count;
    out << ",\"dls_iterations\":" << event.dls_iterations;
    out << ",\"dls_accepted_steps\":" << event.dls_accepted_steps;
    out << ",\"dls_rejected_steps\":" << event.dls_rejected_steps;
    out << ",\"dls_jacobian_candidates\":" << event.dls_jacobian_candidates;
    out << ",\"dls_trial_evaluations\":" << event.dls_trial_evaluations;
    out << ",\"dls_jacobian_seconds\":";
    write_json_number_local(out, event.dls_jacobian_seconds);
    out << ",\"dls_linear_solve_seconds\":";
    write_json_number_local(out, event.dls_linear_solve_seconds);
    out << ",\"dls_trial_score_seconds\":";
    write_json_number_local(out, event.dls_trial_score_seconds);
    out << ",\"dls_trial_residual_seconds\":";
    write_json_number_local(out, event.dls_trial_residual_seconds);
}

void write_trace_lpsr(std::ostream& out, const HybridTraceEvent& event) {
    out << ",\"epoch_start_merit\":";
    write_json_number_local(out, event.epoch_start_merit);
    out << ",\"before_merit\":";
    write_json_number_local(out, event.before_merit);
    out << ",\"used_cuda\":" << (event.used_cuda ? "true" : "false");
    out << ",\"lpsr_enabled\":" << (event.lpsr_enabled ? "true" : "false");
    out << ",\"lpsr_initial_population\":" << event.lpsr_initial_population;
    out << ",\"lpsr_min_population\":" << event.lpsr_min_population;
    out << ",\"lpsr_target_population\":" << event.lpsr_target_population;
    out << ",\"lpsr_progress\":";
    write_json_number_local(out, event.lpsr_progress);
    out << ",\"lpsr_reduction_count\":" << event.lpsr_reduction_count;
    out << ",\"lpsr_removed_total\":" << event.lpsr_removed_total;
    out << ",\"early_stop_lpsr_ready\":"
        << (event.early_stop_lpsr_ready ? "true" : "false");
    out << ",\"topology_diversity_mode\":\""
        << json_escape_local(event.topology_diversity_mode) << "\"";
    out << ",\"topology_key_builds\":" << event.topology_key_builds;
    out << ",\"topology_descriptor_builds\":"
        << event.topology_descriptor_builds;
    out << ",\"topology_descriptor_selector_calls\":"
        << event.topology_descriptor_selector_calls;
    out << ",\"topology_key_seconds\":";
    write_json_number_local(out, event.topology_key_seconds);
    out << ",\"topology_descriptor_seconds\":";
    write_json_number_local(out, event.topology_descriptor_seconds);
    out << ",\"topology_descriptor_selector_seconds\":";
    write_json_number_local(out, event.topology_descriptor_selector_seconds);
}

void write_trace_best(std::ostream& out, const HybridTraceEvent& event) {
    out << ",\"best\":";
    write_score_summary_json(out, event.best_score);
    out << ",\"best_values\":[";
    for (std::size_t i = 0; i < event.best_values.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        write_json_number_local(out, event.best_values[i]);
    }
    out << "]";
}

} // namespace

bool append_trace_jsonl(
    const std::string& out_dir,
    const std::string& filename,
    const HybridTraceEvent& event) {
    if (out_dir.empty()) {
        return false;
    }
    std::ofstream out(join_path(out_dir, filename), std::ios::app);
    if (!out) {
        return false;
    }
    out << std::setprecision(12);
    write_trace_identity(out, event);
    write_trace_constraints(out, event);
    write_trace_basin_slots(out, event);
    write_trace_niches(out, event);
    write_trace_focus(out, event);
    write_trace_plateau(out, event);
    write_trace_population(out, event);
    write_trace_material(out, event);
    write_trace_mutation(out, event);
    write_trace_geometry_memory(out, event);
    write_trace_mating(out, event);
    write_trace_polish(out, event);
    write_trace_timing(out, event);
    write_trace_lpsr(out, event);
    write_trace_best(out, event);
    out << "}\n";
    return true;
}

bool append_hybrid_trace_jsonl(const std::string& out_dir, const HybridTraceEvent& event) {
    return append_trace_jsonl(out_dir, "hybrid_trace.jsonl", event);
}

bool append_family_transition_audit_jsonl(
    const std::string& out_dir,
    const FamilyTransitionAuditEvent& event) {
    if (out_dir.empty()) {
        return false;
    }
    std::ofstream out(
        join_path(out_dir, "family_transition_audit.jsonl"),
        std::ios::app);
    if (!out) {
        return false;
    }
    out << std::setprecision(12);
    out << "{";
    out << "\"event\":\"" << json_escape_local(event.event) << "\"";
    out << ",\"epoch\":" << event.epoch;
    out << ",\"generation\":" << event.generation;
    out << ",\"population_index\":" << event.population_index;
    out << ",\"accepted_by_compare\":"
        << (event.accepted_by_compare ? "true" : "false");
    out << ",\"accepted_by_annealing\":"
        << (event.accepted_by_annealing ? "true" : "false");
    out << ",\"structural_key_changed\":"
        << (event.structural_key_changed ? "true" : "false");
    out << ",\"semantic_key_changed\":"
        << (event.semantic_key_changed ? "true" : "false");
    out << ",\"within_family\":"
        << (event.within_family ? "true" : "false");
    out << ",\"family_similarity\":";
    write_json_number_local(out, event.family_similarity);
    out << ",\"shape_difference\":" << event.shape_difference;
    out << ",\"structure_distance\":";
    write_json_number_local(out, event.structure_distance);
    out << ",\"full_distance\":";
    write_json_number_local(out, event.full_distance);
    out << ",\"parent_merit\":";
    write_json_number_local(out, event.parent_merit);
    out << ",\"trial_merit\":";
    write_json_number_local(out, event.trial_merit);
    out << ",\"parent_feasible\":"
        << (event.parent_feasible ? "true" : "false");
    out << ",\"trial_feasible\":"
        << (event.trial_feasible ? "true" : "false");
    out << ",\"parent_trace_feasible\":"
        << (event.parent_trace_feasible ? "true" : "false");
    out << ",\"trial_trace_feasible\":"
        << (event.trial_trace_feasible ? "true" : "false");
    out << ",\"parent_topology_key\":\""
        << json_escape_local(event.parent_topology_key) << "\"";
    out << ",\"trial_topology_key\":\""
        << json_escape_local(event.trial_topology_key) << "\"";
    out << ",\"parent_semantic_key\":\""
        << json_escape_local(event.parent_semantic_key) << "\"";
    out << ",\"trial_semantic_key\":\""
        << json_escape_local(event.trial_semantic_key) << "\"";
    out << ",\"parent_descriptor_key\":\""
        << json_escape_local(event.parent_descriptor_key) << "\"";
    out << ",\"trial_descriptor_key\":\""
        << json_escape_local(event.trial_descriptor_key) << "\"";
    out << ",\"evaluated_trials\":" << event.evaluated_trials;
    out << ",\"trial_same_topology_count\":"
        << event.trial_same_topology_count;
    out << ",\"trial_within_family_count\":"
        << event.trial_within_family_count;
    out << ",\"trial_cross_family_count\":"
        << event.trial_cross_family_count;
    out << ",\"trial_unknown_topology_count\":"
        << event.trial_unknown_topology_count;
    out << ",\"accepted_same_topology_count\":"
        << event.accepted_same_topology_count;
    out << ",\"accepted_within_family_count\":"
        << event.accepted_within_family_count;
    out << ",\"accepted_cross_family_count\":"
        << event.accepted_cross_family_count;
    out << ",\"accepted_unknown_topology_count\":"
        << event.accepted_unknown_topology_count;
    out << "}\n";
    return true;
}

} } }
