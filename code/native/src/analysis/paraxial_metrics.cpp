/*
 * Computes effective focal length, paraxial focus/color, BFL and minimum edge thickness.
 * Merit measurement and optical reporting use these first-order/geometry summaries.
 * Primary wavelength, rear reference intervals and non-finite focus conventions
 * must agree with the same LensSystem prescription used for full ray tracing.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

double estimate_effective_focal_length(const LensSystem& system) {
    return paraxial_effective_focal_length(system, system.primary_wavelength());
}

double paraxial_focus_position(const LensSystem& system, double wavelength_um) {
    double y = 1.0;
    double u = 0.0;
    double z_current = -1.0;
    double vertex_z = 0.0;
    Material material_before = Material::air();
    double focus_z = std::numeric_limits<double>::quiet_NaN();
    for (std::size_t i = system.first_optical_surface(); i < system.surfaces.size(); ++i) {
        const Surface& surface = system.surfaces[i];
        y += u * (vertex_z - z_current);
        z_current = vertex_z;
        if (surface.kind != SurfaceKind::Object && surface.kind != SurfaceKind::Image) {
            const double n1 = material_before.refractive_index(wavelength_um);
            const double n2 = surface.material_after.refractive_index(wavelength_um);
            if (std::abs(n2) > kEps) {
                const double power =
                    (std::isfinite(surface.radius) && std::abs(surface.radius) >= kEps)
                        ? (n2 - n1) / surface.radius
                        : 0.0;
                u = (n1 * u - y * power) / n2;
                if (std::abs(u) > kEps) {
                    focus_z = z_current - y / u;
                }
            }
        }
        material_before = surface.material_after;
        if (std::isfinite(surface.thickness)) {
            vertex_z += surface.thickness;
        }
    }
    return focus_z;
}

double paraxial_axial_color_focus_shift(const LensSystem& system) {
    double min_focus = std::numeric_limits<double>::infinity();
    double max_focus = -std::numeric_limits<double>::infinity();
    std::size_t active_count = 0;
    std::size_t valid_count = 0;
    if (system.wavelengths.empty()) {
        const double focus = paraxial_focus_position(system, system.primary_wavelength());
        return std::isfinite(focus) ? 0.0 : std::numeric_limits<double>::quiet_NaN();
    }
    for (const Wavelength& wave : system.wavelengths) {
        if (wave.weight <= 0.0) {
            continue;
        }
        ++active_count;
        const double focus = paraxial_focus_position(system, wave.value_um);
        if (!std::isfinite(focus)) {
            continue;
        }
        min_focus = std::min(min_focus, focus);
        max_focus = std::max(max_focus, focus);
        ++valid_count;
    }
    return active_count > 0 && valid_count == active_count
               ? std::max(0.0, max_focus - min_focus)
               : std::numeric_limits<double>::quiet_NaN();
}

double back_focal_length(const LensSystem& system) {
    if (system.surfaces.size() < 2) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const std::size_t last_interval = system.surfaces.size() - 2;
    const std::size_t first_interval = system.first_optical_surface();
    std::size_t reference = last_interval;
    if (system.bfl_reference_surface >= 0) {
        reference = std::clamp(
            static_cast<std::size_t>(system.bfl_reference_surface),
            std::min(first_interval, last_interval),
            last_interval);
    }

    double center_path = 0.0;
    double edge_path = 0.0;
    bool edge_path_finite = true;
    for (std::size_t index = reference; index <= last_interval; ++index) {
        const double center = system.surfaces[index].thickness;
        if (!std::isfinite(center)) {
            return center;
        }
        center_path += center;
        const double edge = edge_thickness_at(system, index);
        if (std::isfinite(edge)) {
            edge_path += edge;
        } else {
            edge_path_finite = false;
        }
    }
    return edge_path_finite ? std::min(center_path, edge_path) : center_path;
}

double min_edge_thickness(const LensSystem& system) {
    if (system.surfaces.size() < 2) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    double min_edge = std::numeric_limits<double>::infinity();
    const std::size_t first = system.first_optical_surface();
    for (std::size_t i = first; i + 1 < system.surfaces.size(); ++i) {
        const auto& a = system.surfaces[i];
        const auto& b = system.surfaces[i + 1];
        if (a.kind == SurfaceKind::Object || b.kind == SurfaceKind::Object ||
            image_space_interval(system, i)) {
            continue;
        }
        if (!std::isfinite(a.thickness)) {
            continue;
        }
        const double edge = edge_thickness_at(system, i);
        if (!std::isfinite(edge)) {
            min_edge = -std::numeric_limits<double>::infinity();
            continue;
        }
        min_edge = std::min(min_edge, edge);
    }
    return min_edge;
}

}  // namespace optiland
