// Provide forward/reverse paraxial propagation, STOP transforms, and first-surface localization.
// Pupil geometry and chief-anchor preparation consume these shared first-order calculations.
// Maintain surface-index, refractive-index, and vertex-distance conventions in both directions.

#include "prepared_geometry_inline.hpp"
#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

std::size_t SequentialRayTracer::stop_index() const {
    if (stop_index_cached_) {
        return cached_stop_index_;
    }
    const std::size_t first = system_.first_optical_surface();
    for (std::size_t i = first; i < system_.surfaces.size(); ++i) {
        if (system_.surfaces[i].is_stop) {
            cached_stop_index_ = i;
            stop_index_cached_ = true;
            return i;
        }
    }
    cached_stop_index_ = first < system_.surfaces.size() ? first : 0;
    stop_index_cached_ = true;
    return cached_stop_index_;
}

std::size_t SequentialRayTracer::stop_surface_index() const {
    return stop_index();
}

double SequentialRayTracer::surface_vertex_z(std::size_t surface_index) const {
    if (!vertex_z_cache_valid_) {
        vertex_z_cache_.assign(system_.surfaces.size() + 1, 0.0);
        double z = 0.0;
        const std::size_t first = system_.first_optical_surface();
        for (std::size_t i = 0; i < system_.surfaces.size(); ++i) {
            vertex_z_cache_[i] = i < first ? 0.0 : z;
            if (i >= first && std::isfinite(system_.surfaces[i].thickness)) {
                z += system_.surfaces[i].thickness;
            }
        }
        vertex_z_cache_[system_.surfaces.size()] = z;
        vertex_z_cache_valid_ = true;
    }
    if (surface_index < vertex_z_cache_.size()) {
        return vertex_z_cache_[surface_index];
    }
    double z = vertex_z_cache_.empty() ? 0.0 : vertex_z_cache_.back();
    const std::size_t first = system_.first_optical_surface();
    for (std::size_t i = std::max(first, vertex_z_cache_.size());
         i < surface_index && i < system_.surfaces.size();
         ++i) {
        if (std::isfinite(system_.surfaces[i].thickness)) {
            z += system_.surfaces[i].thickness;
        }
    }
    return z;
}

double material_before_surface(const LensSystem& system, std::size_t surface_index, double wavelength_um) {
    const std::size_t first = system.first_optical_surface();
    if (surface_index <= first || surface_index == 0) {
        return 1.0;
    }
    return system.surfaces[surface_index - 1].material_after.refractive_index(wavelength_um);
}

double surface_vertex_z_for_system(const LensSystem& system, std::size_t surface_index) {
    const std::size_t first = system.first_optical_surface();
    double z = 0.0;
    for (std::size_t i = first; i < surface_index && i < system.surfaces.size(); ++i) {
        if (std::isfinite(system.surfaces[i].thickness)) {
            z += system.surfaces[i].thickness;
        }
    }
    return z;
}

bool surface_aperture_allows(const Surface& surface, double x, double y) {
    double clip_radius = surface.aperture_radius;
    if (clip_radius <= 0.0 && surface.clear_aperture_fixed) {
        clip_radius = surface.semi_aperture;
    }
    return clip_radius <= 0.0 ||
           !aperture_clip_exceeded(std::hypot(x, y), clip_radius);
}

bool localize_first_surface_seed_ray(
    const LensSystem& system,
    double wavelength_um,
    double seed_x,
    double seed_y,
    Ray* ray) {
    if (!ray || !ray->valid || std::isfinite(system.object_distance())) {
        return false;
    }
    const std::size_t first = system.first_ray_aiming_surface(wavelength_um);
    if (first != system.first_optical_surface() || first >= system.surfaces.size()) {
        return false;
    }
    const Surface& first_surface = system.surfaces[first];
    const double sag = surface_sag(first_surface, seed_x, seed_y);
    if (!std::isfinite(seed_x) || !std::isfinite(seed_y) || !std::isfinite(sag)) {
        return false;
    }
    const double first_surface_scale = std::max({
        1.0,
        std::abs(first_surface.aperture_radius),
        std::abs(first_surface.semi_aperture)});
    const double backstep = 1.0e-4 * first_surface_scale;
    const Vec3 target{
        seed_x,
        seed_y,
        surface_vertex_z_for_system(system, first) + sag};
    ray->p = target - ray->d * backstep;
    return std::isfinite(ray->p.x) &&
           std::isfinite(ray->p.y) &&
           std::isfinite(ray->p.z);
}

