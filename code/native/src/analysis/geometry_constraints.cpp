/*
 * Evaluates surface/interval geometry for thickness, sag, diameter and tangent limits.
 * Manufacturing merit stages and bound operands query these shared measurements.
 * Object/image intervals, independent air stops and clear versus mechanical radii
 * have separate rules; unavailable constraint values keep their NaN convention.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
double surface_vertex_z(const LensSystem& system, std::size_t surface_index) {
    const std::size_t first = system.first_optical_surface();
    double z = 0.0;
    for (std::size_t i = first; i < surface_index && i < system.surfaces.size(); ++i) {
        const Surface& surface = system.surfaces[i];
        if (std::isfinite(surface.thickness)) {
            z += surface.thickness;
        }
    }
    return z;
}
}  // namespace analysis_detail

namespace analysis_detail {
double field_angle_radial_rad(const LensSystem& system, const Field& field) {
    return std::abs(deg_to_rad(system.max_field_deg)) * std::hypot(field.hx, field.hy);
}
}  // namespace analysis_detail

namespace analysis_detail {
std::pair<double, double> field_transverse_unit(const Field& field) {
    const double field_radius = std::hypot(field.hx, field.hy);
    if (field_radius <= kEps) {
        return {0.0, 0.0};
    }
    return {field.hx / field_radius, field.hy / field_radius};
}
}  // namespace analysis_detail

namespace analysis_detail {
double clear_aperture_radius(const Surface& surface) {
    return std::max(surface.semi_aperture, surface.aperture_radius);
}
}  // namespace analysis_detail

namespace analysis_detail {
double mechanical_aperture_radius(const Surface& surface) {
    if (surface.mechanical_aperture > 0.0) {
        return surface.mechanical_aperture;
    }
    return clear_aperture_radius(surface);
}
}  // namespace analysis_detail

namespace analysis_detail {
double edge_sample_aperture_radius(const Surface& surface) {
    return std::max(mechanical_aperture_radius(surface), clear_aperture_radius(surface));
}
}  // namespace analysis_detail

namespace analysis_detail {
double edge_sag_radius(const Surface& surface, double fallback_radius) {
    double radius = clear_aperture_radius(surface);
    if (radius <= 0.0) {
        radius = mechanical_aperture_radius(surface);
    }
    if (radius <= 0.0) {
        radius = fallback_radius;
    }
    return std::max(0.0, radius);
}
}  // namespace analysis_detail

namespace analysis_detail {
double aperture_limited_edge_sag(
    const Surface& surface,
    double sample_radius,
    double fallback_radius) {
    const double profile_radius =
        edge_sag_radius(surface, fallback_radius);
    const double effective_radius =
        sample_radius > 0.0 ? sample_radius : profile_radius;
    return profile_surface_sag(surface, effective_radius, profile_radius);
}
}  // namespace analysis_detail

namespace analysis_detail {
double edge_thickness_sample_radius(
    const Surface& a,
    const Surface& b,
    double fallback_radius) {
    const double radius_a = edge_sample_aperture_radius(a);
    const double radius_b = edge_sample_aperture_radius(b);
    double radius = 0.0;
    if (interval_is_glass(a)) {
        radius = std::max(radius_a, radius_b);
    } else if (radius_a > 0.0 && radius_b > 0.0) {
        radius = std::min(radius_a, radius_b);
    } else {
        radius = std::max(radius_a, radius_b);
    }
    if (radius <= 0.0) {
        radius = fallback_radius;
    }
    return std::max(0.0, radius);
}
}  // namespace analysis_detail

namespace analysis_detail {
double edge_thickness_at(const LensSystem& system, std::size_t i) {
    if (i + 1 >= system.surfaces.size()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const auto& a = system.surfaces[i];
    const auto& b = system.surfaces[i + 1];
    if (!std::isfinite(a.thickness) || a.kind == SurfaceKind::Object || b.kind == SurfaceKind::Object) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double fallback_radius = system.entrance_pupil_radius();
    const double r_max = edge_thickness_sample_radius(a, b, fallback_radius);
    if (!(r_max > 0.0)) {
        const double sag_a =
            aperture_limited_edge_sag(a, 0.0, fallback_radius);
        const double sag_b =
            aperture_limited_edge_sag(b, 0.0, fallback_radius);
        if (!std::isfinite(sag_a) || !std::isfinite(sag_b)) {
            return -std::numeric_limits<double>::infinity();
        }
        return a.thickness + sag_b - sag_a;
    }
    constexpr int kIntervals = 32;
    double min_edge = std::numeric_limits<double>::infinity();
    for (int sample = 0; sample <= kIntervals; ++sample) {
        const double fraction = 0.5 + 0.5 * static_cast<double>(sample) / kIntervals;
        const double radius = r_max * fraction;
        const double sag_a =
            aperture_limited_edge_sag(a, radius, fallback_radius);
        const double sag_b =
            aperture_limited_edge_sag(b, radius, fallback_radius);
        if (!std::isfinite(sag_a) || !std::isfinite(sag_b)) {
            return -std::numeric_limits<double>::infinity();
        }
        min_edge = std::min(min_edge, a.thickness + sag_b - sag_a);
    }
    return min_edge;
}
}  // namespace analysis_detail

namespace analysis_detail {
double stop_air_edge_thickness_at(const LensSystem& system, std::size_t stop) {
    if (stop == 0 || stop + 1 >= system.surfaces.size()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const auto& a = system.surfaces[stop - 1];
    const auto& aperture = system.surfaces[stop];
    const auto& b = system.surfaces[stop + 1];
    if (!aperture.is_stop || !aperture.is_plane() || interval_is_glass(a) ||
        interval_is_glass(aperture) || a.kind == SurfaceKind::Object ||
        b.kind == SurfaceKind::Image || !std::isfinite(a.thickness) ||
        !std::isfinite(aperture.thickness)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double fallback = system.entrance_pupil_radius();
    const double radius = edge_thickness_sample_radius(a, b, fallback);
    const double center = a.thickness + aperture.thickness;
    double minimum = center;
    // Include both clear-profile knees as well as the common mechanical rim.
    // The center is already included in minimum. The adjacent-edge helper
    // treats radius zero as a request for each face's own rim, not the axis.
    for (int sample = 1; sample <= 66; ++sample) {
        const double r = sample <= 64 ? radius * sample / 64.0
            : std::min(radius, edge_sag_radius(sample == 65 ? a : b, fallback));
        const double za = aperture_limited_edge_sag(a, r, fallback);
        const double zb = aperture_limited_edge_sag(b, r, fallback);
        if (!std::isfinite(za) || !std::isfinite(zb)) {
            return -std::numeric_limits<double>::infinity();
        }
        minimum = std::min(minimum, center + zb - za);
    }
    return minimum;
}
}  // namespace analysis_detail

namespace analysis_detail {
bool image_space_interval(const LensSystem& system, std::size_t i) {
    return i + 1 < system.surfaces.size() &&
           system.surfaces[i + 1].kind == SurfaceKind::Image;
}
}  // namespace analysis_detail

namespace analysis_detail {
double glass_edge_reference_radius(const LensSystem& system, std::size_t i) {
    if (i + 1 >= system.surfaces.size()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const auto& a = system.surfaces[i];
    const auto& b = system.surfaces[i + 1];
    const double fallback_radius = system.entrance_pupil_radius();
    return edge_thickness_sample_radius(a, b, fallback_radius);
}
}  // namespace analysis_detail

namespace analysis_detail {
double glass_interval_outer_diameter(const LensSystem& system, std::size_t i) {
    const double radius = glass_edge_reference_radius(system, i);
    return radius > 0.0 ? 2.0 * radius : std::numeric_limits<double>::quiet_NaN();
}
}  // namespace analysis_detail

namespace analysis_detail {
double glass_center_thickness_od_ratio(const LensSystem& system, std::size_t i) {
    if (i >= system.surfaces.size() || !interval_is_glass(system.surfaces[i])) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double diameter = glass_interval_outer_diameter(system, i);
    return diameter > 0.0 ? system.surfaces[i].thickness / diameter
                          : std::numeric_limits<double>::quiet_NaN();
}
}  // namespace analysis_detail

namespace analysis_detail {
double glass_edge_thickness_od_ratio(const LensSystem& system, std::size_t i) {
    if (i >= system.surfaces.size() || !interval_is_glass(system.surfaces[i])) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double diameter = glass_interval_outer_diameter(system, i);
    const double edge_value = edge_thickness_at(system, i);
    return diameter > 0.0 ? edge_value / diameter
                          : std::numeric_limits<double>::quiet_NaN();
}
}  // namespace analysis_detail

namespace analysis_detail {
bool independent_air_stop_surface(const Surface& surface) {
    return surface.is_stop && !interval_is_glass(surface);
}
}  // namespace analysis_detail

namespace analysis_detail {
double surface_bl0_ratio(const Surface& surface) {
    if (surface.kind == SurfaceKind::Object || surface.kind == SurfaceKind::Image ||
        independent_air_stop_surface(surface)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (surface.is_plane()) {
        return 0.0;
    }
    double aperture_radius = clear_aperture_radius(surface);
    if (!(aperture_radius > 0.0)) {
        aperture_radius = mechanical_aperture_radius(surface);
    }
    if (!(aperture_radius > 0.0)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double radius = std::abs(surface.radius);
    if (!std::isfinite(radius)) {
        return 0.0;
    }
    if (radius <= kEps) {
        return std::numeric_limits<double>::infinity();
    }
    return aperture_radius / radius;
}
}  // namespace analysis_detail

namespace analysis_detail {
double minimum_surface_tangent_axis_angle_deg(
    const Surface& surface,
    int sample_count) {
    if (surface.kind == SurfaceKind::Object || surface.kind == SurfaceKind::Image) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (surface.is_plane()) {
        return 90.0;
    }

    const double aperture_radius = clear_aperture_radius(surface);
    if (!(aperture_radius > 0.0) || !std::isfinite(aperture_radius)) {
        // There is no defined off-axis manufacturing zone yet.  The axial
        // tangent is perpendicular to the optical axis and is therefore safe.
        return 90.0;
    }

    const int samples = std::clamp(sample_count, 2, 4096);
    double min_angle_deg = 90.0;
    for (int sample = 0; sample < samples; ++sample) {
        const double r = aperture_radius * static_cast<double>(sample) /
                         static_cast<double>(samples - 1);
        double radial_slope = 0.0;
        if (std::isfinite(surface.radius) && std::abs(surface.radius) >= kEps) {
            const double root_arg =
                1.0 - (1.0 + surface.conic) * r * r /
                          (surface.radius * surface.radius);
            if (!(root_arg > 0.0)) {
                // At a hemispherical/conic domain rim the meridional tangent
                // is parallel to the optical axis; outside it the profile is
                // not manufacturable on the selected clear aperture.
                return 0.0;
            }
            radial_slope += r / (surface.radius * std::sqrt(root_arg));
        }
        if (surface.kind == SurfaceKind::EvenAsphere) {
            double radial_power = r;
            for (std::size_t i = 0; i < surface.asphere_coefficients.size(); ++i) {
                const double order = 2.0 * static_cast<double>(i + 1);
                radial_slope += order * surface.asphere_coefficients[i] * radial_power;
                radial_power *= r * r;
            }
        }
        if (!std::isfinite(radial_slope)) {
            return 0.0;
        }
        // Meridional tangent is proportional to (dr, dz)=(1, dz/dr).
        // Its acute angle to the z axis is atan2(|dr|, |dz|).
        const double angle_deg = rad_to_deg(std::atan2(1.0, std::abs(radial_slope)));
        min_angle_deg = std::min(min_angle_deg, angle_deg);
    }
    return min_angle_deg;
}
}  // namespace analysis_detail

namespace analysis_detail {
double surface_constraint_value(
    const LensSystem& system,
    const std::string& raw_kind,
    std::size_t index,
    std::size_t term) {
    if (index >= system.surfaces.size()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const std::string kind = normalized_name(raw_kind);
    const auto& surface = system.surfaces[index];
    if (kind == "radius") {
        return surface.radius;
    }
    if (kind == "thickness" || kind == "center_thickness") {
        return surface.thickness;
    }
    if (kind == "edge_thickness") {
        return edge_thickness_at(system, index);
    }
    if (kind == "stop_air_edge_thickness") {
        return stop_air_edge_thickness_at(system, index);
    }
    if (kind == "atc" || kind == "glass_atc" ||
        kind == "center_thickness_od_ratio" ||
        kind == "glass_center_thickness_od_ratio") {
        return glass_center_thickness_od_ratio(system, index);
    }
    if (kind == "ate" || kind == "glass_ate" ||
        kind == "edge_thickness_od_ratio" ||
        kind == "glass_edge_thickness_od_ratio") {
        return glass_edge_thickness_od_ratio(system, index);
    }
    if (kind == "bl0" || kind == "blocking_ratio" || kind == "surface_bl0") {
        return surface_bl0_ratio(surface);
    }
    if (kind == "clear_aperture" || kind == "clear_aperture_radius" ||
        kind == "semi_aperture") {
        return surface.semi_aperture;
    }
    if (kind == "mechanical_aperture" || kind == "mechanical_aperture_radius" ||
        kind == "mechanical_radius") {
        return mechanical_aperture_radius(surface);
    }
    if (kind == "aperture_radius" || kind == "hard_aperture") {
        return surface.aperture_radius;
    }
    if (kind == "material_nd" || kind == "nd") {
        return surface.material_after.nd;
    }
    if (kind == "material_vd" || kind == "vd" || kind == "abbe" || kind == "abbe_number") {
        return surface.material_after.vd;
    }
    if (kind == "conic") {
        return surface.conic;
    }
    if (kind == "asphere_coeff" || kind == "asphere" || kind == "a") {
        return term < surface.asphere_coefficients.size()
                   ? surface.asphere_coefficients[term]
                   : 0.0;
    }
    return std::numeric_limits<double>::quiet_NaN();
}
}  // namespace analysis_detail

namespace analysis_detail {
double constraint_value(const LensSystem& system, const BoundConstraint& bound) {
    return surface_constraint_value(system, bound.kind, bound.index, bound.term);
}
}  // namespace analysis_detail

}  // namespace optiland
