/*
 * Exposes wavefront statistics and mean image-quality reductions to scoring/reporting.
 * Merit measurement can reuse supplied tracer/aiming contexts through these entry points.
 * Field/wavelength traversal, weights and finite-result selection remain consistent
 * with the per-field accumulation and the native wavefront reference convention.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

std::vector<FieldWavefront> wavefront_stats_with_context(
    const LensSystem& system,
    const MeritConfig& config,
    SequentialRayTracer& tracer,
    AimingSession& aiming) {
    std::vector<FieldWavefront> out;
    const auto fields = fields_or_default(system);
    const auto waves = wavelengths_or_default(system);
    // Keep one configured-chief session across fields. This preserves the
    // field-continuation seed family while remaining isolated from sampled
    // image-pupil rays, whose cache keys intentionally share indices.
    AimingSession chief_aiming(system);
    for (std::size_t fi = 0; fi < fields.size(); ++fi) {
        const auto pupils = merit_field_pupils(
            system, config, fields[fi], MeritPupilMetric::Wavefront);
        WavefrontStats stats = compute_wavefront_stats_for_field(
            system,
            config,
            tracer,
            aiming,
            chief_aiming,
            fi,
            fields[fi],
            waves,
            pupils);
        out.push_back({
            fields[fi],
            std::numeric_limits<double>::quiet_NaN(),
            fi,
            0,
            stats});
    }
    return out;
}

std::vector<FieldWavefront> wavefront_stats(const LensSystem& system, const MeritConfig& config) {
    SequentialRayTracer tracer(system);
    AimingSession aiming(system);
    return wavefront_stats_with_context(system, config, tracer, aiming);
}

double mean_rms_spot(const LensSystem& system, int rings, double* max_spot) {
    MeritConfig config;
    config.num_rays = rings;
    config.pupil_distribution = "hexapolar";
    return mean_rms_spot(system, config, max_spot);
}

double mean_rms_spot(const LensSystem& system, const MeritConfig& config, double* max_spot) {
    const auto spots = polychromatic_field_spot_stats(system, config);
    double sum = 0.0;
    double weight = 0.0;
    double local_max = 0.0;
    for (const auto& spot : spots) {
        if (std::isfinite(spot.stats.rms_radius)) {
            sum += spot.stats.rms_radius;
            weight += 1.0;
            local_max = std::max(local_max, spot.stats.rms_radius);
        }
    }
    if (max_spot) {
        *max_spot = local_max;
    }
    if (weight <= 0.0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return sum / weight;
}

double mean_rms_wavefront(const LensSystem& system, const MeritConfig& config, double* max_wavefront) {
    const auto fields = wavefront_stats(system, config);
    double sum = 0.0;
    double weight = 0.0;
    double local_max = 0.0;
    for (const auto& field : fields) {
        if (std::isfinite(field.stats.rms_waves)) {
            sum += field.stats.rms_waves;
            weight += 1.0;
            local_max = std::max(local_max, field.stats.rms_waves);
        }
    }
    if (max_wavefront) {
        *max_wavefront = local_max;
    }
    if (weight <= 0.0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return sum / weight;
}

}  // namespace optiland
