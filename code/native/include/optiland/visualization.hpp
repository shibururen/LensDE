#pragma once

#include "optiland/analysis.hpp"
#include "optiland/core.hpp"
#include "optiland/raytrace.hpp"

#include <string>

namespace optiland {

bool save_layout_svg(
    const LensSystem& system,
    const std::string& path,
    int meridional_half_count = 1,
    int width = 1200,
    int height = 420);

bool save_spot_csv(
    const LensSystem& system,
    const std::string& path,
    int num_points = 3,
    const std::string& distribution = "hexapolar",
    unsigned int seed = 1,
    int arms = 6);

bool save_raytrace_csv(
    const LensSystem& system,
    const std::string& path,
    int num_points = 3,
    const std::string& distribution = "hexapolar",
    unsigned int seed = 1,
    RayAimingMode mode = RayAimingMode::Paraxial,
    int arms = 6);

bool save_relative_illumination_csv(
    const RelativeIlluminationResult& result,
    const std::string& path);

}  // namespace optiland
