#pragma once
#include "../prelude.cuh"
#include "../contracts/constants.cuh"
#include "../geometry/material_and_sag.cuh"
#include "intersection_gate.cuh"
#include "paraxial_generation.cuh"

namespace optiland {
namespace {
__device__ bool d_surface_normal(
    const DeviceSurface& surface,
    double x,
    double y,
    double* nx,
    double* ny,
    double* nz) {
    if (!nx || !ny || !nz) {
        return false;
    }
    if (d_is_plane(surface)) {
        *nx = 0.0;
        *ny = 0.0;
        *nz = 1.0;
        return true;
    }
    double dzdx = 0.0;
    double dzdy = 0.0;
    if (!d_surface_slopes(surface, x, y, &dzdx, &dzdy)) {
        *nx = 0.0;
        *ny = 0.0;
        *nz = 1.0;
        return true;
    }
    *nx = dzdx;
    *ny = dzdy;
    *nz = -1.0;
    d_normalize3(nx, ny, nz);
    return true;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_refract(
    double ix,
    double iy,
    double iz,
    double nx,
    double ny,
    double nz,
    double n1,
    double n2,
    double* ox,
    double* oy,
    double* oz) {
    if (!ox || !oy || !oz || fabs(n2) <= kDeviceEps) {
        return false;
    }
    d_normalize3(&ix, &iy, &iz);
    d_normalize3(&nx, &ny, &nz);
    double d = ix * nx + iy * ny + iz * nz;
    const double sgn = (d >= 0.0) ? 1.0 : -1.0;
    nx *= sgn;
    ny *= sgn;
    nz *= sgn;
    d = fabs(d);
    const double u = n1 / n2;
    const double root_arg = 1.0 - u * u * (1.0 - d * d);
    if (root_arg < 0.0) {
        return false;
    }
    const double root = sqrt(root_arg);
    *ox = u * ix + nx * root - u * nx * d;
    *oy = u * iy + ny * root - u * ny * d;
    *oz = u * iz + nz * root - u * nz * d;
    d_normalize3(ox, oy, oz);
    return true;
}
} // namespace
} // namespace optiland