bool reverse_refract_at_surface(
    const LensSystem& system,
    std::size_t surface_index,
    double wavelength_um,
    Ray* ray) {
    if (!ray || surface_index >= system.surfaces.size()) {
        return false;
    }
    const Surface& surface = system.surfaces[surface_index];
    if (surface.kind == SurfaceKind::Object || surface.kind == SurfaceKind::Image) {
        return true;
    }
    const double vertex_z = surface_vertex_z_for_system(system, surface_index);
    const Vec3 local_point{ray->p.x, ray->p.y, ray->p.z - vertex_z};
    const Vec3 normal = surface_normal(surface, local_point);
    if (surface.reflective) {
        ray->d = reflect(ray->d, normal);
        return true;
    }
    const double n_after = surface.material_after.refractive_index(wavelength_um);
    const double n_before = material_before_surface(system, surface_index, wavelength_um);
    Vec3 out;
    if (!refract(ray->d, normal, n_after, n_before, &out)) {
        return false;
    }
    ray->d = out;
    return true;
}

bool reverse_trace_stop_center_to_first(
    const LensSystem& system,
    double wavelength_um,
    double meridional_x,
    double meridional_y,
    double angle_deg,
    Vec3* first_surface_point,
    Vec3* object_space_forward_direction) {
    if (!first_surface_point || !object_space_forward_direction ||
        system.surfaces.empty()) {
        return false;
    }
    const std::size_t first = system.first_ray_aiming_surface(wavelength_um);
    if (first >= system.surfaces.size()) {
        return false;
    }
    std::size_t stop = first;
    for (std::size_t i = first; i < system.surfaces.size(); ++i) {
        if (system.surfaces[i].is_stop) {
            stop = i;
            break;
        }
    }
    if (stop < first) {
        return false;
    }

    const double axis_norm = std::hypot(meridional_x, meridional_y);
    const double ux = axis_norm > 1.0e-12 ? meridional_x / axis_norm : 0.0;
    const double uy = axis_norm > 1.0e-12 ? meridional_y / axis_norm : 1.0;
    const double theta = deg_to_rad(std::clamp(angle_deg, 0.0, 89.999));
    const double st = std::sin(theta);
    const double ct = std::cos(theta);
    const double stop_z = surface_vertex_z_for_system(system, stop) +
                          surface_sag(system.surfaces[stop], 0.0, 0.0);
    if (!std::isfinite(stop_z)) {
        return false;
    }
    Ray ray;
    ray.p = {0.0, 0.0, stop_z};
    ray.d = normalize({-st * ux, -st * uy, -std::max(1.0e-9, ct)});
    ray.wavelength_um = wavelength_um;
    ray.valid = true;
    ray.intensity = 1.0;

    if (stop == first) {
        if (!reverse_refract_at_surface(system, first, wavelength_um, &ray)) {
            return false;
        }
        *first_surface_point = {0.0, 0.0, surface_sag(system.surfaces[first], 0.0, 0.0)};
        *object_space_forward_direction = normalize(ray.d * -1.0);
        return std::isfinite(object_space_forward_direction->z);
    }

    for (std::size_t idx = stop; idx > first; --idx) {
        if (!reverse_refract_at_surface(system, idx, wavelength_um, &ray)) {
            return false;
        }
        const std::size_t prev = idx - 1;
        const Surface& surface = system.surfaces[prev];
        const double vertex_z = surface_vertex_z_for_system(system, prev);
        Ray local_ray = ray;
        local_ray.p.z -= vertex_z;
        double t = 0.0;
        if (!intersect_surface(surface, local_ray, &t)) {
            return false;
        }
        ray.p = ray.p + ray.d * std::max(0.0, t);
        if (!system.defer_secondary_aperture_clipping(ray.wavelength_um) &&
            !surface_aperture_allows(surface, ray.p.x, ray.p.y)) {
            return false;
        }
    }

    const double first_z = surface_vertex_z_for_system(system, first);
    *first_surface_point = {ray.p.x, ray.p.y, ray.p.z - first_z};
    if (!reverse_refract_at_surface(system, first, wavelength_um, &ray)) {
        return false;
    }
    *object_space_forward_direction = normalize(ray.d * -1.0);
    return std::isfinite(first_surface_point->x) &&
           std::isfinite(first_surface_point->y) &&
           std::isfinite(object_space_forward_direction->x) &&
           std::isfinite(object_space_forward_direction->y) &&
           std::isfinite(object_space_forward_direction->z);
}

