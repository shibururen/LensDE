// Propagate individual physical rays and optional histories through the sequential surface chain.
// Prepared tracing and aiming probes share the same intersection/refraction conventions.
// Surface order, optical-path accumulation, clipping, and failure progress are scientific outputs.

#include "prepared_geometry_inline.hpp"
#include "spectral_aperture_inline.hpp"
#include "trace_diagnostics_inline.hpp"
#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

bool SequentialRayTracer::trace_ray(
    Ray& ray,
    std::vector<TracePoint>* history,
    bool ignore_automatic_stop_aperture) const {
    if (!history && !ignore_automatic_stop_aperture && prepared_trace_supported()) {
        return trace_ray_prepared_no_history(ray);
    }

    double vertex_z = 0.0;
    Material material_before = Material::air();
    const std::size_t first = system_.first_optical_surface();
    const double object_space_dz = ray.d.z;
    ray.stop_x = std::numeric_limits<double>::quiet_NaN();
    ray.stop_y = std::numeric_limits<double>::quiet_NaN();
    ray.stop_reached = false;
    ray.min_backward_air_gap = 0.0;
    ray.min_backward_glass_gap = 0.0;

    for (std::size_t i = 0; i < system_.surfaces.size(); ++i) {
        const Surface& surface = system_.surfaces[i];
        if (i < first || surface.kind == SurfaceKind::Object) {
            if (history) {
                TracePoint point;
                point.z = ray.p.z;
                point.x = ray.p.x;
                point.y = ray.p.y;
                point.l = ray.d.x;
                point.m = ray.d.y;
                point.n = ray.d.z;
                point.intensity = ray.intensity;
                point.valid = ray.valid;
                history->push_back(point);
            }
            continue;
        }
        Ray local_ray = ray;
        local_ray.p.z -= vertex_z;

        const bool skip_automatic_stop =
            ignore_automatic_stop_aperture && surface.is_stop &&
            !surface.clear_aperture_fixed &&
            system_.aperture.type != ApertureType::FloatByStopSize;

        double t = 0.0;
        if (!ray.valid) {
            ray.intensity = 0.0;
        } else if (!intersect_surface(surface, local_ray, &t,
                     skip_automatic_stop ||
                     system_.defer_secondary_aperture_clipping(ray.wavelength_um))) {
            mark_ray_failure(ray, RayFailureCode::SurfaceMiss, static_cast<int>(i), 1.0);
        } else {
            const double step_z = t * ray.d.z;
            const double z_tol = 1.0e-10 * std::max(1.0, std::abs(surface.thickness));
            const bool backward_by_parameter = t < -1.0e-10;
            const bool backward_by_z = step_z < -z_tol;
            const bool allowed_object_space_retro =
                i == first &&
                object_space_dz < -1.0e-12 &&
                t >= -1.0e-10 &&
                backward_by_z;
            if ((backward_by_parameter || backward_by_z) && !allowed_object_space_retro) {
                const double reference = std::max(1.0e-9, std::abs(surface.thickness));
                const double severity = std::max(
                    std::abs(t),
                    std::abs(step_z) / reference);
                mark_backward_propagation_warning(ray, static_cast<int>(i), severity);
                record_backward_surface_overlap(
                    ray, system_, i, vertex_z, ray.p.x, ray.p.y, ray.p.z);
            }
            const double n_pre = material_before.refractive_index(ray.wavelength_um);
            ray.p = ray.p + ray.d * t;
            ray.opd += std::abs(t * n_pre);

            if (surface.is_stop && !ray.stop_reached &&
                std::isfinite(ray.p.x) && std::isfinite(ray.p.y)) {
                ray.stop_x = ray.p.x;
                ray.stop_y = ray.p.y;
                ray.stop_reached = true;
            }

            const double clip_radius = skip_automatic_stop
                                           ? 0.0
                                           : effective_aperture_clip_radius(i, ray.wavelength_um);
            if (system_.defer_secondary_aperture_clipping(ray.wavelength_um)) {
                record_secondary_aperture_hit(ray, surface, static_cast<int>(i), ray.p.x, ray.p.y);
            } else if (clip_radius > 0.0) {
                const double radial = std::sqrt(ray.p.x * ray.p.x + ray.p.y * ray.p.y);
                if (aperture_clip_exceeded(radial, clip_radius)) {
                    mark_ray_failure(
                        ray,
                        RayFailureCode::ApertureClip,
                        static_cast<int>(i),
                        (radial - clip_radius) / std::max(clip_radius, kEps));
                }
            }

            if (ray.valid && surface.kind != SurfaceKind::Image) {
                const Vec3 local_point{ray.p.x, ray.p.y, ray.p.z - vertex_z};
                const Vec3 normal = surface_normal(surface, local_point);
                if (surface.reflective) {
                    ray.d = reflect(ray.d, normal);
                } else {
                    const double n_post = surface.material_after.refractive_index(ray.wavelength_um);
                    Vec3 next_dir;
                    if (!refract(ray.d, normal, n_pre, n_post, &next_dir)) {
                        mark_ray_failure(ray, RayFailureCode::TotalInternalReflection, static_cast<int>(i), 1.0);
                    } else {
                        ray.d = next_dir;
                    }
                }
            }
        }

        if (history) {
            TracePoint point;
            point.z = ray.p.z;
            point.x = ray.p.x;
            point.y = ray.p.y;
            point.l = ray.d.x;
            point.m = ray.d.y;
            point.n = ray.d.z;
            point.intensity = ray.intensity;
            point.valid = ray.valid;
            history->push_back(point);
        }

        material_before = surface.material_after;
        if (std::isfinite(surface.thickness)) {
            vertex_z += surface.thickness;
        }
    }
    return ray.valid;
}

