#pragma once
#include "../prelude.cuh"
#include "constants.cuh"
#include "../diagnostics/builder_instrumentation.cuh"

namespace optiland {
namespace {
struct DeviceField {
    double hx = 0.0;
    double hy = 0.0;
    double fvcx = 0.0;
    double fvcy = 0.0;
    double fvdx = 0.0;
    double fvdy = 0.0;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
// Every chief-referenced metric uses the configured vignetted pupil center.
// Unvignetted principal rays are not part of the production score contract.
__device__ bool d_field_radius_matches_anchor(double radius, double anchor) {
    const double tolerance =
        1.0e-12 * fmax(1.0, fmax(fabs(radius), fabs(anchor)));
    return isfinite(radius) && isfinite(anchor) &&
           fabs(radius - anchor) <= tolerance;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ void d_field_radius_extrema(
    const DeviceField* fields,
    const DeviceConfig& cfg,
    double* minimum,
    double* maximum);
} // namespace
} // namespace optiland

namespace optiland {
namespace {
// Calibrated-paraxial sampling models cover the pupil domain that is actually
// scored.  Raw pupil coordinates remain interpolation coordinates; candidate
// vignetting maps them to physical stop targets during model calibration.
// Chief-ray metrics retain the field vignetting map: raw pupil (0, 0) maps to
// the center of the vignetted pupil rather than the unvignetted stop center.
struct DeviceWave {
    double value_um = 0.5875618;
    double weight = 1.0;
    int primary = 0;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
struct DeviceVariable {
    int kind = 0;
    int index = 0;
    int term = 0;
    int glass_surface_index = -1;
    int glass_candidate_offset = 0;
    int glass_candidate_count = 0;
    int glass_initial_choice = 0;
    double lower = 0.0;
    double upper = 0.0;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
struct DeviceGlassCandidate {
    double nd = 1.5;
    double vd = 50.0;
    double indices[kMaxDeviceWaves] = {};
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
struct DeviceGlassProximityPoint {
    double nd = 1.5;
    double vd = 50.0;
};
} // namespace
} // namespace optiland

namespace optiland {
namespace {
struct DeviceGlassProximity {
    int index = 0;
    int offset = 0;
    int count = 0;
    int region_offset = 0;
    int region_count = 0;
    int vd_metric = 0;
    double weight = 1.0;
    double tolerance = 0.0;
    double region_weight = 0.0;
    double region_tolerance = 0.0;
    double nd_scale = 0.01;
    double vd_scale = 5.0;
    double vd_relative_scale = 0.05;
    double smooth_temperature = 0.0;
};
} // namespace
} // namespace optiland
