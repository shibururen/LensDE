#pragma once

#include "optiland/core.hpp"

namespace optiland {

struct QuickFocusResult {
    LensSystem system;
    double best_thickness = 0.0;
    Score score;
};

QuickFocusResult quick_focus(
    const LensSystem& system,
    const MeritConfig& config,
    double lower,
    double upper,
    int samples = 48);

}  // namespace optiland
