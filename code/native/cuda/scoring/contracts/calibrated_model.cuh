#pragma once
#include "../prelude.cuh"
#include "../diagnostics/builder_instrumentation.cuh"

namespace optiland {
namespace {
struct DeviceSeed {
    double x = 0.0;
    double y = 0.0;
    int valid = 0;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
struct DeviceCalibratedEdgeAnchor {
    double px = 0.0;
    double py = 0.0;
    DeviceSeed seed;
    double stop_x = NAN;
    double stop_y = NAN;
    double correction_x = 0.0;
    double correction_y = 0.0;
    int valid = 0;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
struct DeviceCalibratedModel {
    int valid = 0;
    int calibrated_edges_all_valid = 0;
    DeviceSeed chief_seed;
    double chief_stop_x = 0.0;
    double chief_stop_y = 0.0;
    double fit_rms = INFINITY;
    DeviceCalibratedEdgeAnchor edge_anchors[4];
};
} // namespace
} // namespace optiland