bool SequentialRayTracer::trace_unclipped_to_surface_history(
    Ray ray,
    std::size_t surface_index,
    std::vector<TracePoint>* history) const {
    if (!history || surface_index >= system_.surfaces.size()) {
        return false;
    }
    const std::size_t max_surface = surface_index + 1;
    history->assign(max_surface, TracePoint{});
    const std::size_t first = system_.first_optical_surface();
    auto record = [&](std::size_t index) {
        TracePoint& point = (*history)[index];
        point.z = ray.p.z;
        point.x = ray.p.x;
        point.y = ray.p.y;
        point.l = ray.d.x;
        point.m = ray.d.y;
        point.n = ray.d.z;
        point.intensity = ray.intensity;
        point.valid = ray.valid;
    };
    for (std::size_t i = 0; i < std::min(first, max_surface); ++i) {
        record(i);
    }
    if (surface_index < first ||
        system_.surfaces[surface_index].kind == SurfaceKind::Object) {
        return (*history)[surface_index].valid;
    }

    if (prepared_trace_supported()) {
        const PreparedTraceWaveCache& wave_cache =
            prepared_trace_cache(ray.wavelength_um);
        for (std::size_t i = first; i < max_surface; ++i) {
            const unsigned char geometry = wave_cache.geometry[i];
            const double vertex_z = wave_cache.vertex_z[i];
            if (!ray.valid) {
                ray.intensity = 0.0;
            } else {
                double t = 0.0;
                const bool intersection_ok = prepared_geometry_intersect(
                    geometry,
                    wave_cache.radius[i],
                    wave_cache.inv_radius[i],
                    ray.p.x,
                    ray.p.y,
                    ray.p.z - vertex_z,
                    ray.d.x,
                    ray.d.y,
                    ray.d.z,
                    &t);
                if (!intersection_ok) {
                    ray.valid = false;
                    ray.intensity = 0.0;
                } else {
                    const double n_pre = wave_cache.n_before[i];
                    ray.p = ray.p + ray.d * t;
                    ray.opd += std::abs(t * n_pre);
                    if (ray.valid && wave_cache.refract_surface[i]) {
                        const Vec3 normal = prepared_geometry_normal(
                            geometry,
                            wave_cache.radius[i],
                            wave_cache.inv_radius[i],
                            ray.p.x,
                            ray.p.y,
                            ray.p.z - vertex_z);
                        Vec3 next_dir;
                        if (!refract_assume_unit(
                                ray.d,
                                normal,
                                n_pre,
                                wave_cache.n_after[i],
                                &next_dir)) {
                            ray.valid = false;
                            ray.intensity = 0.0;
                        } else {
                            ray.d = next_dir;
                        }
                    }
                }
            }
            record(i);
        }
        return (*history)[surface_index].valid;
    }

    double vertex_z = 0.0;
    Material material_before = Material::air();
    for (std::size_t i = first; i < max_surface; ++i) {
        const Surface& surface = system_.surfaces[i];
        Ray local_ray = ray;
        local_ray.p.z -= vertex_z;
        double t = 0.0;
        // Match trace_to_surface(..., true, true): prefer the physical-domain
        // root, then retry the unbounded mathematical surface when the domain
        // itself is the only reason for the miss.  No aperture clip is applied.
        bool intersection_ok = false;
        if (ray.valid) {
            intersection_ok = intersect_surface(surface, local_ray, &t, false);
            if (!intersection_ok) {
                intersection_ok = intersect_surface(surface, local_ray, &t, true);
            }
        }
        if (!intersection_ok) {
            ray.valid = false;
            ray.intensity = 0.0;
        } else {
            const double n_pre =
                material_before.refractive_index(ray.wavelength_um);
            ray.p = ray.p + ray.d * t;
            ray.opd += std::abs(t * n_pre);
            if (ray.valid && surface.kind != SurfaceKind::Image) {
                const Vec3 local_point{
                    ray.p.x, ray.p.y, ray.p.z - vertex_z};
                const Vec3 normal = surface_normal(surface, local_point);
                if (surface.reflective) {
                    ray.d = reflect(ray.d, normal);
                } else {
                    const double n_post =
                        surface.material_after.refractive_index(
                            ray.wavelength_um);
                    Vec3 next_dir;
                    if (!refract(
                            ray.d, normal, n_pre, n_post, &next_dir)) {
                        ray.valid = false;
                        ray.intensity = 0.0;
                    } else {
                        ray.d = next_dir;
                    }
                }
            }
        }
        record(i);
        material_before = surface.material_after;
        if (std::isfinite(surface.thickness)) {
            vertex_z += surface.thickness;
        }
    }
    return (*history)[surface_index].valid;
}

