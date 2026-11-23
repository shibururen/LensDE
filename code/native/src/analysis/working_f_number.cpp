/*
 * Measures on-axis working F-number and image-space NA from traced marginal directions.
 * Merit measurement stores these metrics for first-order operands and score reporting.
 * Use the selected primary wavelength, physical image-space index and valid marginal
 * rays; missing image geometry or degenerate directions retain unavailable metrics.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

WorkingFNumberMetrics working_f_number_metrics(const LensSystem& system) {
    WorkingFNumberMetrics metrics;
    if (system.surfaces.empty()) {
        return metrics;
    }

    std::size_t image_index = system.surfaces.size();
    for (std::size_t i = system.first_optical_surface();
         i < system.surfaces.size();
         ++i) {
        if (system.surfaces[i].kind == SurfaceKind::Image) {
            image_index = i;
            break;
        }
    }
    if (image_index >= system.surfaces.size() || image_index == 0) {
        return metrics;
    }

    std::size_t wave_index = 0;
    Wavelength wave{system.primary_wavelength(), 1.0, true};
    for (std::size_t i = 0; i < system.wavelengths.size(); ++i) {
        if (system.wavelengths[i].primary) {
            wave = system.wavelengths[i];
            wave_index = i;
            break;
        }
        if (i == 0) {
            wave = system.wavelengths[i];
        }
    }
    wave.weight = 1.0;
    wave.primary = true;

    const double image_index_of_refraction =
        system.surfaces[image_index - 1].material_after.refractive_index(
            wave.value_um);
    if (!std::isfinite(image_index_of_refraction) ||
        image_index_of_refraction <= kEps) {
        return metrics;
    }

    Field axis_field{};
    std::size_t axis_field_index = 0;
    double axis_field_radius = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < system.fields.size(); ++i) {
        const double radius = std::hypot(system.fields[i].hx, system.fields[i].hy);
        if (std::isfinite(radius) && radius < axis_field_radius) {
            axis_field_radius = radius;
            axis_field_index = i;
        }
    }
    if (axis_field_radius <= 1.0e-9) {
        axis_field = system.fields[axis_field_index];
    } else {
        // Working F/# is defined on axis even when an imported field table
        // omits an explicit axis field.
        axis_field_index = 0;
    }
    SequentialRayTracer tracer(system);
    AimingSession aiming(system);
    std::size_t pupil_index = 0;
    auto trace_direction = [&](const PupilPoint& pupil, Vec3* direction) {
        if (!direction) {
            return false;
        }
        Ray ray = ray_aiming_uses_session(system.ray_aiming.mode)
                      ? aiming.generate(
                            axis_field_index,
                            wave_index,
                            pupil_index++,
                            axis_field,
                            wave,
                            pupil)
                      : tracer.generate_ray(axis_field, pupil, wave.value_um);
        TracePoint point;
        if (!ray.valid ||
            !tracer.trace_to_surface(ray, image_index, &point) ||
            !point.valid) {
            return false;
        }
        const double norm_sq = point.l * point.l + point.m * point.m +
                               point.n * point.n;
        if (!std::isfinite(norm_sq) || norm_sq <= kEps) {
            return false;
        }
        const double inv_norm = 1.0 / std::sqrt(norm_sq);
        *direction = {
            point.l * inv_norm,
            point.m * inv_norm,
            point.n * inv_norm,
        };
        return true;
    };

    Vec3 chief_direction;
    if (!trace_direction(PupilPoint{0.0, 0.0, 1.0}, &chief_direction)) {
        return metrics;
    }

    double numerical_aperture_squared_sum = 0.0;
    for (const PupilPoint& pupil : single_ray_guard_edge_pupils()) {
        Vec3 marginal_direction;
        if (!trace_direction(pupil, &marginal_direction)) {
            continue;
        }
        const double cosine = std::clamp(
            dot(chief_direction, marginal_direction),
            -1.0,
            1.0);
        const double sine_squared = std::max(0.0, 1.0 - cosine * cosine);
        numerical_aperture_squared_sum +=
            image_index_of_refraction * image_index_of_refraction * sine_squared;
        ++metrics.valid_marginal_rays;
    }
    if (metrics.valid_marginal_rays != 4) {
        return metrics;
    }

    metrics.image_space_numerical_aperture =
        std::sqrt(numerical_aperture_squared_sum / 4.0);
    if (std::isfinite(metrics.image_space_numerical_aperture) &&
        metrics.image_space_numerical_aperture > kEps) {
        metrics.working_f_number =
            1.0 / (2.0 * metrics.image_space_numerical_aperture);
    }
    return metrics;
}

}  // namespace optiland
