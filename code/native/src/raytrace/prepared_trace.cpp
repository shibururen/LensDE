// Execute prepared batch traces and selected-history traces using a cached optical layout.
// Field evaluation reuses the prepared representation when its supported-system checks pass.
// Keep ray order, history selection, aperture decisions, and prepared-cache validity unchanged.

#include "prepared_geometry_inline.hpp"
#include "spectral_aperture_inline.hpp"
#include "trace_diagnostics_inline.hpp"
#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

RayTraceResult SequentialRayTracer::trace_field(
    const Field& field,
    double wavelength_um,
    int rings,
    bool keep_history) const {
    const auto pupils = make_hexapolar_distribution(rings);
    return trace_field(field, wavelength_um, pupils, keep_history);
}

RayTraceResult SequentialRayTracer::trace_field(
    const Field& field,
    double wavelength_um,
    const std::vector<PupilPoint>& pupils,
    bool keep_history) const {
    RayTraceResult result;
    result.rays.reserve(pupils.size());
    if (keep_history) {
        result.history_by_surface.resize(system_.surfaces.size());
    }

    AimingSession adaptive_session(system_);
    if (!keep_history) {
        for (std::size_t pi = 0; pi < pupils.size(); ++pi) {
            const auto& pupil = pupils[pi];
            Ray ray = ray_aiming_uses_session(system_.ray_aiming.mode)
                          ? adaptive_session.generate(0, 0, pi, field, Wavelength{wavelength_um, 1.0, true}, pupil)
                          : generate_ray(field, pupil, wavelength_um);
            result.rays.push_back(ray);
        }
        trace_rays(result.rays);
        return result;
    }

    for (std::size_t pi = 0; pi < pupils.size(); ++pi) {
        const auto& pupil = pupils[pi];
        Ray ray = ray_aiming_uses_session(system_.ray_aiming.mode)
                      ? adaptive_session.generate(0, 0, pi, field, Wavelength{wavelength_um, 1.0, true}, pupil)
                      : generate_ray(field, pupil, wavelength_um);
        std::vector<TracePoint> history;
        trace_ray(ray, keep_history ? &history : nullptr);
        if (keep_history) {
            for (std::size_t i = 0; i < history.size() && i < result.history_by_surface.size(); ++i) {
                result.history_by_surface[i].push_back(history[i]);
            }
        }
        result.rays.push_back(ray);
    }
    return result;
}

bool SequentialRayTracer::prepared_trace_supported() const {
    if (prepared_trace_support_cached_) {
        return prepared_trace_supported_;
    }
    bool supported = !system_.surfaces.empty();
    for (const auto& surface : system_.surfaces) {
        if (surface.reflective) {
            supported = false;
            break;
        }
        if (surface.kind == SurfaceKind::Object ||
            surface.kind == SurfaceKind::Image ||
            surface.kind == SurfaceKind::Plane) {
            continue;
        }
        if (surface.kind != SurfaceKind::Standard) {
            supported = false;
            break;
        }
        if (!surface.asphere_coefficients.empty()) {
            supported = false;
            break;
        }
        if (std::abs(surface.conic) > 1.0e-14) {
            supported = false;
            break;
        }
    }
    prepared_trace_supported_ = supported;
    prepared_trace_support_cached_ = true;
    return prepared_trace_supported_;
}