bool SequentialRayTracer::trace_to_surface(
    Ray ray,
    std::size_t surface_index,
    TracePoint* point,
    bool ignore_target_aperture,
    bool ignore_intermediate_apertures) const {
    if (prepared_trace_supported()) {
        return trace_to_surface_prepared(
            ray,
            surface_index,
            point,
            ignore_target_aperture,
            ignore_intermediate_apertures);
    }

    std::vector<TracePoint> history;
    const std::size_t max_surface = std::min(surface_index + 1, system_.surfaces.size());
    double vertex_z = 0.0;
    Material material_before = Material::air();
    const std::size_t first = system_.first_optical_surface();

    if (surface_index < first || (surface_index < system_.surfaces.size() &&
                                  system_.surfaces[surface_index].kind == SurfaceKind::Object)) {
        if (point) {
            point->z = ray.p.z;
            point->x = ray.p.x;
            point->y = ray.p.y;
            point->l = ray.d.x;
            point->m = ray.d.y;
            point->n = ray.d.z;
            point->intensity = ray.intensity;
            point->valid = ray.valid;
        }
        return point ? point->valid : ray.valid;
    }

    for (std::size_t i = first; i < max_surface; ++i) {
        const Surface& surface = system_.surfaces[i];
        Ray local_ray = ray;
        local_ray.p.z -= vertex_z;
        double t = 0.0;
        const bool skip_target_clip = ignore_target_aperture && i == surface_index;
        const bool skip_clip = system_.defer_secondary_aperture_clipping(ray.wavelength_um) ||
                               ignore_intermediate_apertures || skip_target_clip;
        // Ignoring an aperture suppresses the post-intersection clip, but a
        // physical stop remains the preferred root-selection domain.  Aiming
        // probes outside that domain still need a residual, so only a genuine
        // in-domain miss may fall back to the unbounded mathematical surface.
        bool intersection_ok = false;
        if (ray.valid) {
            intersection_ok = intersect_surface(surface, local_ray, &t, false);
            if (!intersection_ok && skip_clip) {
                intersection_ok = intersect_surface(surface, local_ray, &t, true);
            }
        }
        if (!intersection_ok) {
            ray.valid = false;
            ray.intensity = 0.0;
        } else {
            const double n_pre = material_before.refractive_index(ray.wavelength_um);
            ray.p = ray.p + ray.d * t;
            ray.opd += std::abs(t * n_pre);
            const double clip_radius =
                skip_clip ? 0.0 : effective_aperture_clip_radius(i, ray.wavelength_um);
            if (clip_radius > 0.0) {
                const double radial = std::sqrt(ray.p.x * ray.p.x + ray.p.y * ray.p.y);
                if (aperture_clip_exceeded(radial, clip_radius)) {
                    ray.valid = false;
                    ray.intensity = 0.0;
                }
            }
            if (ray.valid && surface.kind != SurfaceKind::Image) {
                const Vec3 local_point{ray.p.x, ray.p.y, ray.p.z - vertex_z};
                const Vec3 normal = surface_normal(surface, local_point);
                if (surface.reflective) {
                    ray.d = reflect(ray.d, normal);
                } else {
                    const double n_post = surface.material_after.refractive_index(ray.wavelength_um);
                    Vec3 next_dir;
                    if (!refract(ray.d, normal, n_pre, n_post, &next_dir)) {
                        ray.valid = false;
                        ray.intensity = 0.0;
                    } else {
                        ray.d = next_dir;
                    }
                }
            }
        }

        if (i == surface_index && point) {
            point->z = ray.p.z;
            point->x = ray.p.x;
            point->y = ray.p.y;
            point->l = ray.d.x;
            point->m = ray.d.y;
            point->n = ray.d.z;
            point->intensity = ray.intensity;
            point->valid = ray.valid;
        }

        material_before = surface.material_after;
        if (std::isfinite(surface.thickness)) {
            vertex_z += surface.thickness;
        }
    }
    return point ? point->valid : ray.valid;
}

} // namespace optiland
