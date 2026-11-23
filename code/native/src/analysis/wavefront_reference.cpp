/*
 * Builds object-space and primary-wave reference geometry and final wavefront summaries.
 * Wavefront accumulation/sampling uses these corrections around traced optical paths.
 * Reference wavelength, refractive indices and OPD sign/unit conventions must agree
 * with the same chief ray and image surface used by the surrounding field evaluation.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
ObjectSpaceReferencePlane make_object_space_reference_plane(
    const LensSystem& system,
    const SequentialRayTracer& tracer,
    const Ray& chief_initial,
    double wavelength_um) {
    ObjectSpaceReferencePlane reference;
    if (std::isfinite(system.object_distance())) {
        return reference;
    }
    const std::size_t first = system.first_optical_surface();
    if (first >= system.surfaces.size()) {
        return reference;
    }
    TracePoint hit;
    if (!tracer.trace_to_surface(chief_initial, first, &hit, true, true) || !hit.valid) {
        return reference;
    }
    reference.enabled = true;
    reference.point = {hit.x, hit.y, hit.z};
    reference.normal = normalize(chief_initial.d);
    reference.refractive_index = object_space_refractive_index(system, wavelength_um);
    return reference;
}
}  // namespace analysis_detail

namespace analysis_detail {
double object_space_reference_offset(
    const ObjectSpaceReferencePlane& reference,
    const Ray& initial_ray) {
    if (!reference.enabled) {
        return 0.0;
    }
    const double distance_to_reference =
        dot(reference.point - initial_ray.p, reference.normal);
    return reference.refractive_index * distance_to_reference;
}
}  // namespace analysis_detail

namespace analysis_detail {
bool image_to_reference_sphere_opd(
    const Ray& ray,
    const Vec3& center,
    double radius,
    double n_image,
    double* opd_image_to_sphere,
    Vec3* pupil_point ) {
    if (!opd_image_to_sphere ||
        !std::isfinite(radius) ||
        radius <= kEps ||
        !std::isfinite(n_image) ||
        std::abs(n_image) <= kEps) {
        return false;
    }
    const Vec3 back{-ray.d.x, -ray.d.y, -ray.d.z};
    const Vec3 rel{ray.p.x - center.x, ray.p.y - center.y, ray.p.z - center.z};
    const double a = dot(back, back);
    const double b = 2.0 * dot(back, rel);
    const double c = dot(rel, rel) - radius * radius;
    if (!std::isfinite(a) || a <= kEps || !std::isfinite(b) || !std::isfinite(c)) {
        return false;
    }
    double discriminant = b * b - 4.0 * a * c;
    if (!std::isfinite(discriminant) || discriminant < -1.0e-9) {
        return false;
    }
    discriminant = std::max(0.0, discriminant);
    const double root = std::sqrt(discriminant);
    const double denom = 2.0 * a;
    double t = (-b - root) / denom;
    if (t < 0.0) {
        t = (-b + root) / denom;
    }
    if (!std::isfinite(t) || t < 0.0) {
        return false;
    }
    *opd_image_to_sphere = n_image * t;
    if (pupil_point) {
        *pupil_point = {
            ray.p.x + back.x * t,
            ray.p.y + back.y * t,
            ray.p.z + back.z * t,
        };
    }
    return std::isfinite(*opd_image_to_sphere);
}
}  // namespace analysis_detail

namespace analysis_detail {
std::size_t primary_wavelength_index(
    const LensSystem& system,
    const std::vector<Wavelength>& waves) {
    if (waves.empty()) {
        return 0;
    }
    for (std::size_t wi = 0; wi < waves.size(); ++wi) {
        if (waves[wi].primary) {
            return wi;
        }
    }
    const double primary_wave = system.primary_wavelength();
    std::size_t best = 0;
    double best_delta = std::numeric_limits<double>::infinity();
    for (std::size_t wi = 0; wi < waves.size(); ++wi) {
        const double delta = std::abs(waves[wi].value_um - primary_wave);
        if (std::isfinite(delta) && delta < best_delta) {
            best = wi;
            best_delta = delta;
        }
    }
    return best;
}
}  // namespace analysis_detail

namespace analysis_detail {
bool build_primary_wavefront_reference_sphere(
    const LensSystem& system,
    const MeritConfig& config,
    SequentialRayTracer& tracer,
    AimingSession& chief_aiming,
    std::size_t field_index,
    const Field& field,
    const std::vector<Wavelength>& waves,
    WavefrontReferenceSphere& reference) {
    if (waves.empty()) {
        return false;
    }
    const std::size_t primary_index = primary_wavelength_index(system, waves);
    const Wavelength& primary_wave = waves[primary_index];
    if (!std::isfinite(primary_wave.value_um)) {
        return false;
    }
    const Field& chief_field = field;
    const PupilPoint chief_pupil{0.0, 0.0, 1.0};
    Ray chief_initial = ray_aiming_uses_session(system.ray_aiming.mode)
                            ? chief_aiming.generate_chief(
                                  field_index,
                                  primary_index,
                                  chief_field,
                                  primary_wave)
                            : tracer.generate_ray(
                                  chief_field, chief_pupil, primary_wave.value_um);
    Ray chief = chief_initial;
    tracer.trace_ray(chief, nullptr);
    if (!ray_is_usable_for_wavefront(chief, config) && !normal_secondary_aperture_blockage(chief)) {
        return false;
    }
    const double image_z = system.surfaces.empty()
                               ? chief.p.z
                               : surface_vertex_z(system, system.surfaces.size() - 1);
    const double pupil_z = image_z + tracer.exit_pupil_location(primary_wave.value_um);
    const double radius = std::sqrt(
        chief.p.x * chief.p.x +
        chief.p.y * chief.p.y +
        (chief.p.z - pupil_z) * (chief.p.z - pupil_z));
    if (!std::isfinite(radius) || radius <= kEps) {
        return false;
    }
    reference.valid = true;
    reference.center = chief.p;
    reference.radius = radius;
    return true;
}
}  // namespace analysis_detail

namespace analysis_detail {
void finalize_wavefront_stats(WavefrontStatsAccumulator& accumulator) {
    WavefrontStats& stats = accumulator.stats;
    stats.coverage = stats.total_weight > 0.0
                         ? stats.valid_weight / stats.total_weight
                         : (stats.sampling_rejected_rays > 0 ? 1.0 : 0.0);
    if (accumulator.sum_weight <= 0.0 || stats.valid_rays == 0) {
        stats.rms_waves = std::numeric_limits<double>::quiet_NaN();
        return;
    }
    stats.mean_waves = accumulator.sum_opd / accumulator.sum_weight;
    stats.rms_waves = std::sqrt(accumulator.sum_opd2 / accumulator.sum_weight);
    if (std::isfinite(accumulator.min_opd) && std::isfinite(accumulator.max_opd)) {
        stats.peak_to_valley_waves = accumulator.max_opd - accumulator.min_opd;
    }
    stats.single_ray_max_abs_waves = accumulator.max_abs_opd;
}
}  // namespace analysis_detail

}  // namespace optiland
