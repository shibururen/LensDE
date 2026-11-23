#pragma once
#include "optiland/core/rays.hpp"
#include <vector>

namespace optiland {
struct TracePoint {
    double z = 0.0;
    double x = 0.0;
    double y = 0.0;
    double l = 0.0;
    double m = 0.0;
    double n = 1.0;
    double intensity = 1.0;
    bool valid = true;
};

struct RayTraceResult {
    std::vector<Ray> rays;
    std::vector<std::vector<TracePoint>> history_by_surface;
};

struct PupilPoint {
    double px = 0.0;
    double py = 0.0;
    double weight = 1.0;
};


}  // namespace optiland
