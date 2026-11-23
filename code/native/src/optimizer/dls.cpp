/*
 * Expose damped-least-squares polishing through the public DifferentialEvolution API.
 * Each invocation creates a DlsSession borrowing solver inputs and the initial physical values.
 * Keep session lifetime and progress-callback transfer local to this call; residual/trial state belongs to it.
 */
#include "dls_session.hpp"
namespace optiland {
OptimizationResult DifferentialEvolution::polish_damped_least_squares(
    const std::vector<double>& initial_values, const PolishOptions& options,
    ProgressCallback progress) const {
    return optimizer_detail::DlsSession(*this, initial_values, options, std::move(progress)).run();
}
}  // namespace optiland
