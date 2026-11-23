/*
 * Expose the fixed-stage hybrid DE-DLS algorithm through DifferentialEvolution.
 * One HybridSession borrows solver/options and owns population, random stream and phase statistics.
 * Keep the public wrapper thin so DE, polish and refresh continue to run in the session-defined sequence.
 */
#include "hybrid_session.hpp"
namespace optiland {
OptimizationResult DifferentialEvolution::run_hybrid_de_dls(
    const HybridDeDlsOptions& options, ProgressCallback progress) {
    return optimizer_detail::HybridSession(*this, options, std::move(progress)).run();
}
}  // namespace optiland
