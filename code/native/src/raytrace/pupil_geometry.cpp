// Resolve entrance/exit pupils, real and paraxial STOP radii, and effective clipping targets.
// Ray generation and aiming use these quantities to map normalized pupils to physical geometry.
// Do not interchange geometric, mechanical, and STOP aperture radii or change radius fallbacks.

#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

double SequentialRayTracer::entrance_pupil_radius(double wavelength_um) const {
    double cached = 0.0;
    if (lookup_wavelength_cache(entrance_pupil_radius_cache_, wavelength_um, &cached)) {
        return cached;
    }
    double value = system_.entrance_pupil_radius();
    if (system_.aperture.type == ApertureType::EntrancePupilDiameter) {
        value = 0.5 * system_.aperture.value;
        return store_wavelength_cache(entrance_pupil_radius_cache_, wavelength_um, value);
    }
    if (system_.aperture.type == ApertureType::ObjectNumericalAperture) {
        const double object_distance = system_.object_distance();
        if (std::isfinite(object_distance) && system_.aperture.value > 0.0) {
            double n0 = 1.0;
            if (system_.has_object_surface()) {
                n0 = system_.surfaces.front().material_after.refractive_index(wavelength_um);
            }
            if (n0 > kEps) {
                const double sine_u = std::clamp(system_.aperture.value / n0, 0.0, 1.0 - 1.0e-12);
                const double u0 = std::asin(sine_u);
                const double z = entrance_pupil_location(wavelength_um) + object_distance;
                if (std::isfinite(z)) {
                    value = std::abs(z) * std::tan(u0);
                    return store_wavelength_cache(entrance_pupil_radius_cache_, wavelength_um, value);
                }
            }
        }
        return store_wavelength_cache(entrance_pupil_radius_cache_, wavelength_um, value);
    }
    if (system_.aperture.type == ApertureType::FloatByStopSize) {
        const std::size_t si = stop_index();
        if (si < system_.surfaces.size() && system_.surfaces[si].aperture_radius > 0.0) {
            value = system_.surfaces[si].aperture_radius;
            return store_wavelength_cache(entrance_pupil_radius_cache_, wavelength_um, value);
        }
        if (si < system_.surfaces.size() && system_.surfaces[si].semi_aperture > 0.0) {
            value = system_.surfaces[si].semi_aperture;
            return store_wavelength_cache(entrance_pupil_radius_cache_, wavelength_um, value);
        }
        value = 1.0;
        return store_wavelength_cache(entrance_pupil_radius_cache_, wavelength_um, value);
    }
    const double efl = paraxial_effective_focal_length(system_, wavelength_um);
    if (std::isfinite(efl) && system_.aperture.value > 0.0) {
        value = 0.5 * std::abs(efl) / system_.aperture.value;
        return store_wavelength_cache(entrance_pupil_radius_cache_, wavelength_um, value);
    }
    return store_wavelength_cache(entrance_pupil_radius_cache_, wavelength_um, value);
}

double SequentialRayTracer::entrance_pupil_location(double wavelength_um) const {
    double cached = 0.0;
    if (lookup_wavelength_cache(entrance_pupil_location_cache_, wavelength_um, &cached)) {
        return cached;
    }
    const std::size_t si = stop_index();
    const std::size_t first = system_.first_optical_surface();
    if (si <= first || si >= system_.surfaces.size()) {
        return store_wavelength_cache(entrance_pupil_location_cache_, wavelength_um, 0.0);
    }
    const double image_z = surface_vertex_z(system_.surfaces.empty() ? 0 : system_.surfaces.size() - 1);
    const double stop_z = surface_vertex_z(si);
    double y = 0.0;
    double u = 0.1;
    double z = image_z - stop_z;

    for (std::size_t idx = si; idx > first; --idx) {
        const std::size_t i = idx - 1;
        const double vertex_z = image_z - surface_vertex_z(i);
        y += (vertex_z - z) * u;
        z = vertex_z;
        paraxial_refract_reverse(system_, i, wavelength_um, y, &u);
    }
    if (std::abs(u) <= kEps || !std::isfinite(y) || !std::isfinite(u)) {
        return store_wavelength_cache(entrance_pupil_location_cache_, wavelength_um, 0.0);
    }
    return store_wavelength_cache(entrance_pupil_location_cache_, wavelength_um, y / u);
}

