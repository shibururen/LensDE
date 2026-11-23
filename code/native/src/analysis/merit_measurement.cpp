/*
 * Collects first-order, spot, wavefront, chief-ray and trace-health score measurements.
 * This is the first MeritEvaluation phase and may share tracer/aiming model contexts.
 * Keep field/wavelength weighting, validity flags and supervised illumination indices
 * aligned before later phases decide failures and accumulate merit contributions.
 */

#include "merit_evaluation.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
void MeritEvaluation::measure_image_quality() {

    score.merit_model = config.merit_model;
    score.distortion_model = effective_distortion_model_name(system, config);
    soft_trace = soft_trace_policy(config);
    score.efl = estimate_effective_focal_length(system);
    score.paraxial_axial_color_focus_shift_mm =
        paraxial_axial_color_focus_shift(system);
    score.ttl = system.total_track();
    score.bfl = back_focal_length(system);
    const WorkingFNumberMetrics working_f_number =
        working_f_number_metrics(system);
    score.working_f_number = working_f_number.working_f_number;
    score.image_space_numerical_aperture =
        working_f_number.image_space_numerical_aperture;
    score.working_f_number_valid_marginal_rays =
        static_cast<int>(working_f_number.valid_marginal_rays);
    const bool reuse_aiming_models =
        ray_aiming_model_reuse_safe(system.ray_aiming.mode);
    need_wavefront = merit_needs_wavefront(config);
    SequentialRayTracer shared_tracer(system);
    AimingSession shared_aiming(system);
    field_spots = reuse_aiming_models
                                 ? polychromatic_field_spot_stats_with_context(
                                       system,
                                       config,
                                       shared_tracer,
                                       shared_aiming)
                                 : polychromatic_field_spot_stats(system, config);
    field_wavefronts =
        need_wavefront
            ? (reuse_aiming_models
                   ? wavefront_stats_with_context(system, config, shared_tracer, shared_aiming)
                   : wavefront_stats(system, config))
            : std::vector<FieldWavefront>{};
    double spot_sum = 0.0;
    double spot_count = 0.0;
    double max_spot = 0.0;
    double worst_wavelength_spot_sum = 0.0;
    double worst_wavelength_spot_count = 0.0;
    double max_worst_wavelength_spot = 0.0;
    double spectral_spot_spread_sum = 0.0;
    double spectral_spot_spread_count = 0.0;
    double max_spectral_spot_spread = 0.0;
    double max_single_ray_spot = 0.0;
    double max_single_ray_spot_ratio = 0.0;
    bool have_single_ray_spot = false;
    double wavefront_sum = 0.0;
    double wavefront_count = 0.0;
    double max_wavefront = 0.0;
    double max_single_ray_wavefront = 0.0;
    double max_single_ray_wavefront_ratio = 0.0;
    bool have_single_ray_wavefront = false;
    spot_failed = field_spots.empty();
    wavefront_failed = need_wavefront && field_wavefronts.empty();
    double coverage_total = 0.0;
    double coverage_valid = 0.0;
    double progress_total = 0.0;
    double progress_weight = 0.0;
    double min_field_coverage = std::numeric_limits<double>::infinity();
    double min_trace_progress = std::numeric_limits<double>::infinity();
    double min_chief_progress = std::numeric_limits<double>::infinity();
    illumination_fields = relative_illumination_supervised_fields(system, config);
    if (minimum_relative_illumination_enabled(config)) {
        score.relative_illumination_floor_by_field.assign(
            field_spots.size(),
            config.minimum_relative_illumination);
        if (!illumination_fields.empty()) {
            for (std::size_t i = 0; i < field_spots.size(); ++i) {
                if (std::find(illumination_fields.begin(), illumination_fields.end(), i) == illumination_fields.end()) {
                    score.relative_illumination_floor_by_field[i] =
                        std::numeric_limits<double>::quiet_NaN();
                }
            }
        }
    }
    for (const auto& spot : field_spots) {
        score.relative_illumination_proxy_by_field.push_back(
            spot.relative_illumination_proxy);
        if (std::isfinite(spot.relative_illumination_proxy)) {
            if (!std::isfinite(score.min_relative_illumination_proxy)) {
                score.min_relative_illumination_proxy =
                    spot.relative_illumination_proxy;
            } else {
                score.min_relative_illumination_proxy = std::min(
                    score.min_relative_illumination_proxy,
                    spot.relative_illumination_proxy);
            }
        }
        coverage_total += spot.stats.total_weight;
        coverage_valid += spot.stats.valid_weight;
        progress_total += spot.stats.trace_progress * spot.stats.total_weight;
        progress_weight += spot.stats.total_weight;
        min_field_coverage = std::min(min_field_coverage, spot.stats.coverage);
        min_trace_progress = std::min(min_trace_progress, spot.stats.trace_progress);
        min_chief_progress = std::min(min_chief_progress, spot.stats.chief_progress);
        score.failed_ray_weight += spot.stats.failed_weight;
        if (!spot.stats.chief_ok) {
            score.chief_ray_ok = false;
            ++score.chief_ray_failed_fields;
        }
        if (field_is_off_axis(spot.field) && spot.stats.valid_rays == 0) {
            ++score.zero_coverage_off_axis_fields;
        }
        score.max_axis_cross_count =
            std::max(score.max_axis_cross_count, spot.stats.axis_cross_count);
        if (spot.stats.axis_cross_excess > 0) {
            ++score.ray_path_axis_cross_fields;
        }
        if (spot.stats.center_early_focus != 0) {
            ++score.center_early_focus_fields;
        }
        if (spot.stats.chief_entry_side_failed != 0) {
            ++score.chief_entry_side_failed_fields;
        }
        if (spot.stats.structural_failure != 0) {
            ++score.ray_path_structural_failure_fields;
        }
        if (std::isfinite(spot.stats.min_pupil_order_jacobian)) {
            if (!std::isfinite(score.min_pupil_order_jacobian) ||
                spot.stats.min_pupil_order_jacobian <
                    score.min_pupil_order_jacobian) {
                score.min_pupil_order_jacobian =
                    spot.stats.min_pupil_order_jacobian;
                score.pupil_order_worst_surface_progress =
                    spot.stats.pupil_order_worst_surface_progress;
            }
        }
        if (spot.stats.pupil_order_fold != 0) {
            ++score.pupil_order_fold_fields;
        }
        if (spot.stats.pupil_order_hard_fold != 0) {
            ++score.pupil_order_hard_fold_fields;
        }
        const double pupil_order_violation =
            std::max(0.0, spot.stats.pupil_order_prior_violation);
        score.pupil_order_prior_penalty +=
            std::max(0.0, config.pupil_order_prior_weight) *
            pupil_order_violation * pupil_order_violation;
        const bool spot_usable =
            std::isfinite(spot.stats.rms_radius) &&
            (!soft_trace || spot.stats.coverage >= config.min_coverage_for_image_merit);
        if (!spot_usable) {
            spot_failed = true;
            continue;
        }
        spot_sum += spot.stats.rms_radius;
        spot_count += 1.0;
        max_spot = std::max(max_spot, spot.stats.rms_radius);
        if (std::isfinite(spot.worst_wavelength_rms_radius)) {
            worst_wavelength_spot_sum += spot.worst_wavelength_rms_radius;
            worst_wavelength_spot_count += 1.0;
            max_worst_wavelength_spot = std::max(
                max_worst_wavelength_spot,
                spot.worst_wavelength_rms_radius);
        }
        if (std::isfinite(spot.spectral_rms_spread)) {
            spectral_spot_spread_sum += spot.spectral_rms_spread;
            spectral_spot_spread_count += 1.0;
            max_spectral_spot_spread = std::max(
                max_spectral_spot_spread,
                spot.spectral_rms_spread);
        }
        if (std::isfinite(spot.stats.single_ray_max_radius)) {
            max_single_ray_spot =
                std::max(max_single_ray_spot, spot.stats.single_ray_max_radius);
            const double denom = std::max(1.0e-12, std::abs(spot.stats.rms_radius));
            max_single_ray_spot_ratio =
                std::max(max_single_ray_spot_ratio, spot.stats.single_ray_max_radius / denom);
            have_single_ray_spot = true;
        }
    }
    score.trace_coverage = coverage_total > 0.0 ? coverage_valid / coverage_total : 0.0;
    score.min_field_coverage = std::isfinite(min_field_coverage) ? min_field_coverage : 0.0;
    score.trace_progress = progress_weight > 0.0 ? progress_total / progress_weight : 0.0;
    score.min_trace_progress = std::isfinite(min_trace_progress) ? min_trace_progress : 0.0;
    score.min_chief_progress = std::isfinite(min_chief_progress) ? min_chief_progress : 1.0;
    score.mean_spot = spot_count > 0.0 ? spot_sum / spot_count : std::numeric_limits<double>::quiet_NaN();
    score.max_spot = max_spot;
    score.mean_worst_wavelength_spot =
        worst_wavelength_spot_count > 0.0
            ? worst_wavelength_spot_sum / worst_wavelength_spot_count
            : std::numeric_limits<double>::quiet_NaN();
    score.max_worst_wavelength_spot =
        worst_wavelength_spot_count > 0.0
            ? max_worst_wavelength_spot
            : std::numeric_limits<double>::quiet_NaN();
    score.mean_spectral_spot_spread =
        spectral_spot_spread_count > 0.0
            ? spectral_spot_spread_sum / spectral_spot_spread_count
            : std::numeric_limits<double>::quiet_NaN();
    score.max_spectral_spot_spread =
        spectral_spot_spread_count > 0.0
            ? max_spectral_spot_spread
            : std::numeric_limits<double>::quiet_NaN();
    score.max_single_ray_spot =
        have_single_ray_spot ? max_single_ray_spot : std::numeric_limits<double>::quiet_NaN();
    score.max_single_ray_spot_ratio =
        have_single_ray_spot ? max_single_ray_spot_ratio : std::numeric_limits<double>::quiet_NaN();
    if (need_wavefront) {
        for (const auto& wavefront : field_wavefronts) {
            const bool wavefront_usable =
                std::isfinite(wavefront.stats.rms_waves) &&
                (!soft_trace || wavefront.stats.coverage >= config.min_coverage_for_image_merit);
            if (!wavefront.stats.chief_ok || !wavefront_usable) {
                wavefront_failed = true;
                continue;
            }
            wavefront_sum += wavefront.stats.rms_waves;
            wavefront_count += 1.0;
            max_wavefront = std::max(max_wavefront, wavefront.stats.rms_waves);
            if (std::isfinite(wavefront.stats.single_ray_max_abs_waves)) {
                max_single_ray_wavefront = std::max(
                    max_single_ray_wavefront,
                    wavefront.stats.single_ray_max_abs_waves);
                const double denom = std::max(1.0e-12, std::abs(wavefront.stats.rms_waves));
                max_single_ray_wavefront_ratio = std::max(
                    max_single_ray_wavefront_ratio,
                    wavefront.stats.single_ray_max_abs_waves / denom);
                have_single_ray_wavefront = true;
            }
        }
        score.mean_wavefront_rms =
            wavefront_count > 0.0 ? wavefront_sum / wavefront_count : std::numeric_limits<double>::quiet_NaN();
        score.max_wavefront_rms = max_wavefront;
        score.max_single_ray_wavefront =
            have_single_ray_wavefront ? max_single_ray_wavefront : std::numeric_limits<double>::quiet_NaN();
        score.max_single_ray_wavefront_ratio =
            have_single_ray_wavefront ? max_single_ray_wavefront_ratio : std::numeric_limits<double>::quiet_NaN();
    }
    if (merit_needs_chief_ray_metrics(config)) {
        // Configured-pupil chief metrics use an independent aiming session. The shared
        // session may already hold image-quality sampling entries under the
        // same field/wave keys.
        const ChiefRayMetrics chief_metrics = chief_ray_metrics(system, config);
        score.max_distortion = chief_metrics.max_distortion;
        score.distortion_model = chief_metrics.distortion_model;
        score.max_chief_ray_angle = chief_metrics.max_chief_ray_angle;
        score.chief_ray_path_bend_mean_deg =
            chief_metrics.chief_ray_path_bend_mean_deg;
        score.chief_ray_path_bend_mean_off_axis_deg =
            chief_metrics.chief_ray_path_bend_mean_off_axis_deg;
        score.chief_ray_path_bend_max_deg =
            chief_metrics.chief_ray_path_bend_max_deg;
        score.chief_ray_path_bend_max_step_deg =
            chief_metrics.chief_ray_path_bend_max_step_deg;
        score.chief_ray_path_excess_mean =
            chief_metrics.chief_ray_path_excess_mean;
        score.chief_ray_path_excess_max =
            chief_metrics.chief_ray_path_excess_max;
        score.lateral_color = chief_metrics.lateral_color;
        score.distortion_sign_flip_fields = chief_metrics.distortion_sign_flip_fields;
    }
    score.min_edge = min_edge_thickness(system);
    const FiniteRayAberrationMetrics finite_metrics =
        reuse_aiming_models
            ? finite_ray_aberration_metrics_with_context(system, config, shared_tracer, shared_aiming)
            : finite_ray_aberration_metrics(system, config);
    score.finite_ray_field_focus_spread_mm = finite_metrics.field_focus_spread_mm;
    score.finite_ray_astig_focus_gap_mm = finite_metrics.astig_focus_gap_mm;
    score.finite_ray_axial_color_focus_shift_mm = finite_metrics.axial_color_focus_shift_mm;
    score.finite_ray_lateral_color_mm = finite_metrics.lateral_color_mm;
    score.finite_ray_spherical_focus_spread_mm = finite_metrics.spherical_focus_spread_mm;
    score.finite_ray_coma_tangential_mm = finite_metrics.coma_tangential_mm;
    score.finite_ray_valid_fraction = finite_metrics.valid_fraction;
    score.finite_ray_metric_count = finite_metrics.metric_count;
    compute_topology_potential_v2(score, config);
    compute_topology_potential_v3(score, config);
}
}  // namespace analysis_detail

}  // namespace optiland
