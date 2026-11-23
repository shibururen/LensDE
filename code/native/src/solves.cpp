/*
 * Implements the CLI quick-focus solve by scoring a sampled rear-interval thickness.
 * Each candidate is a LensSystem copy evaluated by the normal merit pipeline;
 * sample order, endpoint inclusion and the existing finite-merit selection rule
 * define this bounded focus search and leave the input prescription unchanged.
 */

#include "optiland/solves.hpp"

#include "optiland/analysis.hpp"

#include <algorithm>
#include <cmath>

namespace optiland {

QuickFocusResult quick_focus(
    const LensSystem& system,
    const MeritConfig& config,
    double lower,
    double upper,
    int samples) {
    QuickFocusResult result;
    result.system = system;
    result.score.merit = config.fail_merit;

    if (system.surfaces.size() < 2 || samples <= 1) {
        result.score = evaluate_merit(system, config);
        return result;
    }

    const std::size_t focus_surface = system.surfaces.size() - 2;
    for (int i = 0; i < samples; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(samples - 1);
        const double value = lower + t * (upper - lower);
        LensSystem candidate = system;
        candidate.surfaces[focus_surface].thickness = value;
        Score score = evaluate_merit(candidate, config);
        if (score.merit < result.score.merit || !std::isfinite(result.score.merit)) {
            result.system = candidate;
            result.best_thickness = value;
            result.score = score;
        }
    }
    return result;
}

}  // namespace optiland
