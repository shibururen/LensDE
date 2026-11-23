/*
 * Provides glass proximity/region distances and grouped mechanical-aperture updates.
 * Used by material merit constraints and clear-aperture materialization.
 * Glass interval classification, nd/Vd distance scales and grouped surface extents
 * must remain consistent with the materials and apertures stored in LensSystem.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
double glass_proximity_distance(
    const LensSystem& system,
    const GlassProximityConstraint& constraint) {
    if (constraint.index >= system.surfaces.size() || constraint.points.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const Material& material = system.surfaces[constraint.index].material_after;
    if (!std::isfinite(material.nd) || !std::isfinite(material.vd) ||
        material.nd <= 1.0005 || material.vd <= 0.0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double nd_scale = std::max(1.0e-12, std::abs(constraint.nd_scale));
    const double vd_scale = std::max(1.0e-12, std::abs(constraint.vd_scale));
    const double vd_relative_scale =
        std::max(1.0e-12, std::abs(constraint.vd_relative_scale));
    const std::string vd_metric = normalized_name(constraint.vd_metric);
    auto vd_component = [&](double point_vd) {
        if (vd_metric == "relative_log" || vd_metric == "relative" || vd_metric == "log") {
            return std::log(material.vd / point_vd) / vd_relative_scale;
        }
        return (material.vd - point_vd) / vd_scale;
    };
    double best = std::numeric_limits<double>::infinity();
    for (const auto& point : constraint.points) {
        if (!std::isfinite(point.nd) || !std::isfinite(point.vd) ||
            point.nd <= 1.0 || point.vd <= 0.0) {
            continue;
        }
        const double d_nd = (material.nd - point.nd) / nd_scale;
        const double d_vd = vd_component(point.vd);
        best = std::min(best, std::sqrt(d_nd * d_nd + d_vd * d_vd));
    }
    if (!std::isfinite(best) || constraint.smooth_temperature <= 0.0) {
        return best;
    }
    const double temperature = std::max(1.0e-12, constraint.smooth_temperature);
    double weighted_sum = 0.0;
    double weight_sum = 0.0;
    for (const auto& point : constraint.points) {
        if (!std::isfinite(point.nd) || !std::isfinite(point.vd) ||
            point.nd <= 1.0 || point.vd <= 0.0) {
            continue;
        }
        const double d_nd = (material.nd - point.nd) / nd_scale;
        const double d_vd = vd_component(point.vd);
        const double distance = std::sqrt(d_nd * d_nd + d_vd * d_vd);
        const double weight = std::exp(-(distance - best) / temperature);
        weighted_sum += weight * distance;
        weight_sum += weight;
    }
    return weight_sum > 0.0 ? weighted_sum / weight_sum : best;
}
}  // namespace analysis_detail

namespace analysis_detail {
double glass_region_distance(
    const LensSystem& system,
    const GlassProximityConstraint& constraint) {
    if (constraint.index >= system.surfaces.size() ||
        constraint.region_points.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const Material& material = system.surfaces[constraint.index].material_after;
    if (!std::isfinite(material.nd) || !std::isfinite(material.vd) ||
        material.nd <= 1.0005 || material.vd <= 0.0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double nd_scale = std::max(1.0e-12, std::abs(constraint.nd_scale));
    const double vd_scale = std::max(1.0e-12, std::abs(constraint.vd_scale));
    const double vd_relative_scale =
        std::max(1.0e-12, std::abs(constraint.vd_relative_scale));
    const std::string vd_metric = normalized_name(constraint.vd_metric);
    const bool use_relative_log_vd =
        vd_metric == "relative_log" || vd_metric == "relative" || vd_metric == "log";
    auto normalized_point = [&](double nd, double vd) {
        return std::pair<double, double>{
            nd / nd_scale,
            use_relative_log_vd ? std::log(vd) / vd_relative_scale : vd / vd_scale};
    };

    std::vector<std::pair<double, double>> polygon;
    polygon.reserve(constraint.region_points.size());
    for (const auto& point : constraint.region_points) {
        if (!std::isfinite(point.nd) || !std::isfinite(point.vd) ||
            point.nd <= 1.0 || point.vd <= 0.0) {
            continue;
        }
        polygon.push_back(normalized_point(point.nd, point.vd));
    }
    if (polygon.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const auto query = normalized_point(material.nd, material.vd);
    auto segment_distance = [&](const auto& a, const auto& b) {
        const double dx = b.first - a.first;
        const double dy = b.second - a.second;
        const double length_sq = dx * dx + dy * dy;
        const double projection = length_sq > 0.0
                                      ? std::clamp(
                                            ((query.first - a.first) * dx +
                                             (query.second - a.second) * dy) /
                                                length_sq,
                                            0.0,
                                            1.0)
                                      : 0.0;
        const double ex = query.first - (a.first + projection * dx);
        const double ey = query.second - (a.second + projection * dy);
        return std::sqrt(ex * ex + ey * ey);
    };

    if (polygon.size() == 1) {
        return segment_distance(polygon.front(), polygon.front());
    }
    if (polygon.size() == 2) {
        return segment_distance(polygon[0], polygon[1]);
    }
    double edge_distance = std::numeric_limits<double>::infinity();
    bool inside = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const auto& a = polygon[j];
        const auto& b = polygon[i];
        edge_distance = std::min(edge_distance, segment_distance(a, b));
        const bool crosses = (a.second > query.second) != (b.second > query.second);
        if (crosses) {
            const double crossing_x =
                a.first + (query.second - a.second) *
                              (b.first - a.first) / (b.second - a.second);
            if (query.first < crossing_x) {
                inside = !inside;
            }
        }
    }
    return inside || edge_distance <= 1.0e-12 ? 0.0 : edge_distance;
}
}  // namespace analysis_detail

namespace analysis_detail {
bool interval_is_glass(const Surface& surface) {
    return surface.material_after.refractive_index(0.5875618) > 1.0005;
}
}  // namespace analysis_detail

namespace analysis_detail {
void update_mechanical_apertures_from_groups(LensSystem& system) {
    const double wave = system.primary_wavelength();
    std::vector<bool> in_glass_element(system.surfaces.size(), false);
    bool in_element = false;
    std::size_t first = 0;

    for (std::size_t i = 0; i < system.surfaces.size(); ++i) {
        if (system.surfaces[i].kind == SurfaceKind::Object) {
            continue;
        }
        const bool after_is_glass =
            system.surfaces[i].material_after.refractive_index(wave) > 1.0005;
        if (!in_element && after_is_glass) {
            in_element = true;
            first = i;
        }
        if (in_element && (!after_is_glass || i + 1 == system.surfaces.size())) {
            const std::size_t last = i;
            double element_radius = 0.0;
            for (std::size_t si = first; si <= last && si < system.surfaces.size(); ++si) {
                element_radius = std::max(element_radius, clear_aperture_radius(system.surfaces[si]));
            }
            for (std::size_t si = first; si <= last && si < system.surfaces.size(); ++si) {
                in_glass_element[si] = true;
                if (!system.surfaces[si].mechanical_aperture_fixed && element_radius > 0.0) {
                    system.surfaces[si].mechanical_aperture = element_radius;
                }
            }
            in_element = false;
        }
    }

    for (std::size_t i = 0; i < system.surfaces.size(); ++i) {
        auto& surface = system.surfaces[i];
        if (surface.kind == SurfaceKind::Object || surface.mechanical_aperture_fixed ||
            in_glass_element[i]) {
            continue;
        }
        const double clear_radius = clear_aperture_radius(surface);
        if (clear_radius > 0.0) {
            surface.mechanical_aperture = clear_radius;
        }
    }
}
}  // namespace analysis_detail

}  // namespace optiland
