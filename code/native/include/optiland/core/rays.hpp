#pragma once
#include "optiland/core/numeric.hpp"
#include <limits>

namespace optiland {
enum class RayFailureCode {
    None = 0,
    AimNotConverged,
    SurfaceMiss,
    ApertureClip,
    TotalInternalReflection,
    NonFinite,
    BackwardPropagation,
    SagInvalid,
    GeometryInvalid,
};

struct Ray {
    Vec3 p;
    Vec3 d = {0.0, 0.0, 1.0};
    double intensity = 1.0;
    double nominal_weight = 1.0;
    double wavelength_um = 0.5875618;
    double opd = 0.0;
    bool valid = true;
    // Geometry and physical transmission are separate for secondary waves
    // during vignetting optimization. A blocked ray can still trace normally.
    bool aperture_blocked = false;
    int aperture_blocked_surface = -1;
    double edge_prediction_scale = 1.0;
    bool aim_converged = true;
    double aim_residual = 0.0;
    double aim_target_radius = 1.0;
    double homotopy_fraction = 1.0;
    int aim_iterations = 0;
    RayFailureCode failure_code = RayFailureCode::None;
    int failure_surface = -1;
    double failure_severity = 0.0;
    bool backward_propagation = false;
    int backward_surface = -1;
    double backward_severity = 0.0;
    // Same-height surface separation at backward steps, independent of the
    // clear-aperture envelope. Zero means no resolved physical overlap.
    double min_backward_air_gap = 0.0;
    double min_backward_glass_gap = 0.0;
    // Actual intersection with the physical aperture-stop surface.  This is
    // recorded before aperture clipping/refraction so callers can distinguish
    // a real in-stop downstream failure from a rough pupil-mapping miss.
    double stop_x = std::numeric_limits<double>::quiet_NaN();
    double stop_y = std::numeric_limits<double>::quiet_NaN();
    bool stop_reached = false;
};


}  // namespace optiland
