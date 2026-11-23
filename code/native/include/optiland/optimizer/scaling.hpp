#pragma once

namespace optiland {
enum class InternalScalingMode {
    LegacyOptical,
    BoundNormalized,
    // Keep non-asphere variables bound-normalized while expressing even-asphere
    // variables through a frozen edge-sag coordinate supplied by VariableSpec.
    EdgeSagNormalized,
};

enum class DistanceMetric {
    LegacyScaled,
    BoundNormalized,
    StructureBoundNormalized,
};


}  // namespace optiland