double SequentialRayTracer::exit_pupil_location(double wavelength_um) const {
    double cached = 0.0;
    if (lookup_wavelength_cache(exit_pupil_location_cache_, wavelength_um, &cached)) {
        return cached;
    }
    const std::size_t si = stop_index();
    if (si >= system_.surfaces.size() || si + 1 >= system_.surfaces.size()) {
        return store_wavelength_cache(exit_pupil_location_cache_, wavelength_um, 0.0);
    }

    double y = 0.0;
    double u = 0.1;
    double z = surface_vertex_z(si);
    for (std::size_t i = si + 1; i < system_.surfaces.size(); ++i) {
        const double vertex_z = surface_vertex_z(i);
        y += (vertex_z - z) * u;
        z = vertex_z;
        paraxial_refract_forward(system_, i, wavelength_um, y, &u);
    }
    if (std::abs(u) <= kEps || !std::isfinite(y) || !std::isfinite(u)) {
        return store_wavelength_cache(exit_pupil_location_cache_, wavelength_um, 0.0);
    }
    return store_wavelength_cache(exit_pupil_location_cache_, wavelength_um, -y / u);
}

double SequentialRayTracer::paraxial_stop_radius(double wavelength_um) const {
    double cached = 0.0;
    if (lookup_wavelength_cache(paraxial_stop_radius_cache_, wavelength_um, &cached)) {
        return cached;
    }
    if (system_.aperture.type == ApertureType::FloatByStopSize) {
        const std::size_t si = stop_index();
        if (si < system_.surfaces.size() && system_.surfaces[si].aperture_radius > 0.0) {
            return store_wavelength_cache(
                paraxial_stop_radius_cache_,
                wavelength_um,
                system_.surfaces[si].aperture_radius);
        }
        if (si < system_.surfaces.size() && system_.surfaces[si].semi_aperture > 0.0) {
            return store_wavelength_cache(
                paraxial_stop_radius_cache_,
                wavelength_um,
                system_.surfaces[si].semi_aperture);
        }
    }
    const std::size_t si = stop_index();
    const double epd_radius = entrance_pupil_radius(wavelength_um);
    double y0 = epd_radius;
    double u0 = 0.0;
    double z0 = -10.0;
    const double object_distance = system_.object_distance();
    if (std::isfinite(object_distance)) {
        const double epl = entrance_pupil_location(wavelength_um);
        y0 = 0.0;
        z0 = -object_distance;
        const double dz = epl - z0;
        u0 = std::abs(dz) > kEps ? epd_radius / dz : 0.0;
    }
    double y_stop = 0.0;
    if (paraxial_trace_to_surface(system_, y0, u0, z0, wavelength_um, si, &y_stop)) {
        return store_wavelength_cache(paraxial_stop_radius_cache_, wavelength_um, std::abs(y_stop));
    }
    return store_wavelength_cache(paraxial_stop_radius_cache_, wavelength_um, epd_radius);
}

double SequentialRayTracer::real_stop_radius(double wavelength_um) const {
    double cached = 0.0;
    if (lookup_wavelength_cache(real_stop_radius_cache_, wavelength_um, &cached)) {
        return cached;
    }
    const std::size_t si = stop_index();
    Ray ray;
    const double epd_radius = entrance_pupil_radius(wavelength_um);
    const double object_distance = system_.object_distance();
    if (std::isfinite(object_distance)) {
        const double epl = entrance_pupil_location(wavelength_um);
        ray.p = {0.0, 0.0, -object_distance};
        ray.d = normalize({0.0, epd_radius, epl + object_distance});
    } else {
        ray.p = {0.0, epd_radius, -100.0};
        ray.d = {0.0, 0.0, 1.0};
    }
    ray.wavelength_um = wavelength_um;
    ray.intensity = 1.0;
    ray.valid = true;
    TracePoint point;
    // This ray is used to size the physical aperture stop, so the current
    // stop aperture must not clip the very ray that determines the new size.
    if (trace_to_surface(ray, si, &point, true) && point.valid) {
        return store_wavelength_cache(
            real_stop_radius_cache_,
            wavelength_um,
            std::max(kEps, std::sqrt(point.x * point.x + point.y * point.y)));
    }
    return store_wavelength_cache(real_stop_radius_cache_, wavelength_um, paraxial_stop_radius(wavelength_um));
}

