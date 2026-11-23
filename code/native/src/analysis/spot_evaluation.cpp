/*
 * Traces field/wavelength pupil sets for monochromatic and polychromatic spot statistics.
 * Public spot entry points and MeritEvaluation call these routines with local or shared contexts.
 * Preserve pupil indices, spectral weights, symmetry/history reuse and guard-ray
 * augmentation when combining traced samples into per-field image-quality results.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

std::vector<FieldSpot> spot_diagram_stats(const LensSystem& system, int rings) {
    MeritConfig config;
    config.num_rays = rings;
    config.pupil_distribution = "hexapolar";
    return spot_diagram_stats(system, config);
}

std::vector<FieldSpot> spot_diagram_stats(const LensSystem& system, const MeritConfig& config) {
    std::vector<FieldSpot> out;
    SequentialRayTracer tracer(system);
    AimingSession aiming(system);
    const auto fields = fields_or_default(system);
    const auto waves = wavelengths_or_default(system);
    for (std::size_t fi = 0; fi < fields.size(); ++fi) {
        const auto pupils = merit_field_pupils(
            system, config, fields[fi], MeritPupilMetric::Spot);
        for (std::size_t wi = 0; wi < waves.size(); ++wi) {
            const auto& field = fields[fi];
            const auto& wave = waves[wi];
            if (ray_aiming_uses_session(system.ray_aiming.mode)) {
                std::vector<Ray> rays;
                rays.reserve(pupils.size());
                for (std::size_t pi = 0; pi < pupils.size(); ++pi) {
                    Ray ray = aiming.generate(fi, wi, pi, field, wave, pupils[pi]);
                    tracer.trace_ray(ray, nullptr);
                    rays.push_back(ray);
                }
                out.push_back({field, wave.value_um, compute_spot_stats(rays)});
            } else {
                const auto traced = tracer.trace_field(field, wave.value_um, pupils, false);
                out.push_back({field, wave.value_um, compute_spot_stats(traced.rays)});
            }
        }
    }
    return out;
}

std::vector<FieldSpot> polychromatic_field_spot_stats_with_context(
    const LensSystem& system,
    const MeritConfig& config,
    SequentialRayTracer& tracer,
    AimingSession& aiming) {
    std::vector<FieldSpot> out;
    const auto waves = wavelengths_or_default(system);
    const auto fields = fields_or_default(system);
    const bool has_primary_wave = std::any_of(
        waves.begin(), waves.end(), [](const Wavelength& wave) {
            return wave.primary && wave.weight > 0.0;
        });
    const bool collect_illumination_proxy =
        relative_illumination_proxy_requested(config);
    // CP reads the configured chief anchor directly from its field/wavelength
    // model, without using the per-pupil cache. Both consumers use the same
    // system and original FVC/FVD here, so reuse that model within this score.
    // Other modes generate the chief at pupil index zero; keep their cache
    // isolated from image-pupil samples, where index zero can mean another ray.
    AimingSession isolated_chief_aiming(system);
    AimingSession& chief_aiming =
        system.ray_aiming.mode == RayAimingMode::CalibratedParaxial
            ? aiming
            : isolated_chief_aiming;
    for (std::size_t fi = 0; fi < fields.size(); ++fi) {
        const auto& field = fields[fi];
        const auto pupils = merit_field_pupils(
            system, config, field, MeritPupilMetric::Spot);
        const std::vector<unsigned char> order_history_mask =
            pupil_order_history_mask(field, config, pupils);
        const bool collect_pupil_order_history =
            pupil_order_enabled_for_field(field, config);
        std::vector<Ray> rays;
        std::vector<StructuralRaySample> center_samples;
        SpotStats structural_stats;
        bool chief_ok = true;
        bool checked_chief = false;
        double chief_progress_sum = 0.0;
        double chief_weight_sum = 0.0;
        double worst_wavelength_rms = 0.0;
        double best_wavelength_rms = std::numeric_limits<double>::infinity();
        std::size_t active_wavelength_count = 0;
        std::size_t valid_wavelength_count = 0;
        double illumination_proxy_area =
            std::numeric_limits<double>::quiet_NaN();
        const bool need_structure = config.reject_ray_path_structural_failures;
        const bool center_field = !field_is_off_axis(field);
        const bool off_axis_field = !center_field;
        const std::size_t first_surface = system.first_optical_surface();
        const double image_z = system.surfaces.empty()
                                   ? std::numeric_limits<double>::quiet_NaN()
                                   : surface_vertex_z(system, system.surfaces.size() - 1);
        double last_optical_z = std::numeric_limits<double>::quiet_NaN();
        for (std::size_t si = system.surfaces.size(); si-- > first_surface;) {
            if (system.surfaces[si].kind != SurfaceKind::Image &&
                system.surfaces[si].kind != SurfaceKind::Object) {
                last_optical_z = surface_vertex_z(system, si);
                break;
            }
        }
        for (std::size_t wi = 0; wi < waves.size(); ++wi) {
            const auto& wave = waves[wi];
            if (wave.weight <= 0.0) {
                continue;
            }
            ++active_wavelength_count;
            const bool need_chief =
                config.require_chief_ray ||
                (need_structure && off_axis_field && first_surface < system.surfaces.size());
            if (need_chief) {
                const Field& chief_field = field;
                const PupilPoint chief{0.0, 0.0, 1.0};
                Ray chief_ray;
                if (ray_aiming_uses_session(system.ray_aiming.mode)) {
                    chief_ray = chief_aiming.generate_chief(
                        fi, wi, chief_field, wave);
                } else {
                    chief_ray = tracer.generate_ray(chief_field, chief, wave.value_um);
                }
                const Ray initial_chief = chief_ray;
                std::vector<TracePoint> chief_history;
                tracer.trace_ray(chief_ray, need_structure && off_axis_field ? &chief_history : nullptr);
                chief_ray.intensity *= wave.weight;
                chief_ray.nominal_weight *= wave.weight;
                if (config.require_chief_ray) {
                    checked_chief = true;
                    chief_progress_sum += wave.weight * ray_trace_progress_fraction(chief_ray, system.surfaces.size());
                    chief_weight_sum += wave.weight;
                    if (!ray_is_usable_for_spot(chief_ray, config) &&
                        !normal_secondary_aperture_blockage(chief_ray)) {
                        chief_ok = false;
                    }
                }
                if (need_structure && off_axis_field) {
                    apply_off_axis_chief_structure_metrics(
                        field,
                        initial_chief,
                        chief_history,
                        first_surface,
                        config,
                        structural_stats);
                }
            }
            const bool collect_history = need_structure && center_field;
            const bool primary_for_pupil_order =
                collect_pupil_order_history &&
                (wave.primary || (!has_primary_wave && wi == 0));
            SymmetricTraceBatch wave_batch = trace_pupil_batch(
                system,
                config,
                tracer,
                aiming,
                fi,
                wi,
                field,
                wave,
                pupils,
                collect_history,
                0,
                primary_for_pupil_order ? &order_history_mask : nullptr);
            if (primary_for_pupil_order) {
                // Exact field-meridional boundaries and the retained pupil
                // center are structural Guard samples.  Keep them outside the
                // RMS quadrature, but merge their already-small trace set into
                // the ordered per-surface history so Py=+/-1 participates in
                // the fold contract.
                std::vector<unsigned char> combined_mask = order_history_mask;
                std::vector<std::vector<TracePoint>> combined_histories =
                    std::move(wave_batch.histories);
                // A quadrature with no meridional nodes has no stored base
                // histories. Keep its empty slots aligned with the mask before
                // appending the exact boundary and center histories.
                combined_histories.resize(pupils.size());
                const auto exact_order_pupils =
                    missing_exact_pupil_order_pupils(field, pupils);
                if (!exact_order_pupils.empty()) {
                    std::vector<unsigned char> exact_mask(
                        exact_order_pupils.size(), 1);
                    constexpr std::size_t kPupilOrderGuardIndexOffset = 3000000;
                    SymmetricTraceBatch exact_batch = trace_pupil_batch(
                        system,
                        config,
                        tracer,
                        aiming,
                        fi,
                        wi,
                        field,
                        wave,
                        exact_order_pupils,
                        false,
                        kPupilOrderGuardIndexOffset,
                        &exact_mask);
                    combined_mask.insert(
                        combined_mask.end(), exact_mask.begin(), exact_mask.end());
                    combined_histories.insert(
                        combined_histories.end(),
                        std::make_move_iterator(exact_batch.histories.begin()),
                        std::make_move_iterator(exact_batch.histories.end()));
                }
                apply_pupil_order_metrics(
                    system,
                    field,
                    config,
                    combined_mask,
                    combined_histories,
                    structural_stats);
            }
            const SpotStats wave_stats = compute_spot_stats(
                wave_batch.rays,
                config,
                system.surfaces.size(),
                system.ray_aiming.mode,
                static_cast<int>(tracer.stop_surface_index()));
            if (collect_illumination_proxy &&
                !std::isfinite(illumination_proxy_area) &&
                (wave.primary || (!has_primary_wave && wi == 0))) {
                illumination_proxy_area = sparse_projected_illumination_area(
                    field,
                    pupils,
                    wave_batch.rays,
                    system.ray_aiming.mode,
                    static_cast<int>(tracer.stop_surface_index()));
            }
            if (std::isfinite(wave_stats.rms_radius)) {
                worst_wavelength_rms =
                    std::max(worst_wavelength_rms, wave_stats.rms_radius);
                best_wavelength_rms =
                    std::min(best_wavelength_rms, wave_stats.rms_radius);
                ++valid_wavelength_count;
            }
            for (std::size_t pi = 0; pi < wave_batch.rays.size(); ++pi) {
                Ray ray = wave_batch.rays[pi];
                if (collect_history) {
                    StructuralRaySample sample;
                    sample.pupil = pupils[pi];
                    sample.history = std::move(wave_batch.histories[pi]);
                    sample.usable =
                        ray.valid &&
                        std::isfinite(ray.p.x) &&
                        std::isfinite(ray.p.y) &&
                        std::isfinite(ray.p.z);
                    center_samples.push_back(std::move(sample));
                }
                ray.intensity *= wave.weight;
                ray.nominal_weight *= wave.weight;
                rays.push_back(ray);
            }
        }
        if (need_structure && center_field) {
            apply_center_field_structure_metrics(
                center_samples,
                config,
                image_z,
                last_optical_z,
                structural_stats);
        }
        SpotStats stats = compute_spot_stats(
            rays,
            config,
            system.surfaces.size(),
            system.ray_aiming.mode,
            static_cast<int>(tracer.stop_surface_index()));
        stats.chief_ok = !checked_chief || chief_ok;
        stats.chief_progress = chief_weight_sum > 0.0 ? chief_progress_sum / chief_weight_sum : 1.0;
        stats.axis_cross_count = structural_stats.axis_cross_count;
        stats.axis_cross_excess = structural_stats.axis_cross_excess;
        stats.center_early_focus = structural_stats.center_early_focus;
        stats.chief_entry_side_failed = structural_stats.chief_entry_side_failed;
        stats.structural_failure = structural_stats.structural_failure;
        stats.min_pupil_order_jacobian =
            structural_stats.min_pupil_order_jacobian;
        stats.pupil_order_worst_surface_progress =
            structural_stats.pupil_order_worst_surface_progress;
        stats.pupil_order_prior_violation =
            structural_stats.pupil_order_prior_violation;
        stats.pupil_order_fold = structural_stats.pupil_order_fold;
        stats.pupil_order_hard_fold = structural_stats.pupil_order_hard_fold;
        update_spot_single_ray_guard_from_edges(
            system,
            config,
            tracer,
            aiming,
            fi,
            field,
            waves,
            pupils,
            rays,
            stats);
        const bool complete_spectral_stats =
            active_wavelength_count > 0 &&
            valid_wavelength_count == active_wavelength_count;
        const double worst = complete_spectral_stats
                                 ? worst_wavelength_rms
                                 : std::numeric_limits<double>::quiet_NaN();
        const double spread = complete_spectral_stats
                                  ? std::max(0.0, worst_wavelength_rms - best_wavelength_rms)
                                  : std::numeric_limits<double>::quiet_NaN();
        out.push_back({
            field,
            std::numeric_limits<double>::quiet_NaN(),
            stats,
            illumination_proxy_area,
            std::numeric_limits<double>::quiet_NaN(),
            worst,
            spread});
    }
    if (collect_illumination_proxy && !out.empty()) {
        const auto center_it = std::min_element(
            out.begin(), out.end(), [](const FieldSpot& a, const FieldSpot& b) {
                return std::hypot(a.field.hx, a.field.hy) <
                       std::hypot(b.field.hx, b.field.hy);
            });
        const double reference_area = center_it->relative_illumination_proxy_area;
        if (std::isfinite(reference_area) && reference_area > 0.0) {
            for (FieldSpot& spot : out) {
                if (std::isfinite(spot.relative_illumination_proxy_area) &&
                    spot.relative_illumination_proxy_area >= 0.0) {
                    spot.relative_illumination_proxy =
                        spot.relative_illumination_proxy_area / reference_area;
                }
            }
        }
    }
    return out;
}

std::vector<FieldSpot> polychromatic_field_spot_stats(const LensSystem& system, const MeritConfig& config) {
    SequentialRayTracer tracer(system);
    AimingSession aiming(system);
    return polychromatic_field_spot_stats_with_context(system, config, tracer, aiming);
}

}  // namespace optiland
