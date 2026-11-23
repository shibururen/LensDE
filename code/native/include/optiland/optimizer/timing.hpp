#pragma once

namespace optiland {
struct DeTimingStats {
    int evaluation_calls = 0;
    long long evaluated_candidates = 0;
    double setup_seconds = 0.0;
    double initial_evaluation_seconds = 0.0;
    double trial_generation_seconds = 0.0;
    double trial_mating_pool_seconds = 0.0;
    double trial_mating_plan_seconds = 0.0;
    double trial_pbest_seconds = 0.0;
    double trial_mating_select_seconds = 0.0;
    double trial_mutation_seconds = 0.0;
    long long topology_similarity_cache_hits = 0;
    long long topology_similarity_cache_misses = 0;
    long long topology_shape_diff_cache_hits = 0;
    long long topology_shape_diff_cache_misses = 0;
    double physical_conversion_seconds = 0.0;
    double evaluation_seconds = 0.0;
    double selection_seconds = 0.0;
    double selection_family_gate_seconds = 0.0;
    double selection_compare_seconds = 0.0;
    double selection_update_seconds = 0.0;
    double selection_basin_seconds = 0.0;
    double selection_finalize_seconds = 0.0;
    double basin_consider_seconds = 0.0;
    double basin_diversity_match_seconds = 0.0;
    double basin_distance_seconds = 0.0;
    double basin_make_elite_seconds = 0.0;
    double basin_trim_seconds = 0.0;
    double basin_archive_stage_seconds = 0.0;
    int basin_new_count = 0;
    int basin_replace_count = 0;
    int basin_trim_count = 0;
    double best_rescore_seconds = 0.0;
    double attach_population_seconds = 0.0;
    int dls_iterations = 0;
    int dls_accepted_steps = 0;
    int dls_rejected_steps = 0;
    long long dls_jacobian_candidates = 0;
    int dls_trial_evaluations = 0;
    double dls_jacobian_seconds = 0.0;
    double dls_linear_solve_seconds = 0.0;
    double dls_trial_score_seconds = 0.0;
    double dls_trial_residual_seconds = 0.0;
    long long topology_key_builds = 0;
    long long topology_descriptor_builds = 0;
    long long topology_descriptor_selector_calls = 0;
    double topology_key_seconds = 0.0;
    double topology_descriptor_seconds = 0.0;
    double topology_descriptor_selector_seconds = 0.0;
    CudaEvaluationTiming cuda;
};


}  // namespace optiland
