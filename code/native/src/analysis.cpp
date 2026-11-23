/*
 * Public merit-scoring entry point used by optimizers, checkpoints and CLI scoring.
 * Aligns the scoring copy with the configured primary-wavelength aperture contract,
 * then runs one analysis_detail::MeritEvaluation; the caller's lens remains unchanged.
 */

#include "analysis/merit_evaluation.hpp"

namespace optiland {
Score evaluate_merit(const LensSystem& system, const MeritConfig& config) {
    if (system.primary_wavelength_apertures != config.vignetting_optimization_enabled()) {
        LensSystem scoring_system = system;
        scoring_system.primary_wavelength_apertures = config.vignetting_optimization_enabled();
        return evaluate_merit(scoring_system, config);
    }
    return analysis_detail::MeritEvaluation(system, config).evaluate();
}
}  // namespace optiland
