#pragma once

namespace optiland {

// Numerical safety domain used by the CPU and CUDA even-asphere root solvers.
// This is deliberately more permissive than the independent, merit-driven
// surface_tangent_angle_lower_deg manufacturability constraint.
inline constexpr double kAsphereRootDomainMinimumTangentAxisAngleDeg = 20.0;
inline constexpr double kAsphereRootDomainMaximumRadialSlope =
    2.7474774194546222787616640264977;  // cot(20 degrees)
inline constexpr double kAsphereRootDomainMaximumRadialSlopeConservative =
    kAsphereRootDomainMaximumRadialSlope * (1.0 + 1.0e-12);

}  // namespace optiland
