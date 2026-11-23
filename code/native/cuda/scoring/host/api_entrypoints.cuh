#pragma once
#include "../prelude.cuh"
#include "execution_context.cuh"

namespace optiland {
bool evaluate_population_cuda_scores(
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const MeritConfig& merit_config,
    const std::vector<double>& flat_population,
    int population_size,
    std::vector<double>* merits,
    std::vector<CudaScoreSummary>* summaries,
    CudaEvaluationTiming* timing,
    const GlassSearchContext* glass_search) {
    static std::mutex evaluation_cuda_mutex;
    const std::lock_guard<std::mutex> evaluation_cuda_lock(evaluation_cuda_mutex);
    return PopulationScoringExecution(
        base,
        variables,
        merit_config,
        flat_population,
        population_size,
        merits,
        summaries,
        timing,
        glass_search
    ).run();
}
} // namespace optiland

namespace optiland {
bool evaluate_population_cuda(
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const MeritConfig& merit_config,
    const std::vector<double>& flat_population,
    int population_size,
    std::vector<double>* merits,
    CudaEvaluationTiming* timing,
    const GlassSearchContext* glass_search) {
    std::vector<CudaScoreSummary> summaries;
    return evaluate_population_cuda_scores(
        base,
        variables,
        merit_config,
        flat_population,
        population_size,
        merits,
        &summaries,
        timing,
        glass_search);
}
} // namespace optiland