const SequentialRayTracer::PreparedTraceWaveCache&
SequentialRayTracer::prepared_trace_cache(double wavelength_um) const {
    for (const auto& cache : prepared_trace_wave_caches_) {
        if (std::abs(cache.wavelength_um - wavelength_um) < 1.0e-12) {
            return cache;
        }
    }
    PreparedTraceWaveCache cache;
    cache.wavelength_um = wavelength_um;
    cache.vertex_z.assign(system_.surfaces.size(), 0.0);
    cache.n_before.assign(system_.surfaces.size(), 1.0);
    cache.n_after.assign(system_.surfaces.size(), 1.0);
    cache.aperture_clip_radius.assign(system_.surfaces.size(), 0.0);
    cache.aperture_clip_limit_sq.assign(system_.surfaces.size(), 0.0);
    cache.radius.assign(system_.surfaces.size(), 0.0);
    cache.inv_radius.assign(system_.surfaces.size(), 0.0);
    cache.backward_z_tol.assign(system_.surfaces.size(), 0.0);
    cache.backward_reference.assign(system_.surfaces.size(), 1.0);
    cache.geometry.assign(system_.surfaces.size(), kPreparedGeometrySkip);
    cache.refract_surface.assign(system_.surfaces.size(), 0);
    Material material_before = Material::air();
    double vertex_z = 0.0;
    const std::size_t first = system_.first_optical_surface();
    for (std::size_t i = 0; i < system_.surfaces.size(); ++i) {
        const Surface& surface = system_.surfaces[i];
        cache.vertex_z[i] = i < first ? 0.0 : vertex_z;
        cache.n_before[i] = material_before.refractive_index(wavelength_um);
        cache.n_after[i] = surface.material_after.refractive_index(wavelength_um);
        cache.backward_z_tol[i] = 1.0e-10 * std::max(1.0, std::abs(surface.thickness));
        cache.backward_reference[i] = std::max(1.0e-9, std::abs(surface.thickness));
        if (i >= first && surface.kind != SurfaceKind::Object) {
            if (surface.is_plane()) {
                cache.geometry[i] = kPreparedGeometryPlane;
            } else {
                cache.geometry[i] = kPreparedGeometrySphere;
                cache.radius[i] = surface.radius;
                cache.inv_radius[i] = 1.0 / surface.radius;
            }
            cache.refract_surface[i] = surface.kind == SurfaceKind::Image ? 0 : 1;
        }
        double clip_radius = surface.aperture_radius;
        if (clip_radius <= 0.0 && surface.clear_aperture_fixed) {
            clip_radius = surface.semi_aperture;
        }
        if (surface.is_stop &&
            !surface.clear_aperture_fixed &&
            system_.aperture.type != ApertureType::FloatByStopSize) {
            if (system_.ray_aiming.stop_radius_source == RayAimingStopRadiusSource::Paraxial) {
                // Keep the prepared fast path on the same canonical stop-radius
                // contract as the generic tracer and CUDA scorer. In particular,
                // aperture_stop_radius() evaluates a paraxial stop at the primary
                // wavelength instead of changing the physical stop by ray color.
                clip_radius = aperture_stop_radius(wavelength_um);
            } else {
                clip_radius = std::numeric_limits<double>::quiet_NaN();
            }
        }
        cache.aperture_clip_radius[i] = clip_radius;
        cache.aperture_clip_limit_sq[i] = aperture_clip_limit_sq(clip_radius);
        material_before = surface.material_after;
        if (i >= first && std::isfinite(surface.thickness)) {
            vertex_z += surface.thickness;
        }
    }
    prepared_trace_wave_caches_.push_back(std::move(cache));
    return prepared_trace_wave_caches_.back();
}

