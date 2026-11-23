/*
 * Expose adaptive DE-DLS through DifferentialEvolution and create one AdaptiveSession per invocation.
 * The session borrows the solver/options and owns its run state; the progress callback is transferred once.
 * Keep this entry thin so initialization, RNG ownership and phase ordering remain inside the session.
 */
#include "optimizer/adaptive_session.hpp"

namespace optiland {
OptimizationResult DifferentialEvolution::run_adaptive_hybrid_de_dls(
    const AdaptiveHybridDeDlsOptions& options,
    ProgressCallback progress) {
    return optimizer_detail::AdaptiveSession(*this, options, std::move(progress)).execute();
}
} // namespace optiland
