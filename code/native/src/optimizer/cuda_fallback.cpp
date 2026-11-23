/*
 * Provide the CPU-build implementations of the public CUDA population-evaluation entry points.
 * The optimizer evaluation layer links these fallbacks when OPTILAND_WITH_CUDA is not enabled.
 * Keep availability and failure-result semantics aligned so callers choose their CPU evaluation path.
 */
#include "support.hpp"

namespace optiland {
using namespace optimizer_detail;

#ifndef OPTILAND_WITH_CUDA
bool evaluate_population_cuda_available() {
    return false;
}
#endif

#ifndef OPTILAND_WITH_CUDA
bool evaluate_population_cuda(
    const LensSystem&,
    const std::vector<VariableSpec>&,
    const MeritConfig&,
    const std::vector<double>&,
    int,
    std::vector<double>*,
    CudaEvaluationTiming*,
    const GlassSearchContext*) {
    return false;
}
#endif

#ifndef OPTILAND_WITH_CUDA
bool evaluate_population_cuda_scores(
    const LensSystem&,
    const std::vector<VariableSpec>&,
    const MeritConfig&,
    const std::vector<double>&,
    int,
    std::vector<double>*,
    std::vector<CudaScoreSummary>*,
    CudaEvaluationTiming*,
    const GlassSearchContext*) {
    return false;
}
#endif

}  // namespace optiland