bool SequentialRayTracer::trace_ray_prepared_no_history(Ray& ray) const {
    const std::size_t first = system_.first_optical_surface();
    const double object_space_dz = ray.d.z;
    const PreparedTraceWaveCache& wave_cache = prepared_trace_cache(ray.wavelength_um);
    ray.stop_x = std::numeric_limits<double>::quiet_NaN();
    ray.stop_y = std::numeric_limits<double>::quiet_NaN();
    ray.stop_reached = false;
    ray.min_backward_air_gap = 0.0;
    ray.min_backward_glass_gap = 0.0;
    double px = ray.p.x;
    double py = ray.p.y;
    double pz = ray.p.z;
    double dx = ray.d.x;
    double dy = ray.d.y;
    double dz = ray.d.z;
    double opd = ray.opd;

    for (std::size_t i = 0; i < system_.surfaces.size(); ++i) {
        const unsigned char geometry = wave_cache.geometry[i];
        if (geometry == kPreparedGeometrySkip) {
            continue;
        }
        const double vertex_z = wave_cache.vertex_z[i];

        double t = 0.0;
        if (!ray.valid) {
            ray.intensity = 0.0;
            break;
        }

        const bool intersect_ok = prepared_geometry_intersect(
            geometry,
            wave_cache.radius[i],
            wave_cache.inv_radius[i],
            px,
            py,
            pz - vertex_z,
            dx,
            dy,
            dz,
            &t);

        if (!intersect_ok) {
            ray.p = {px, py, pz};
            ray.d = {dx, dy, dz};
            ray.opd = opd;
            mark_ray_failure(ray, RayFailureCode::SurfaceMiss, static_cast<int>(i), 1.0);
        } else {
            const double step_z = t * dz;
            const double z_tol = wave_cache.backward_z_tol[i];
            const bool backward_by_parameter = t < -1.0e-10;
            const bool backward_by_z = step_z < -z_tol;
            const bool allowed_object_space_retro =
                i == first &&
                object_space_dz < -1.0e-12 &&
                t >= -1.0e-10 &&
                backward_by_z;
            if ((backward_by_parameter || backward_by_z) && !allowed_object_space_retro) {
                const double reference = wave_cache.backward_reference[i];
                const double severity = std::max(
                    std::abs(t),
                    std::abs(step_z) / reference);
                mark_backward_propagation_warning(ray, static_cast<int>(i), severity);
                record_backward_surface_overlap(
                    ray, system_, i, vertex_z, px, py, pz);
            }
            const double n_pre = wave_cache.n_before[i];
            px += dx * t;
            py += dy * t;
            pz += dz * t;
            opd += std::abs(t * n_pre);

            if (system_.surfaces[i].is_stop && !ray.stop_reached &&
                std::isfinite(px) && std::isfinite(py)) {
                ray.stop_x = px;
                ray.stop_y = py;
                ray.stop_reached = true;
            }

            double clip_radius = wave_cache.aperture_clip_radius[i];
            double clip_limit_sq = wave_cache.aperture_clip_limit_sq[i];
            if (!std::isfinite(clip_limit_sq)) {
                clip_radius = effective_aperture_clip_radius(i, ray.wavelength_um);
                clip_limit_sq = aperture_clip_limit_sq(clip_radius);
            }
            if (system_.defer_secondary_aperture_clipping(ray.wavelength_um)) {
                record_secondary_aperture_hit(ray, system_.surfaces[i], static_cast<int>(i), px, py);
            } else if (aperture_clip_exceeded_sq(px * px + py * py, clip_limit_sq)) {
                const double radial = std::sqrt(px * px + py * py);
                ray.p = {px, py, pz};
                ray.d = {dx, dy, dz};
                ray.opd = opd;
                mark_ray_failure(
                    ray,
                    RayFailureCode::ApertureClip,
                    static_cast<int>(i),
                    (radial - clip_radius) / std::max(clip_radius, kEps));
            }

            if (ray.valid && wave_cache.refract_surface[i]) {
                const Vec3 normal = prepared_geometry_normal(
                    geometry,
                    wave_cache.radius[i],
                    wave_cache.inv_radius[i],
                    px,
                    py,
                    pz - vertex_z);
                Vec3 next_dir;
                if (!refract_assume_unit({dx, dy, dz}, normal, n_pre, wave_cache.n_after[i], &next_dir)) {
                    ray.p = {px, py, pz};
                    ray.d = {dx, dy, dz};
                    ray.opd = opd;
                    mark_ray_failure(ray, RayFailureCode::TotalInternalReflection, static_cast<int>(i), 1.0);
                } else {
                    dx = next_dir.x;
                    dy = next_dir.y;
                    dz = next_dir.z;
                }
            }
        }
    }
    ray.p = {px, py, pz};
    ray.d = {dx, dy, dz};
    ray.opd = opd;
    return ray.valid;
}

