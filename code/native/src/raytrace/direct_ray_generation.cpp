// Construct direct, paraxial, and surface-point rays before selecting the requested aiming mode.
// SequentialRayTracer callers use these entry points for finite and infinite object geometries.
// Preserve launch coordinates and mode routing; physical aperture rejection belongs to tracing.

#include "aiming_geometry_inline.hpp"
#include "prepared_geometry_inline.hpp"
#include "trace_diagnostics_inline.hpp"
#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

Ray SequentialRayTracer::generate_direct_ray(
    const Field& field,
    const PupilPoint& pupil,
    double wavelength_um) const {
    const double aperture_radius = entrance_pupil_radius(wavelength_um);
    const double px = field.fvcx + pupil.px * (1.0 - field.fvdx);
    const double py = field.fvcy + pupil.py * (1.0 - field.fvdy);
    const Vec3 direction = field_direction_cosines(system_, field);
    const double object_distance = system_.object_distance();
    const bool infinite_object = !std::isfinite(object_distance);
    const Vec3 target{px * aperture_radius, py * aperture_radius, 0.0};

    Ray ray;
    ray.d = direction;
    ray.nominal_weight = std::max(0.0, pupil.weight);
    if (infinite_object) {
        const double start_distance = infinite_object_start_distance(system_, aperture_radius, target.z);
        ray.p = target - direction * start_distance;
    } else {
        ray.valid = set_finite_object_ray(&ray, target, direction, object_distance);
        if (!ray.valid) {
            mark_ray_failure(ray, RayFailureCode::GeometryInvalid, -1, 1.0);
        }
    }
    ray.wavelength_um = wavelength_um;
    ray.intensity = (px * px + py * py <= 1.0 + 1.0e-10)
                        ? ray.nominal_weight
                        : 0.0;
    if (ray.intensity <= 0.0) {
        const double radial = std::sqrt(px * px + py * py);
        mark_ray_failure(ray, RayFailureCode::ApertureClip, -1, std::max(0.0, radial - 1.0));
    }
    return ray;
}

Ray SequentialRayTracer::generate_paraxial_ray(
    const Field& field,
    const PupilPoint& pupil,
    double wavelength_um) const {
    const double object_distance = system_.object_distance();
    const bool infinite_object = !std::isfinite(object_distance);
    const Vec3 direction = field_direction_cosines(system_, field);
    const double pxf = field.fvcx + pupil.px * (1.0 - field.fvdx);
    const double pyf = field.fvcy + pupil.py * (1.0 - field.fvdy);
    const double ep_radius = entrance_pupil_radius(wavelength_um);

    // With a physical stop at the first optical surface, nothing precedes the
    // stop to image it.  The entrance pupil is therefore S1 itself.  Aim at
    // the requested coordinate on its actual sag surface instead of at a
    // generic vertex-plane entrance pupil; otherwise an off-axis bundle is
    // translated at S1 and can be spuriously clipped.
    const std::size_t first = system_.first_optical_surface();
    if (first < system_.surfaces.size() && system_.surfaces[first].is_stop) {
        double target_radius = ep_radius;
        const Surface& stop_surface = system_.surfaces[first];
        if (stop_surface.clear_aperture_fixed ||
            system_.aperture.type == ApertureType::FloatByStopSize) {
            if (stop_surface.aperture_radius > kEps) {
                target_radius = stop_surface.aperture_radius;
            } else if (stop_surface.semi_aperture > kEps) {
                target_radius = stop_surface.semi_aperture;
            }
        }
        Ray ray = generate_ray_through_surface_point(
            field,
            wavelength_um,
            first,
            pxf * target_radius,
            pyf * target_radius,
            pupil.weight);
        if (pxf * pxf + pyf * pyf > 1.0 + 1.0e-10) {
            const double radial = std::sqrt(pxf * pxf + pyf * pyf);
            mark_ray_failure(
                ray,
                RayFailureCode::ApertureClip,
                static_cast<int>(first),
                std::max(0.0, radial - 1.0));
        }
        return ray;
    }

    // Zemax RAIM=0 defines every spectral ray from the same paraxial entrance
    // pupil: the one calculated at the primary wavelength.  Dispersion acts
    // only during the subsequent real surface trace.
    const double primary_wavelength = system_.primary_wavelength();
    const double pupil_wavelength =
        std::isfinite(primary_wavelength) && primary_wavelength > 0.0
            ? primary_wavelength
            : wavelength_um;
    const double epl = entrance_pupil_location(pupil_wavelength);
    const Vec3 target{pxf * ep_radius, pyf * ep_radius, epl};

    Ray ray;
    ray.d = direction;
    ray.nominal_weight = std::max(0.0, pupil.weight);
    if (infinite_object) {
        const double start_distance = infinite_object_start_distance(system_, ep_radius, target.z);
        ray.p = target - direction * start_distance;
    } else {
        ray.valid = set_finite_object_ray(&ray, target, direction, object_distance);
        if (!ray.valid) {
            mark_ray_failure(ray, RayFailureCode::GeometryInvalid, -1, 1.0);
        }
    }
    ray.wavelength_um = wavelength_um;
    ray.intensity = (pxf * pxf + pyf * pyf <= 1.0 + 1.0e-10)
                        ? ray.nominal_weight
                        : 0.0;
    if (ray.intensity <= 0.0) {
        const double radial = std::sqrt(pxf * pxf + pyf * pyf);
        mark_ray_failure(ray, RayFailureCode::ApertureClip, -1, std::max(0.0, radial - 1.0));
    }
    return ray;
}

