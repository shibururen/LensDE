/*
 * Collects centroid-reference wavefront samples and coordinates per-field wavefront work.
 * The public wavefront entry points select these reference-specific sampling paths.
 * Invalid samples retain their slots, and wavelength/pupil offsets keep fitted models,
 * guard residuals and accumulated optical-path data aligned across the spectrum.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
void collect_centroid_dircos_wavefront_samples_for_field_wave(
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
    std::vector<CentroidDircosWavefrontSample>& samples,
    WavefrontStatsAccumulator& accumulator,
    std::size_t pupil_index_offset ) {
    WavefrontStats& stats = accumulator.stats;
    if (pupils.empty() ||
        !std::isfinite(wave.value_um) ||
        !std::isfinite(wave.weight) ||
        wave.weight <= 0.0) {
        return;
    }
    auto append_invalid_sample = [&](bool aperture_blocked = false) {
        samples.push_back({
            wave_index,
            std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN(),
            0.0, aperture_blocked});
    };
    auto append_invalid_samples = [&](std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            append_invalid_sample();
        }
    };

    if (!reference_sphere.valid ||
        !std::isfinite(reference_sphere.radius) ||
        reference_sphere.radius <= kEps) {
        stats.chief_ok = false;
        append_invalid_samples(pupils.size());
        return;
    }

    const double n_image = image_space_refractive_index(system, wave.value_um);
    if (!std::isfinite(n_image) || std::abs(n_image) <= kEps) {
        stats.chief_ok = false;
        append_invalid_samples(pupils.size());
        return;
    }

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
        append_invalid_samples(pupils.size());
        return;
    }

    double opd_img_ref = 0.0;
    if (!image_to_reference_sphere_opd(
            chief,
            reference_sphere.center,
            reference_sphere.radius,
            n_image,
            &opd_img_ref)) {
        stats.chief_ok = false;
        append_invalid_samples(pupils.size());
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
        pupils,
        false,
        pupil_index_offset,
        nullptr,
        pupil_index_offset == 1000000);
    std::vector<Ray>& rays = ray_batch.rays;
    std::vector<double> object_reference_offsets;
    object_reference_offsets.reserve(pupils.size());
    samples.reserve(samples.size() + pupils.size());
    for (std::size_t pi = 0; pi < pupils.size(); ++pi) {
        object_reference_offsets.push_back(
            object_space_reference_offset(object_reference, ray_batch.initial_rays[pi]));
        rays[pi].intensity *= wave.weight;
        rays[pi].nominal_weight *= wave.weight;
    }

    const double wavelength_mm = std::max(1.0e-12, wave.value_um * 1.0e-3);
    for (std::size_t pi = 0; pi < rays.size(); ++pi) {
        Ray& ray = rays[pi];
        const double weight = ray_nominal_weight(ray);
        if (calibrated_paraxial_stop_sampling_rejection(
                ray,
                system.ray_aiming.mode,
                static_cast<int>(tracer.stop_surface_index()))) {
            stats.sampling_rejected_weight += weight;
            ++stats.sampling_rejected_rays;
            append_invalid_sample(ray.aperture_blocked);
            continue;
        }
        stats.total_weight += weight;
        ++stats.attempted_rays;
        if (!ray_is_usable_for_wavefront(ray, config) || weight <= 0.0) {
            stats.failed_weight += weight;
            ++stats.failed_rays;
            append_invalid_sample();
            continue;
        }
        double opd_img = 0.0;
        Vec3 pupil_point;
        if (!image_to_reference_sphere_opd(
                ray,
                reference_sphere.center,
                reference_sphere.radius,
                n_image,
                &opd_img,
                &pupil_point)) {
            stats.failed_weight += weight;
            ++stats.failed_rays;
            append_invalid_sample();
            continue;
        }
        (void)pupil_point;
        const double object_offset =
            pi < object_reference_offsets.size() ? object_reference_offsets[pi] : 0.0;
        const double opd = ray.opd - object_offset - opd_img;
        const double opd_waves = (opd_ref - opd) / wavelength_mm;
        if (!std::isfinite(opd_waves) ||
            !std::isfinite(ray.d.x) ||
            !std::isfinite(ray.d.y)) {
            stats.failed_weight += weight;
            ++stats.failed_rays;
            append_invalid_sample();
            continue;
        }
        samples.push_back({
            wave_index,
            opd_waves,
            ray.d.x,
            ray.d.y,
            weight});
    }
}
}  // namespace analysis_detail

namespace analysis_detail {
void accumulate_centroid_dircos_wavefront_stats_for_field(
    const LensSystem& system,
    const MeritConfig& config,
    SequentialRayTracer& tracer,
    AimingSession& aiming,
    AimingSession& chief_aiming,
    std::size_t field_index,
    const Field& field,
    const std::vector<Wavelength>& waves,
    const std::vector<PupilPoint>& pupils,
    const WavefrontReferenceSphere& reference_sphere,
    WavefrontStatsAccumulator& accumulator) {
    std::vector<CentroidDircosWavefrontSample> samples;
    samples.reserve(pupils.size() * waves.size());
    for (std::size_t wi = 0; wi < waves.size(); ++wi) {
        collect_centroid_dircos_wavefront_samples_for_field_wave(
            system,
            config,
            tracer,
            aiming,
            chief_aiming,
            field_index,
            wi,
            field,
            waves[wi],
            pupils,
            reference_sphere,
            samples,
            accumulator);
    }

    if (samples.empty()) {
        return;
    }

    CentroidDircosWavefrontFit fit;
    const bool fitted =
        fit_centroid_dircos_wavefront_model(samples, waves.size(), true, true, fit) ||
        fit_centroid_dircos_wavefront_model(samples, waves.size(), true, false, fit) ||
        fit_centroid_dircos_wavefront_model(samples, waves.size(), false, true, fit) ||
        build_centroid_piston_only_wavefront_model(samples, waves.size(), fit);
    if (!fitted) {
        accumulator.stats.chief_ok = false;
        return;
    }

    std::vector<CentroidDircosWavefrontSample> guard_samples;
    const auto edge_pupils = missing_single_ray_guard_edge_pupils(pupils);
    WavefrontStatsAccumulator guard_accumulator;
    for (std::size_t wi = 0; wi < waves.size(); ++wi) {
        collect_centroid_dircos_wavefront_samples_for_field_wave(
            system,
            config,
            tracer,
            aiming,
            chief_aiming,
            field_index,
            wi,
            field,
            waves[wi],
            edge_pupils,
            reference_sphere,
            guard_samples,
            guard_accumulator,
            1000000);
    }

    WavefrontStats& stats = accumulator.stats;
    for (const CentroidDircosWavefrontSample& sample : samples) {
        if (sample.aperture_blocked) {
            stats.single_ray_wavefront_guard_residuals.push_back(0.0);
            continue;
        }
        const double model_value = centroid_dircos_wavefront_model_value(sample, fit);
        const double opd_waves = sample.opd_waves - model_value;
        const double weight = sample.weight;
        if (!std::isfinite(opd_waves) || weight <= 0.0) {
            stats.single_ray_wavefront_guard_residuals.push_back(
                std::numeric_limits<double>::quiet_NaN());
            continue;
        }
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
    for (const CentroidDircosWavefrontSample& sample : guard_samples) {
        if (sample.aperture_blocked) {
            stats.single_ray_wavefront_guard_residuals.push_back(0.0);
            continue;
        }
        const double model_value = centroid_dircos_wavefront_model_value(sample, fit);
        const double opd_waves = sample.opd_waves - model_value;
        if (std::isfinite(opd_waves)) {
            accumulator.max_abs_opd = std::max(accumulator.max_abs_opd, std::abs(opd_waves));
            stats.single_ray_wavefront_guard_residuals.push_back(opd_waves);
        } else {
            stats.single_ray_wavefront_guard_residuals.push_back(
                std::numeric_limits<double>::quiet_NaN());
        }
    }
}
}  // namespace analysis_detail

namespace analysis_detail {
WavefrontStats compute_wavefront_stats_for_field(
    const LensSystem& system,
    const MeritConfig& config,
    SequentialRayTracer& tracer,
    AimingSession& aiming,
    AimingSession& chief_aiming,
    std::size_t field_index,
    const Field& field,
    const std::vector<Wavelength>& waves,
    const std::vector<PupilPoint>& pupils) {
    WavefrontStatsAccumulator accumulator;
    // The initial sphere and OPD reference use the configured pupil's chief.
    // Keep its session isolated from ordinary sampling's pupil-slot cache;
    // the Field (including FVC/FVD) is identical, not a zero-vignetting copy.
    if (wavefront_uses_centroid_reference(config)) {
        WavefrontReferenceSphere reference_sphere;
        if (!build_primary_wavefront_reference_sphere(
                system,
                config,
                tracer,
                chief_aiming,
                field_index,
                field,
                waves,
                reference_sphere)) {
            accumulator.stats.chief_ok = false;
            finalize_wavefront_stats(accumulator);
            return accumulator.stats;
        }
        accumulate_centroid_dircos_wavefront_stats_for_field(
            system,
            config,
            tracer,
            aiming,
            chief_aiming,
            field_index,
            field,
            waves,
            pupils,
            reference_sphere,
            accumulator);
        finalize_wavefront_stats(accumulator);
        return accumulator.stats;
    }

    WavefrontReferenceSphere reference_sphere;
    if (!build_primary_wavefront_reference_sphere(
            system,
            config,
            tracer,
            chief_aiming,
            field_index,
            field,
            waves,
            reference_sphere)) {
        accumulator.stats.chief_ok = false;
        finalize_wavefront_stats(accumulator);
        return accumulator.stats;
    }
    for (std::size_t wi = 0; wi < waves.size(); ++wi) {
        accumulate_wavefront_stats_for_field_wave(
            system,
            config,
            tracer,
            aiming,
            chief_aiming,
            field_index,
            wi,
            field,
            waves[wi],
            pupils,
            reference_sphere,
            accumulator);
    }
    finalize_wavefront_stats(accumulator);
    return accumulator.stats;
}
}  // namespace analysis_detail

}  // namespace optiland