void paraxial_refract_forward(
    const LensSystem& system,
    std::size_t surface_index,
    double wavelength_um,
    double y,
    double* u) {
    const Surface& surface = system.surfaces[surface_index];
    if (!u || surface.kind == SurfaceKind::Object || surface.kind == SurfaceKind::Image) {
        return;
    }
    const double n1 = material_before_surface(system, surface_index, wavelength_um);
    const double n2 = surface.material_after.refractive_index(wavelength_um);
    if (std::abs(n2) <= kEps) {
        return;
    }
    const double power =
        (std::isfinite(surface.radius) && std::abs(surface.radius) >= kEps)
            ? (n2 - n1) / surface.radius
            : 0.0;
    *u = (n1 * (*u) - y * power) / n2;
}

void paraxial_refract_reverse(
    const LensSystem& system,
    std::size_t surface_index,
    double wavelength_um,
    double y,
    double* u) {
    const Surface& surface = system.surfaces[surface_index];
    if (!u || surface.kind == SurfaceKind::Object || surface.kind == SurfaceKind::Image) {
        return;
    }
    const double n1 = surface.material_after.refractive_index(wavelength_um);
    const double n2 = material_before_surface(system, surface_index, wavelength_um);
    if (std::abs(n2) <= kEps) {
        return;
    }
    const double power =
        (std::isfinite(surface.radius) && std::abs(surface.radius) >= kEps)
            ? (n2 - n1) / (-surface.radius)
            : 0.0;
    *u = (n1 * (*u) - y * power) / n2;
}

bool paraxial_trace_to_surface(
    const LensSystem& system,
    double y0,
    double u0,
    double z0,
    double wavelength_um,
    std::size_t surface_index,
    double* y_at_surface,
    double* u_after_surface ,
    std::size_t start_surface ) {
    if (surface_index >= system.surfaces.size()) {
        return false;
    }
    const std::size_t first = start_surface == std::numeric_limits<std::size_t>::max()
                                  ? system.first_optical_surface()
                                  : start_surface;
    if (first >= system.surfaces.size() || surface_index < first) {
        return false;
    }
    double y = y0;
    double u = u0;
    double z = z0;
    for (std::size_t i = first; i <= surface_index && i < system.surfaces.size(); ++i) {
        const double vertex_z = surface_vertex_z_for_system(system, i);
        y += (vertex_z - z) * u;
        z = vertex_z;
        paraxial_refract_forward(system, i, wavelength_um, y, &u);
    }
    if (y_at_surface) {
        *y_at_surface = y;
    }
    if (u_after_surface) {
        *u_after_surface = u;
    }
    return std::isfinite(y) && std::isfinite(u);
}

bool first_surface_to_stop_abcd(
    const LensSystem& system,
    double wavelength_um,
    std::size_t stop_index,
    double* a,
    double* b) {
    if (!a || !b || stop_index >= system.surfaces.size()) {
        return false;
    }
    const std::size_t first = system.first_ray_aiming_surface(wavelength_um);
    if (first >= system.surfaces.size() || stop_index < first) {
        return false;
    }
    const double first_z = surface_vertex_z_for_system(system, first);
    double y_from_height = 0.0;
    double y_from_slope = 0.0;
    if (!paraxial_trace_to_surface(
            system,
            1.0,
            0.0,
            first_z,
            wavelength_um,
            stop_index,
            &y_from_height,
            nullptr,
            first) ||
        !paraxial_trace_to_surface(
            system,
            0.0,
            1.0,
            first_z,
            wavelength_um,
            stop_index,
            &y_from_slope,
            nullptr,
            first)) {
        return false;
    }
    *a = y_from_height;
    *b = y_from_slope;
    return std::isfinite(*a) && std::isfinite(*b);
}

double paraxial_effective_focal_length(const LensSystem& system, double wavelength_um) {
    double y = 1.0;
    double u = 0.0;
    double z_current = -1.0;
    double vertex_z = 0.0;
    Material material_before = Material::air();

    for (std::size_t i = system.first_optical_surface(); i < system.surfaces.size(); ++i) {
        const auto& surface = system.surfaces[i];
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
            }
        }
        material_before = surface.material_after;
        if (std::isfinite(surface.thickness)) {
            vertex_z += surface.thickness;
        }
    }
    if (std::abs(u) <= kEps) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return -1.0 / u;
}

} // namespace optiland
