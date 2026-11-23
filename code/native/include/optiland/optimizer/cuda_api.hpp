#pragma once
#include "optiland/core.hpp"
#include "optiland/optimizer/cuda_evaluation.hpp"
#include <vector>

namespace optiland {
bool evaluate_population_cuda_available();
bool evaluate_population_cuda(
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const MeritConfig& config,
    const std::vector<double>& flat_population,
    int population_size,
    std::vector<double>* merits,
    CudaEvaluationTiming* timing = nullptr,
    const GlassSearchContext* glass_search = nullptr);

bool evaluate_population_cuda_scores(
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const MeritConfig& config,
    const std::vector<double>& flat_population,
    int population_size,
    std::vector<double>* merits,
    std::vector<CudaScoreSummary>* summaries,
    CudaEvaluationTiming* timing = nullptr,
    const GlassSearchContext* glass_search = nullptr);


}  // namespace optiland
