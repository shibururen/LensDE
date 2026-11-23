#pragma once
#include "optiland/core/numeric.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace optiland {
struct BoundConstraint {
    std::string kind;
    std::size_t index = 0;
    std::size_t term = 0;
    double lower = -kInf;
    double upper = kInf;
    double weight = 1000.0;
};

struct MeritOperand {
    std::string name;
    std::string mode = "target";
    std::size_t index = 0;
    std::size_t term = 0;
    double target = 0.0;
    double lower = -kInf;
    double upper = kInf;
    double weight = 1.0;
};

struct GlassProximityPoint {
    double nd = 1.5;
    double vd = 50.0;
};

struct GlassProximityConstraint {
    std::size_t index = 0;
    double weight = 1.0;
    double tolerance = 0.0;
    double region_weight = 0.0;
    double region_tolerance = 0.0;
    double nd_scale = 0.01;
    double vd_scale = 5.0;
    std::string vd_metric = "absolute";
    double vd_relative_scale = 0.05;
    double smooth_temperature = 0.0;
    std::vector<GlassProximityPoint> points;
    std::vector<GlassProximityPoint> region_points;
};

enum class TraceFailurePolicy {
    Strict,
    SoftCoverage,
    GeometryOnlyWhenTraceBad,
};

enum class MeritCategory {
    Trace,
    Constraint,
    FirstOrder,
    ImageQuality,
    Regularization,
    Other,
};


}  // namespace optiland