void SequentialRayTracer::trace_rays_prepared_no_history(
    std::vector<Ray>& rays,
    const std::vector<unsigned char>* history_mask,
    std::vector<std::vector<TracePoint>>* histories) const {
    if (rays.empty()) {
        if (histories) {
            histories->clear();
        }
        return;
    }
    const bool collect_selected_histories =
        histories && history_mask && history_mask->size() == rays.size();
    std::vector<std::size_t> history_indices;
    if (collect_selected_histories) {
        histories->assign(rays.size(), {});
        for (std::size_t ri = 0; ri < rays.size(); ++ri) {
            if ((*history_mask)[ri] != 0) {
                history_indices.push_back(ri);
                (*histories)[ri].resize(system_.surfaces.size());
            }
        }
    } else if (histories) {
        histories->assign(rays.size(), {});
    }
    auto record_selected_surface = [&](std::size_t surface_index) {
        if (!collect_selected_histories) {
            return;
        }
        for (std::size_t ri : history_indices) {
            const Ray& ray = rays[ri];
            TracePoint& point = (*histories)[ri][surface_index];
            point.z = ray.p.z;
            point.x = ray.p.x;
            point.y = ray.p.y;
            point.l = ray.d.x;
            point.m = ray.d.y;
            point.n = ray.d.z;
            point.intensity = ray.intensity;
            point.valid = ray.valid;
        }
    };
    for (const auto& ray : rays) {
        (void)prepared_trace_cache(ray.wavelength_um);
    }
    std::vector<const PreparedTraceWaveCache*> ray_caches;
    ray_caches.reserve(rays.size());
    for (const auto& ray : rays) {
        ray_caches.push_back(&prepared_trace_cache(ray.wavelength_um));
    }

    const std::size_t first = system_.first_optical_surface();
    std::vector<double> object_space_dz(rays.size(), 0.0);
    for (std::size_t ri = 0; ri < rays.size(); ++ri) {
        object_space_dz[ri] = rays[ri].d.z;
        rays[ri].stop_x = std::numeric_limits<double>::quiet_NaN();
        rays[ri].stop_y = std::numeric_limits<double>::quiet_NaN();
        rays[ri].stop_reached = false;
        rays[ri].min_backward_air_gap = 0.0;
        rays[ri].min_backward_glass_gap = 0.0;
    }

    for (std::size_t i = 0; i < system_.surfaces.size(); ++i) {
        const PreparedTraceWaveCache& first_cache = *ray_caches.front();
        const unsigned char geometry = first_cache.geometry[i];
        if (geometry == kPreparedGeometrySkip) {
            record_selected_surface(i);
            continue;
        }
        for (std::size_t ri = 0; ri < rays.size(); ++ri) {
            Ray& ray = rays[ri];
            if (!ray.valid) {
                ray.intensity = 0.0;
                continue;
            }
            const PreparedTraceWaveCache& wave_cache = *ray_caches[ri];
            const double vertex_z = wave_cache.vertex_z[i];
            const double px0 = ray.p.x;
            const double py0 = ray.p.y;
            const double dx0 = ray.d.x;
            const double dy0 = ray.d.y;
            const double dz0 = ray.d.z;

            double t = 0.0;
            const bool intersect_ok = prepared_geometry_intersect(
                geometry,
                wave_cache.radius[i],
                wave_cache.inv_radius[i],
                px0,
                py0,
                ray.p.z - vertex_z,
                dx0,
                dy0,
                dz0,
                &t);
            if (!intersect_ok) {
                mark_ray_failure(ray, RayFailureCode::SurfaceMiss, static_cast<int>(i), 1.0);
                continue;
            }

            const double step_z = t * dz0;
            const double z_tol = wave_cache.backward_z_tol[i];
            const bool backward_by_parameter = t < -1.0e-10;
            const bool backward_by_z = step_z < -z_tol;
            const bool allowed_object_space_retro =
                i == first &&
                object_space_dz[ri] < -1.0e-12 &&
                t >= -1.0e-10 &&
                backward_by_z;
            if ((backward_by_parameter || backward_by_z) && !allowed_object_space_retro) {
                const double reference = wave_cache.backward_reference[i];
                const double severity = std::max(
                    std::abs(t),
                    std::abs(step_z) / reference);
                mark_backward_propagation_warning(ray, static_cast<int>(i), severity);
                record_backward_surface_overlap(
                    ray, system_, i, vertex_z, px0, py0, ray.p.z);
            }

            const double n_pre = wave_cache.n_before[i];
            ray.p.x += dx0 * t;
            ray.p.y += dy0 * t;
            ray.p.z += dz0 * t;
            ray.opd += std::abs(t * n_pre);

            if (system_.surfaces[i].is_stop && !ray.stop_reached &&
                std::isfinite(ray.p.x) && std::isfinite(ray.p.y)) {
                ray.stop_x = ray.p.x;
                ray.stop_y = ray.p.y;
                ray.stop_reached = true;
            }

            double clip_radius = wave_cache.aperture_clip_radius[i];
            double clip_limit_sq = wave_cache.aperture_clip_limit_sq[i];
            if (!std::isfinite(clip_limit_sq)) {
                clip_radius = effective_aperture_clip_radius(i, ray.wavelength_um);
                clip_limit_sq = aperture_clip_limit_sq(clip_radius);
            }
            const double radial_sq = ray.p.x * ray.p.x + ray.p.y * ray.p.y;
            if (system_.defer_secondary_aperture_clipping(ray.wavelength_um)) {
                record_secondary_aperture_hit(ray, system_.surfaces[i], static_cast<int>(i), ray.p.x, ray.p.y);
            } else if (aperture_clip_exceeded_sq(radial_sq, clip_limit_sq)) {
                const double radial = std::sqrt(radial_sq);
                    mark_ray_failure(
                        ray,
                        RayFailureCode::ApertureClip,
                        static_cast<int>(i),
                        (radial - clip_radius) / std::max(clip_radius, kEps));
            }

            if (ray.valid && wave_cache.refract_surface[i]) {
                const Vec3 normal = prepared_geometry_normal(
                    geometry,
                    wave_cache.radius[i],
                    wave_cache.inv_radius[i],
                    ray.p.x,
                    ray.p.y,
                    ray.p.z - vertex_z);
                Vec3 next_dir;
                if (!refract_assume_unit(ray.d, normal, n_pre, wave_cache.n_after[i], &next_dir)) {
                    mark_ray_failure(ray, RayFailureCode::TotalInternalReflection, static_cast<int>(i), 1.0);
                } else {
                    ray.d = next_dir;
                }
            }
        }
        record_selected_surface(i);
    }
}

