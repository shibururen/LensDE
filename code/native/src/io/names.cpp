/*
 * Maps serialized surface, variable, aiming and merit names to native enums and back.
 * The file readers/writers share these canonical names and compatibility aliases.
 * Existing unknown-token fallbacks and distortion/wavefront normalization determine
 * how stored configuration text selects the corresponding native model behavior.
 */

#include "detail.hpp"

namespace optiland {
using namespace io_detail;

namespace io_detail {
const char* surface_kind_name(SurfaceKind kind) {
    switch (kind) {
        case SurfaceKind::Standard:
            return "standard";
        case SurfaceKind::Plane:
            return "plane";
        case SurfaceKind::EvenAsphere:
            return "even_asphere";
        case SurfaceKind::Image:
            return "image";
        case SurfaceKind::Object:
            return "object";
    }
    return "standard";
}
}  // namespace io_detail

namespace io_detail {
const char* ray_aiming_mode_name(RayAimingMode mode) {
    switch (mode) {
        case RayAimingMode::Direct:
            return "direct";
        case RayAimingMode::Paraxial:
            return "paraxial";
        case RayAimingMode::CalibratedParaxial:
            return "calibrated_paraxial";
        case RayAimingMode::ThreeStep:
            return "three_step";
        case RayAimingMode::Iterative:
            return "iterative";
        case RayAimingMode::Robust:
            return "robust";
        case RayAimingMode::AdaptivePilotGrid:
            return "adaptive_pilot_grid";
    }
    return "paraxial";
}
}  // namespace io_detail

namespace io_detail {
const char* ray_aiming_stop_radius_source_name(RayAimingStopRadiusSource source) {
    switch (source) {
        case RayAimingStopRadiusSource::Paraxial:
            return "paraxial";
        case RayAimingStopRadiusSource::Real:
            return "real";
    }
    return "paraxial";
}
}  // namespace io_detail

namespace io_detail {
std::string normalized_name(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return c == '-' || c == ' ' ? '_' : static_cast<char>(std::tolower(c));
    });
    return value;
}
}  // namespace io_detail

namespace io_detail {
std::string canonical_distortion_model(
    const std::string& model,
    const std::string& fallback ) {
    auto canonical = [](const std::string& raw) -> std::string {
        const std::string value = normalized_name(raw);
        if (value == "f_theta" || value == "ftheta" || value == "theta") {
            return "f_theta";
        }
        if (value == "f_tan_theta" || value == "f_tantheta" ||
            value == "ftan_theta" || value == "ftantheta" ||
            value == "tan_theta" || value == "tantheta") {
            return "f_tan_theta";
        }
        if (value == "auto") {
            return "auto";
        }
        return "";
    };
    const std::string parsed = canonical(model);
    if (!parsed.empty()) {
        return parsed;
    }
    const std::string fallback_parsed = canonical(fallback);
    return fallback_parsed.empty() ? "auto" : fallback_parsed;
}
}  // namespace io_detail

namespace io_detail {
std::string canonical_wavefront_reference(
    const std::string& reference,
    const std::string& fallback ) {
    auto canonical = [](const std::string& raw) -> std::string {
        const std::string value = normalized_name(raw);
        if (value == "chief" || value == "chief_ray" ||
            value == "chief_reference" || value == "chief_ray_reference") {
            return "chief_ray";
        }
        if (value == "centroid" || value == "centroid_sphere" ||
            value == "centroid_reference" || value == "centroid_reference_sphere") {
            return "centroid_sphere";
        }
        return "";
    };
    const std::string parsed = canonical(reference);
    if (!parsed.empty()) {
        return parsed;
    }
    const std::string fallback_parsed = canonical(fallback);
    return fallback_parsed.empty() ? "centroid_sphere" : fallback_parsed;
}
}  // namespace io_detail

namespace io_detail {
RayAimingMode ray_aiming_mode_from_string(const std::string& mode) {
    const std::string value = normalized_name(mode);
    if (value == "direct") {
        return RayAimingMode::Direct;
    }
    if (value == "iterative") {
        return RayAimingMode::Iterative;
    }
    if (value == "calibrated_paraxial" || value == "calibrated_paraxial_fast" ||
        value == "calibrated_paraxial_auto" ||
        value == "calibrated_paraxial_corrected" ||
        value == "field_pupil_plane" || value == "field_calibrated_paraxial") {
        return RayAimingMode::CalibratedParaxial;
    }
    if (value == "three_step" || value == "three_step_ray_aiming" ||
        value == "gao_three_step" || value == "gao_2025" ||
        value == "gaoneuro" || value == "opti_neuro" || value == "optineuro") {
        return RayAimingMode::ThreeStep;
    }
    if (value == "robust") {
        return RayAimingMode::Robust;
    }
    if (value == "adaptive" || value == "adaptive_pilot_grid" ||
        value == "pilot_grid" || value == "adaptive_pilot") {
        return RayAimingMode::AdaptivePilotGrid;
    }
    return RayAimingMode::Paraxial;
}
}  // namespace io_detail

