/*
 * Accumulates optical-path wavefront statistics for one field/wavelength pupil set.
 * Wavefront field evaluation supplies tracing/aiming sessions and its reference sphere.
 * Object-plane/reference-sphere corrections, wavelength weights and guard slots retain
 * their pupil correspondence before RMS and single-ray wavefront residuals are formed.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
void accumulate_wavefront_stats_for_field_wave(
    const LensSystem& system,
    const MeritConfig& config,
    SequentialRayTracer& tracer,
    AimingSession& aiming,
    AimingSession& chief_aiming,
    std::size_t field_index,
    std::size_t wave_index,
    const Field& field,
    const Wavelength& wave,
    const std::vector<PupilPoint>& pupils,
    const WavefrontReferenceSphere& reference_sphere,
    WavefrontStatsAccumulator& accumulator) {
    WavefrontStats& stats = accumulator.stats;
    if (pupils.empty() ||
        !std::isfinite(wave.value_um) ||
        !std::isfinite(wave.weight) ||
        wave.weight <= 0.0) {
        return;
    }
    const std::size_t guard_slot_count =
        pupils.size() + missing_single_ray_guard_edge_pupils(pupils).size();

    const Field& chief_field = field;
    const PupilPoint chief_pupil{0.0, 0.0, 1.0};
    Ray chief_initial = ray_aiming_uses_session(system.ray_aiming.mode)
                            ? chief_aiming.generate_chief(
                                  field_index,
                                  wave_index,
                                  chief_field,
                                  wave)
                            : tracer.generate_ray(
                                  chief_field, chief_pupil, wave.value_um);
    const ObjectSpaceReferencePlane object_reference =
        make_object_space_reference_plane(system, tracer, chief_initial, wave.value_um);
    Ray chief = chief_initial;
    tracer.trace_ray(chief, nullptr);
    if (!ray_is_usable_for_wavefront(chief, config) && !normal_secondary_aperture_blockage(chief)) {
        stats.chief_ok = false;
        append_invalid_wavefront_guard_residuals(stats, guard_slot_count);
        return;
    }

    if (!reference_sphere.valid ||
        !std::isfinite(reference_sphere.radius) ||
        reference_sphere.radius <= kEps) {
        stats.chief_ok = false;
        append_invalid_wavefront_guard_residuals(stats, guard_slot_count);
        return;
    }
    const Vec3 center = reference_sphere.center;
    const double radius = reference_sphere.radius;
    const double n_image = image_space_refractive_index(system, wave.value_um);
    double opd_img_ref = 0.0;
    if (!image_to_reference_sphere_opd(chief, center, radius, n_image, &opd_img_ref)) {
        stats.chief_ok = false;
        append_invalid_wavefront_guard_residuals(stats, guard_slot_count);
        return;
    }
    const double chief_object_offset =
        object_space_reference_offset(object_reference, chief_initial);
    const double opd_ref = chief.opd - chief_object_offset - opd_img_ref;

    SymmetricTraceBatch ray_batch = trace_pupil_batch(
        system,
        config,
        tracer,
        aiming,
        field_index,
        wave_index,
        field,
        wave,
        pupils);
    std::vector<Ray>& rays = ray_batch.rays;
    std::vector<double> object_reference_offsets;
    object_reference_offsets.reserve(pupils.size());
    for (std::size_t pi = 0; pi < pupils.size(); ++pi) {
        object_reference_offsets.push_back(
            object_space_reference_offset(object_reference, ray_batch.initial_rays[pi]));
        rays[pi].intensity *= wave.weight;
        rays[pi].nominal_weight *= wave.weight;
    }

    const double wavelength_mm = std::max(1.0e-12, wave.value_um * 1.0e-3);
    struct WavefrontSample {
        double opd_waves = std::numeric_limits<double>::quiet_NaN();
        double weight = 0.0;
        bool aperture_blocked = false;
    };
    std::vector<WavefrontSample> valid_samples;
    valid_samples.reserve(rays.size());
    double piston_weight = 0.0;
    double piston_sum = 0.0;

    for (std::size_t pi = 0; pi < rays.size(); ++pi) {
        Ray& ray = rays[pi];
        const double weight = ray_nominal_weight(ray);
        if (calibrated_paraxial_stop_sampling_rejection(
                ray,
                system.ray_aiming.mode,
                static_cast<int>(tracer.stop_surface_index()))) {
            stats.sampling_rejected_weight += weight;
            ++stats.sampling_rejected_rays;
            valid_samples.push_back({std::numeric_limits<double>::quiet_NaN(), 0.0, ray.aperture_blocked});
            continue;
        }
        stats.total_weight += weight;
        ++stats.attempted_rays;
        if (normal_secondary_aperture_blockage(ray)) {
            stats.single_ray_wavefront_guard_residuals.push_back(0.0);
            continue;
        }
        if (!ray_is_usable_for_wavefront(ray, config)) {
            stats.failed_weight += weight;
            ++stats.failed_rays;
            valid_samples.push_back({});
            continue;
        }
        double opd_img = 0.0;
        Vec3 pupil_point;
        if (!image_to_reference_sphere_opd(ray, center, radius, n_image, &opd_img, &pupil_point)) {
            stats.failed_weight += weight;
            ++stats.failed_rays;
            valid_samples.push_back({});
            continue;
        }
        const double object_offset =
            pi < object_reference_offsets.size() ? object_reference_offsets[pi] : 0.0;
        const double opd = ray.opd - object_offset - opd_img;
        const double opd_waves = (opd_ref - opd) / wavelength_mm;
        if (!std::isfinite(opd_waves) || weight <= 0.0) {
            stats.failed_weight += weight;
            ++stats.failed_rays;
            valid_samples.push_back({});
            continue;
        }
        (void)pupil_point;
        valid_samples.push_back({opd_waves, weight});
        piston_weight += weight;
        piston_sum += weight * opd_waves;
    }

    if (valid_samples.empty() || piston_weight <= 0.0) {
        append_invalid_wavefront_guard_residuals(stats, guard_slot_count);
        return;
    }
    const double piston_waves = piston_sum / piston_weight;
    for (const WavefrontSample& sample : valid_samples) {
        if (sample.aperture_blocked) {
            stats.single_ray_wavefront_guard_residuals.push_back(0.0);
            continue;
        }
        if (!std::isfinite(sample.opd_waves) || sample.weight <= 0.0) {
            stats.single_ray_wavefront_guard_residuals.push_back(
                std::numeric_limits<double>::quiet_NaN());
            continue;
        }
        const double opd_waves = sample.opd_waves - piston_waves;
        const double weight = sample.weight;
        accumulator.sum_weight += weight;
        accumulator.sum_opd += weight * opd_waves;
        accumulator.sum_opd2 += weight * opd_waves * opd_waves;
        accumulator.min_opd = std::min(accumulator.min_opd, opd_waves);
        accumulator.max_opd = std::max(accumulator.max_opd, opd_waves);
        accumulator.max_abs_opd = std::max(accumulator.max_abs_opd, std::abs(opd_waves));
        stats.single_ray_wavefront_guard_residuals.push_back(opd_waves);
        stats.valid_weight += weight;
        ++stats.valid_rays;
    }

    const auto edge_pupils = missing_single_ray_guard_edge_pupils(pupils);
    constexpr std::size_t kGuardPupilIndexOffset = 1000000;
    for (std::size_t ei = 0; ei < edge_pupils.size(); ++ei) {
        Ray ray = ray_aiming_uses_session(system.ray_aiming.mode)
                      ? aiming.generate_image_guard(
                            field_index,
                            wave_index,
                            kGuardPupilIndexOffset + ei,
                            field,
                            wave,
                            edge_pupils[ei])
                      : tracer.generate_ray(field, edge_pupils[ei], wave.value_um);
        const double object_offset = object_space_reference_offset(object_reference, ray);
        ray.nominal_weight *= wave.weight;
        tracer.trace_ray(ray, nullptr);
        double guard_opd_waves = std::numeric_limits<double>::quiet_NaN();
        if (normal_secondary_aperture_blockage(ray)) {
            stats.single_ray_wavefront_guard_residuals.push_back(0.0);
            continue;
        }
        if (!ray_is_usable_for_wavefront(ray, config)) {
            stats.single_ray_wavefront_guard_residuals.push_back(guard_opd_waves);
            continue;
        }
        double opd_img = 0.0;
        Vec3 pupil_point;
        if (!image_to_reference_sphere_opd(ray, center, radius, n_image, &opd_img, &pupil_point)) {
            stats.single_ray_wavefront_guard_residuals.push_back(guard_opd_waves);
            continue;
        }
        (void)pupil_point;
        const double opd = ray.opd - object_offset - opd_img;
        const double opd_waves = (opd_ref - opd) / wavelength_mm - piston_waves;
        if (std::isfinite(opd_waves)) {
            accumulator.max_abs_opd = std::max(accumulator.max_abs_opd, std::abs(opd_waves));
            guard_opd_waves = opd_waves;
        }
        stats.single_ray_wavefront_guard_residuals.push_back(guard_opd_waves);
    }
}
}  // namespace analysis_detail

}  // namespace optiland