void SequentialRayTracer::trace_rays(std::vector<Ray>& rays) const {
    if (rays.empty()) {
        return;
    }
    if (prepared_trace_supported()) {
        trace_rays_prepared_no_history(rays);
        return;
    }
    for (auto& ray : rays) {
        trace_ray(ray, nullptr);
    }
}

void SequentialRayTracer::trace_rays_with_selected_histories(
    std::vector<Ray>& rays,
    const std::vector<unsigned char>& history_mask,
    std::vector<std::vector<TracePoint>>* histories) const {
    if (!histories) {
        trace_rays(rays);
        return;
    }
    histories->assign(rays.size(), {});
    if (rays.empty()) {
        return;
    }
    if (history_mask.size() != rays.size()) {
        trace_rays(rays);
        return;
    }
    if (prepared_trace_supported()) {
        trace_rays_prepared_no_history(rays, &history_mask, histories);
        return;
    }
    for (std::size_t ri = 0; ri < rays.size(); ++ri) {
        trace_ray(
            rays[ri],
            history_mask[ri] != 0 ? &(*histories)[ri] : nullptr);
    }
}

bool SequentialRayTracer::trace_to_surface_prepared(
    Ray ray,
    std::size_t surface_index,
    TracePoint* point,
    bool ignore_target_aperture,
    bool ignore_intermediate_apertures) const {
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

    const std::size_t max_surface = std::min(surface_index + 1, system_.surfaces.size());
    const PreparedTraceWaveCache& wave_cache = prepared_trace_cache(ray.wavelength_um);
    double px = ray.p.x;
    double py = ray.p.y;
    double pz = ray.p.z;
    double dx = ray.d.x;
    double dy = ray.d.y;
    double dz = ray.d.z;
    bool valid = ray.valid;
    double intensity = ray.intensity;

    for (std::size_t i = first; i < max_surface; ++i) {
        const unsigned char geometry = wave_cache.geometry[i];
        const double vertex_z = wave_cache.vertex_z[i];
        if (!valid) {
            intensity = 0.0;
        } else {
            double t = 0.0;
            const bool intersect_ok = prepared_geometry_intersect(
                geometry,
                wave_cache.radius[i],
                wave_cache.inv_radius[i],
                px,
                py,
                pz - vertex_z,
                dx,
                dy,
                dz,
                &t);

            if (!intersect_ok) {
                valid = false;
                intensity = 0.0;
            } else {
                const double n_pre = wave_cache.n_before[i];
                px += dx * t;
                py += dy * t;
                pz += dz * t;

                const bool skip_target_clip = ignore_target_aperture && i == surface_index;
                const bool skip_clip = system_.defer_secondary_aperture_clipping(ray.wavelength_um) ||
                    ignore_intermediate_apertures || skip_target_clip;
                double clip_radius = 0.0;
                double clip_limit_sq = 0.0;
                if (!skip_clip) {
                    clip_radius = wave_cache.aperture_clip_radius[i];
                    clip_limit_sq = wave_cache.aperture_clip_limit_sq[i];
                    if (!std::isfinite(clip_limit_sq)) {
                        clip_radius = effective_aperture_clip_radius(i, ray.wavelength_um);
                        clip_limit_sq = aperture_clip_limit_sq(clip_radius);
                    }
                }
                if (aperture_clip_exceeded_sq(px * px + py * py, clip_limit_sq)) {
                    valid = false;
                    intensity = 0.0;
                }

                if (valid && wave_cache.refract_surface[i]) {
                    const Vec3 normal = prepared_geometry_normal(
                        geometry,
                        wave_cache.radius[i],
                        wave_cache.inv_radius[i],
                        px,
                        py,
                        pz - vertex_z);
                    Vec3 next_dir;
                    if (!refract_assume_unit({dx, dy, dz}, normal, n_pre, wave_cache.n_after[i], &next_dir)) {
                        valid = false;
                        intensity = 0.0;
                    } else {
                        dx = next_dir.x;
                        dy = next_dir.y;
                        dz = next_dir.z;
                    }
                }
            }
        }

        if (i == surface_index && point) {
            point->z = pz;
            point->x = px;
            point->y = py;
            point->l = dx;
            point->m = dy;
            point->n = dz;
            point->intensity = intensity;
            point->valid = valid;
        }
    }
    return point ? point->valid : valid;
}

} // namespace optiland