Ray SequentialRayTracer::generate_ray(
    const Field& field,
    const PupilPoint& pupil,
    double wavelength_um) const {
    return generate_ray_with_mode(field, pupil, wavelength_um, system_.ray_aiming.mode);
}

Ray SequentialRayTracer::generate_ray_with_mode(
    const Field& field,
    const PupilPoint& pupil,
    double wavelength_um,
    RayAimingMode mode) const {
    if (mode == RayAimingMode::Iterative) {
        return aim_iterative(field, pupil, wavelength_um, false);
    }
    if (mode == RayAimingMode::Robust) {
        AimingSession session(system_);
        return session.generate_robust(field, Wavelength{wavelength_um, 1.0, true}, pupil);
    }
    if (ray_aiming_uses_session(mode)) {
        AimingSession session(system_);
        return session.generate(0, 0, 0, field, Wavelength{wavelength_um, 1.0, true}, pupil);
    }
    if (mode == RayAimingMode::Paraxial) {
        return generate_paraxial_ray(field, pupil, wavelength_um);
    }
    return generate_direct_ray(field, pupil, wavelength_um);
}

bool SequentialRayTracer::first_surface_point(const Ray& ray, Vec3* point) const {
    if (!point) {
        return false;
    }
    const double wavelength =
        (std::isfinite(ray.wavelength_um) && ray.wavelength_um > 0.0)
            ? ray.wavelength_um
            : system_.primary_wavelength();
    const std::size_t first = system_.first_ray_aiming_surface(wavelength);
    if (first >= system_.surfaces.size()) {
        return false;
    }
    TracePoint hit;
    if (!trace_to_surface(ray, first, &hit) || !hit.valid) {
        return false;
    }
    *point = {hit.x, hit.y, hit.z - surface_vertex_z(first)};
    return std::isfinite(point->x) && std::isfinite(point->y) && std::isfinite(point->z);
}

Ray SequentialRayTracer::generate_ray_through_first_surface_point(
    const Field& field,
    double wavelength_um,
    double x,
    double y,
    double nominal_weight) const {
    const std::size_t first = system_.first_ray_aiming_surface(wavelength_um);
    Ray ray = generate_ray_through_surface_point(
        field,
        wavelength_um,
        first,
        x,
        y,
        nominal_weight);
    // An explicit first-surface seed identifies the intended analytic branch.
    // Start infinitesimally before that point so the ordinary sequential
    // intersection sees it as the first forward root.  This changes neither
    // the ray direction nor any downstream surface policy, and avoids adding
    // per-ray branch metadata to the generic hot path.
    (void)localize_first_surface_seed_ray(system_, wavelength_um, x, y, &ray);
    return ray;
}

Ray SequentialRayTracer::generate_ray_through_surface_point(
    const Field& field,
    double wavelength_um,
    std::size_t first,
    double x,
    double y,
    double nominal_weight) const {
    Ray ray;
    ray.wavelength_um = wavelength_um;
    ray.nominal_weight = std::max(0.0, nominal_weight);
    ray.intensity = ray.nominal_weight;
    if (first >= system_.surfaces.size()) {
        mark_ray_failure(ray, RayFailureCode::GeometryInvalid, -1, 1.0);
        return ray;
    }

    const Surface& first_surface = system_.surfaces[first];
    const double sag = surface_sag(first_surface, x, y);
    if (!std::isfinite(sag)) {
        mark_ray_failure(ray, RayFailureCode::SagInvalid, static_cast<int>(first), 1.0);
        return ray;
    }
    double clip_radius = first_surface.aperture_radius;
    if (clip_radius <= 0.0 && first_surface.clear_aperture_fixed) {
        clip_radius = first_surface.semi_aperture;
    }
    // Match the ordinary CPU tracer and the CUDA first-surface seed path:
    // calibrated interpolation is allowed to use the shared 1% engineering
    // aperture margin.  A stricter check here made CPU aiming reject seeds
    // that both backends' subsequent aperture tests accept.
    if (!system_.defer_secondary_aperture_clipping(wavelength_um) &&
        clip_radius > 0.0 &&
        aperture_clip_exceeded(std::hypot(x, y), clip_radius)) {
        mark_ray_failure(ray, RayFailureCode::ApertureClip, static_cast<int>(first), 1.0);
        return ray;
    }

    const Vec3 direction = field_direction_cosines(system_, field);
    const Vec3 target{x, y, surface_vertex_z(first) + sag};
    ray.d = direction;
    const double object_distance = system_.object_distance();
    if (std::isfinite(object_distance)) {
        ray.valid = set_finite_object_ray(&ray, target, direction, object_distance);
        if (!ray.valid) {
            mark_ray_failure(ray, RayFailureCode::GeometryInvalid, -1, 1.0);
        }
    } else {
        const double aperture_radius = std::max(entrance_pupil_radius(wavelength_um), std::hypot(x, y));
        const double start_distance = infinite_object_start_distance(system_, aperture_radius, target.z);
        ray.p = target - direction * start_distance;
        ray.valid = std::isfinite(ray.p.x) && std::isfinite(ray.p.y) && std::isfinite(ray.p.z);
        if (!ray.valid) {
            mark_ray_failure(ray, RayFailureCode::GeometryInvalid, -1, 1.0);
        }
    }
    return ray;
}

} // namespace optiland