namespace io_detail {
RayAimingStopRadiusSource ray_aiming_stop_radius_source_from_string(
    const std::string& source,
    RayAimingStopRadiusSource fallback ) {
    const std::string value = normalized_name(source);
    if (value == "real" || value == "real_ray" || value == "real_rays" ||
        value == "real_stop" || value == "real_stop_radius" ||
        value == "actual" || value == "actual_ray") {
        return RayAimingStopRadiusSource::Real;
    }
    if (value == "paraxial" || value == "paraxial_ray" || value == "paraxial_rays" ||
        value == "paraxial_stop" || value == "paraxial_stop_radius") {
        return RayAimingStopRadiusSource::Paraxial;
    }
    return fallback;
}
}  // namespace io_detail

namespace io_detail {
const char* trace_failure_policy_name(TraceFailurePolicy policy) {
    switch (policy) {
        case TraceFailurePolicy::Strict:
            return "strict";
        case TraceFailurePolicy::SoftCoverage:
            return "soft_coverage";
        case TraceFailurePolicy::GeometryOnlyWhenTraceBad:
            return "geometry_only_when_trace_bad";
    }
    return "strict";
}
}  // namespace io_detail

namespace io_detail {
TraceFailurePolicy trace_failure_policy_from_string(const std::string& raw) {
    const std::string value = normalized_name(raw);
    if (value == "soft" || value == "soft_coverage" || value == "coverage") {
        return TraceFailurePolicy::SoftCoverage;
    }
    if (value == "geometry_only" || value == "geometry_only_when_trace_bad" ||
        value == "geometry_when_trace_bad") {
        return TraceFailurePolicy::GeometryOnlyWhenTraceBad;
    }
    return TraceFailurePolicy::Strict;
}
}  // namespace io_detail

namespace io_detail {
const char* merit_category_name(MeritCategory category) {
    switch (category) {
        case MeritCategory::Trace:
            return "trace";
        case MeritCategory::Constraint:
            return "constraint";
        case MeritCategory::FirstOrder:
            return "first_order";
        case MeritCategory::ImageQuality:
            return "image_quality";
        case MeritCategory::Regularization:
            return "regularization";
        case MeritCategory::Other:
            return "other";
    }
    return "other";
}
}  // namespace io_detail

SurfaceKind surface_kind_from_string(const std::string& kind) {
    if (kind == "plane" || kind == "Plane") {
        return SurfaceKind::Plane;
    }
    if (kind == "even_asphere" || kind == "EvenAsphere") {
        return SurfaceKind::EvenAsphere;
    }
    if (kind == "image" || kind == "Image") {
        return SurfaceKind::Image;
    }
    if (kind == "object" || kind == "Object" || kind == "ObjectSurface") {
        return SurfaceKind::Object;
    }
    return SurfaceKind::Standard;
}

VariableKind variable_kind_from_string(const std::string& kind) {
    if (kind == "thickness") {
        return VariableKind::Thickness;
    }
    if (kind == "material_nd" || kind == "MaterialNd" || kind == "nd") {
        return VariableKind::MaterialNd;
    }
    if (kind == "material_vd" || kind == "MaterialVd" || kind == "vd") {
        return VariableKind::MaterialVd;
    }
    if (kind == "vignette_x" || kind == "VignetteX" || kind == "fvdx") {
        return VariableKind::VignetteX;
    }
    if (kind == "vignette_y" || kind == "VignetteY" || kind == "fvdy") {
        return VariableKind::VignetteY;
    }
    if (kind == "vignette_center_y" || kind == "VignetteCenterY" ||
        kind == "vignette_shift_y" || kind == "fvcy") {
        return VariableKind::VignetteCenterY;
    }
    if (kind == "vignette_magnitude_y" || kind == "VignetteMagnitudeY" ||
        kind == "vignetting_magnitude_y") {
        return VariableKind::VignetteMagnitudeY;
    }
    if (kind == "vignette_asymmetry_y" || kind == "VignetteAsymmetryY" ||
        kind == "vignetting_asymmetry_y") {
        return VariableKind::VignetteAsymmetryY;
    }
    if (kind == "conic" || kind == "Conic") {
        return VariableKind::Conic;
    }
    if (kind == "asphere_coeff" || kind == "asphere" || kind == "AsphereCoeff") {
        return VariableKind::AsphereCoeff;
    }
    if (kind == "curvature" || kind == "curv" || kind == "Curvature") {
        return VariableKind::Curvature;
    }
    if (kind == "glass_choice" || kind == "glass" || kind == "GlassChoice") {
        return VariableKind::GlassChoice;
    }
    return VariableKind::Radius;
}

}  // namespace optiland
