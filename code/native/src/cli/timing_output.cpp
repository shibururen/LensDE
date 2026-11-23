// Present DE, DLS, and CUDA timing/counter details from an already-completed OptimizationResult.
// Result publication uses these helpers to distinguish evaluation, setup, solve, and cleanup costs.
// Durations are operational evidence; retain exact iteration/candidate counts without changing solver state.

#include "cli_internal.hpp"

namespace lensde { namespace cli { namespace detail {

void print_de_timing_detail(const OptimizationResult& result) {
    const DeTimingStats& timing = result.timing;
    auto print_dls_detail = [&]() {
        if (timing.dls_iterations <= 0) {
            return;
        }
        std::cout << "  DLS detail: iterations " << timing.dls_iterations
                  << ", accepted " << timing.dls_accepted_steps
                  << ", rejected " << timing.dls_rejected_steps
                  << ", jacobian candidates " << timing.dls_jacobian_candidates
                  << ", trial evals " << timing.dls_trial_evaluations << "\n";
        std::cout << "  DLS timing: jacobian " << timing.dls_jacobian_seconds
                  << " s, linear solve " << timing.dls_linear_solve_seconds
                  << " s, trial score " << timing.dls_trial_score_seconds
                  << " s, trial residual " << timing.dls_trial_residual_seconds
                  << " s\n";
    };
    if (timing.evaluation_calls <= 0) {
        print_dls_detail();
        return;
    }
    if (result.de_seconds <= 0.0 && result.polish_seconds > 0.0) {
        std::cout << "Batch polish timing detail:\n";
        std::cout << "  evaluation " << timing.evaluation_seconds
                  << " s, eval calls " << timing.evaluation_calls
                  << ", candidates " << timing.evaluated_candidates << "\n";
        if (timing.cuda.calls > 0) {
            std::cout << "CUDA evaluation detail:\n";
            std::cout << "  calls " << timing.cuda.calls
                      << ", candidates " << timing.cuda.candidates
                      << ", total " << timing.cuda.total_seconds << " s\n";
            std::cout << "  context init " << timing.cuda.context_seconds
                      << " s, host setup " << timing.cuda.host_setup_seconds
                      << " s, H2D " << timing.cuda.host_to_device_seconds
                      << " s, kernel+sync " << timing.cuda.kernel_seconds
                      << " s, D2H " << timing.cuda.device_to_host_seconds
                      << " s, cleanup " << timing.cuda.cleanup_seconds << " s\n";
        }
        print_dls_detail();
        return;
    }
    const double generation_evaluation =
        std::max(0.0, timing.evaluation_seconds - timing.initial_evaluation_seconds);
    std::cout << "DE timing detail:\n";
    std::cout << "  setup " << timing.setup_seconds
              << " s, initial eval " << timing.initial_evaluation_seconds
              << " s, generation eval " << generation_evaluation
              << " s\n";
    std::cout << "  trial generation " << timing.trial_generation_seconds
              << " s, physical conversion " << timing.physical_conversion_seconds
              << " s, selection " << timing.selection_seconds
              << " s\n";
    if (timing.trial_mating_pool_seconds > 0.0 ||
        timing.trial_mating_plan_seconds > 0.0 ||
        timing.trial_pbest_seconds > 0.0 ||
        timing.trial_mating_select_seconds > 0.0) {
        std::cout << "  trial detail: mating pools "
                  << timing.trial_mating_pool_seconds
                  << " s, plans " << timing.trial_mating_plan_seconds
                  << " s, pbest " << timing.trial_pbest_seconds
                  << " s, mating select " << timing.trial_mating_select_seconds
                  << " s, mutation/rng " << timing.trial_mutation_seconds
                  << " s\n";
    }
    if (timing.topology_similarity_cache_hits > 0 ||
        timing.topology_similarity_cache_misses > 0 ||
        timing.topology_shape_diff_cache_hits > 0 ||
        timing.topology_shape_diff_cache_misses > 0) {
        std::cout << "  topology pair cache: similarity hits "
                  << timing.topology_similarity_cache_hits
                  << ", misses " << timing.topology_similarity_cache_misses
                  << ", shape-diff hits "
                  << timing.topology_shape_diff_cache_hits
                  << ", misses " << timing.topology_shape_diff_cache_misses
                  << "\n";
    }
    if (timing.selection_seconds > 0.0) {
        std::cout << "  selection detail: family gate "
                  << timing.selection_family_gate_seconds
                  << " s, compare " << timing.selection_compare_seconds
                  << " s, update " << timing.selection_update_seconds
                  << " s, basin " << timing.selection_basin_seconds
                  << " s, finalize " << timing.selection_finalize_seconds
                  << " s\n";
    }
    if (timing.basin_consider_seconds > 0.0 || timing.basin_archive_stage_seconds > 0.0) {
        std::cout << "  basin detail: consider " << timing.basin_consider_seconds
                  << " s, diversity " << timing.basin_diversity_match_seconds
                  << " s, distance " << timing.basin_distance_seconds
                  << " s, make elite " << timing.basin_make_elite_seconds
                  << " s, trim " << timing.basin_trim_seconds
                  << " s, archive stage " << timing.basin_archive_stage_seconds
                  << " s, new " << timing.basin_new_count
                  << ", replace " << timing.basin_replace_count
                  << ", trim count " << timing.basin_trim_count << "\n";
    }
    std::cout << "  best re-score " << timing.best_rescore_seconds
              << " s, attach population " << timing.attach_population_seconds
              << " s, eval calls " << timing.evaluation_calls
              << ", candidates " << timing.evaluated_candidates << "\n";
    print_dls_detail();
    if (timing.topology_key_builds > 0 || timing.topology_descriptor_builds > 0) {
        std::cout << "  topology keys " << timing.topology_key_seconds
                  << " s / " << timing.topology_key_builds
                  << " builds, descriptors " << timing.topology_descriptor_seconds
                  << " s / " << timing.topology_descriptor_builds
                  << " builds, descriptor selectors "
                  << timing.topology_descriptor_selector_seconds
                  << " s / " << timing.topology_descriptor_selector_calls
                  << " calls\n";
    }
    if (timing.cuda.calls > 0) {
        std::cout << "CUDA evaluation detail:\n";
        std::cout << "  calls " << timing.cuda.calls
                  << ", candidates " << timing.cuda.candidates
                  << ", total " << timing.cuda.total_seconds << " s\n";
        std::cout << "  context init " << timing.cuda.context_seconds
                  << " s, host setup " << timing.cuda.host_setup_seconds
                  << " s, H2D " << timing.cuda.host_to_device_seconds
                  << " s, kernel+sync " << timing.cuda.kernel_seconds
                  << " s, D2H " << timing.cuda.device_to_host_seconds
                  << " s, cleanup " << timing.cuda.cleanup_seconds << " s\n";
    }
}

double ns_to_seconds(long long ns) {
    return static_cast<double>(ns) * 1.0e-9;
}

} } }