double SequentialRayTracer::aperture_stop_radius(double wavelength_um) const {
    const std::size_t si = stop_index();
    if (system_.aperture.type == ApertureType::FloatByStopSize) {
        if (si < system_.surfaces.size() && system_.surfaces[si].aperture_radius > 0.0) {
            return system_.surfaces[si].aperture_radius;
        }
        if (si < system_.surfaces.size() && system_.surfaces[si].semi_aperture > 0.0) {
            return system_.surfaces[si].semi_aperture;
        }
    }
    // Direct and paraxial launch modes do not aim at a prescribed physical
    // stop radius.  Their stop aperture is the actual-ray envelope previously
    // stored on the stop surface by the aperture update.  The paraxial/real
    // radius models below are targets for genuine ray-aiming solvers only.
    if (system_.ray_aiming.mode == RayAimingMode::Direct ||
        system_.ray_aiming.mode == RayAimingMode::Paraxial) {
        if (si < system_.surfaces.size() && system_.surfaces[si].aperture_radius > 0.0) {
            return system_.surfaces[si].aperture_radius;
        }
        if (si < system_.surfaces.size() && system_.surfaces[si].semi_aperture > 0.0) {
            return system_.surfaces[si].semi_aperture;
        }
        return 0.0;
    }
    const double reference_wavelength = system_.primary_wavelength();
    const double stop_wavelength =
        std::isfinite(reference_wavelength) && reference_wavelength > 0.0
            ? reference_wavelength
            : wavelength_um;
    if (system_.ray_aiming.stop_radius_source == RayAimingStopRadiusSource::Real) {
        return real_stop_radius(stop_wavelength);
    }
    return paraxial_stop_radius(stop_wavelength);
}

double SequentialRayTracer::effective_aperture_clip_radius(
    std::size_t surface_index,
    double wavelength_um) const {
    if (surface_index >= system_.surfaces.size()) {
        return 0.0;
    }
    const Surface& surface = system_.surfaces[surface_index];
    if (surface.is_stop &&
        !surface.clear_aperture_fixed &&
        system_.aperture.type != ApertureType::FloatByStopSize) {
        const double radius = aperture_stop_radius(wavelength_um);
        if (std::isfinite(radius) && radius > 0.0) {
            return radius;
        }
    }
    double clip_radius = surface.aperture_radius;
    if (clip_radius <= 0.0 && surface.clear_aperture_fixed) {
        clip_radius = surface.semi_aperture;
    }
    return clip_radius;
}

double SequentialRayTracer::stop_target_radius(double wavelength_um, RayAimingMode mode) const {
    const std::size_t si = stop_index();
    if (si < system_.surfaces.size()) {
        const Surface& stop_surface = system_.surfaces[si];
        if (stop_surface.clear_aperture_fixed) {
            if (stop_surface.aperture_radius > 0.0) {
                return stop_surface.aperture_radius;
            }
            if (stop_surface.semi_aperture > 0.0) {
                return stop_surface.semi_aperture;
            }
        }
        if (system_.aperture.type == ApertureType::FloatByStopSize) {
            if (stop_surface.aperture_radius > 0.0) {
                return stop_surface.aperture_radius;
            }
            if (stop_surface.semi_aperture > 0.0) {
                return stop_surface.semi_aperture;
            }
        }
    }
    if (mode == RayAimingMode::Direct || mode == RayAimingMode::Paraxial) {
        return aperture_stop_radius(wavelength_um);
    }
    const double reference_wavelength = system_.primary_wavelength();
    const double stop_wavelength =
        std::isfinite(reference_wavelength) && reference_wavelength > 0.0
            ? reference_wavelength
            : wavelength_um;
    if (mode != RayAimingMode::Paraxial &&
        system_.ray_aiming.stop_radius_source == RayAimingStopRadiusSource::Real) {
        return real_stop_radius(stop_wavelength);
    }
    return paraxial_stop_radius(stop_wavelength);
}

} // namespace optiland
