/*
 * Computes chief-ray image references, distortion, CRA, lateral color and path metrics.
 * Merit measurement can supply shared tracing/aiming sessions to reuse reference rays.
 * Field/wavelength order, chief completeness and image-height sign checks determine
 * which reference measurements are valid for subsequent merit contributions.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

double field_tangent_radius(const LensSystem& system, const Field& field) {
    return std::tan(field_angle_radial_rad(system, field));
}

double field_angle_rad(const LensSystem& system, const Field& field) {
    return field_angle_radial_rad(system, field);
}

double paraxial_reference_focal_length(const LensSystem& system) {
    const double max_field = std::abs(system.max_field_deg);
    if (max_field <= kEps) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double sample_deg = std::min(0.1, max_field);
    if (sample_deg <= kEps) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    Field field;
    field.hy = sample_deg / max_field;
    const PupilPoint chief{0.0, 0.0};
    SequentialRayTracer tracer(system);
    Ray ray = tracer.generate_ray(field, chief, system.primary_wavelength());
    tracer.trace_ray(ray, nullptr);
    if (!ray.valid || !std::isfinite(ray.p.y)) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const double tangent = std::tan(deg_to_rad(sample_deg));
    if (std::abs(tangent) <= kEps) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return std::abs(ray.p.y / tangent);
}

bool image_height_side_flipped(double ideal_height, double spot_height) {
    if (!std::isfinite(ideal_height) || !std::isfinite(spot_height)) {
        return false;
    }
    const double ideal_scale = std::abs(ideal_height);
    if (ideal_scale <= 1.0e-12) {
        return false;
    }
    return ideal_height * spot_height < -1.0e-9 * ideal_scale * ideal_scale;
}

ChiefRayMetrics chief_ray_metrics_with_context(
    const LensSystem& system,
    const MeritConfig& config,
    SequentialRayTracer& tracer,
    AimingSession& aiming) {
    ChiefRayMetrics metrics;
    metrics.distortion_model = effective_distortion_model_name(system, config);
    // One configured pupil-center chief for every chief-referenced metric.
    // Never clear FVC/FVD or require an unvignetted STOP-center principal ray.
    const auto fields = fields_or_default(system);
    const auto waves = wavelengths_or_default(system);
    if (fields.empty() || waves.empty()) {
        return metrics;
    }

    const bool use_f_theta = distortion_uses_f_theta(system, config);
    double reference_focal_length = paraxial_reference_focal_length(system);
    if (!std::isfinite(reference_focal_length) || reference_focal_length <= kEps) {
        reference_focal_length = std::abs(estimate_effective_focal_length(system));
    }

    const PupilPoint chief{0.0, 0.0};
    const double primary_wave = system.primary_wavelength();
    std::size_t primary_wave_index = 0;
    bool primary_wave_found = false;
    for (std::size_t wi = 0; wi < waves.size(); ++wi) {
        if (std::abs(waves[wi].value_um - primary_wave) <= 1.0e-12) {
            primary_wave_index = wi;
            primary_wave_found = true;
            break;
        }
    }
    const Wavelength reference_wave =
        primary_wave_found ? waves[primary_wave_index] : waves[0];
    const double reference_wave_um = reference_wave.value_um;
    double max_distortion = 0.0;
    double max_chief_ray_angle = 0.0;
    double max_lateral_color = 0.0;
    double chief_path_bend_sum = 0.0;
    double chief_path_bend_off_axis_sum = 0.0;
    double chief_path_bend_max = 0.0;
    double chief_path_bend_max_step = 0.0;
    double chief_path_excess_sum = 0.0;
    double chief_path_excess_max = 0.0;
    int chief_path_count = 0;
    int chief_path_off_axis_count = 0;
    bool have_distortion = false;
    bool have_chief_ray_angle = false;
    bool have_lateral_color = false;
    bool have_lateral_reference = false;
    bool have_chief_path = false;
    int distortion_sign_flip_fields = 0;
    bool chiefs_complete = true;
    bool references_complete = true;

    for (std::size_t fi = 0; fi < fields.size(); ++fi) {
        const auto& field = fields[fi];
        const double angle_rad = field_angle_rad(system, field);
        const double tan_radius = std::tan(angle_rad);
        const auto [unit_x, unit_y] = field_transverse_unit(field);
        bool field_sign_flip = false;
        const bool off_axis_field = field_is_off_axis(field);

        double reference_x = std::numeric_limits<double>::quiet_NaN();
        double reference_y = std::numeric_limits<double>::quiet_NaN();
        Ray reference_ray = ray_aiming_uses_session(system.ray_aiming.mode)
                                ? aiming.generate_chief(
                                      fi,
                                      primary_wave_index,
                                      field,
                                      reference_wave)
                                : tracer.generate_ray(field, chief, reference_wave_um);
        tracer.trace_ray(reference_ray, nullptr);
        const bool have_reference =
            reference_ray.valid &&
            std::isfinite(reference_ray.p.x) &&
            std::isfinite(reference_ray.p.y);
        references_complete = references_complete && have_reference;
        if (have_reference) {
            reference_x = reference_ray.p.x;
            reference_y = reference_ray.p.y;
            have_lateral_reference = true;
        }

        for (std::size_t wi = 0; wi < waves.size(); ++wi) {
            const auto& wave = waves[wi];
            if (wave.weight <= 0.0) {
                continue;
            }
            Ray ray = ray_aiming_uses_session(system.ray_aiming.mode)
                          ? aiming.generate_chief(fi, wi, field, wave)
                          : tracer.generate_ray(field, chief, wave.value_um);
            const double input_component = ray.d.x * unit_x + ray.d.y * unit_y;
            std::vector<TracePoint> history;
            tracer.trace_ray(ray, &history);
            if (!ray.valid) {
                chiefs_complete = false;
                continue;
            }

            double path_bend = 0.0;
            double path_max_step = 0.0;
            int path_steps = 0;
            for (std::size_t hi = 1; hi < history.size(); ++hi) {
                const double step =
                    direction_angle_deg(history[hi - 1], history[hi]);
                if (!std::isfinite(step)) {
                    continue;
                }
                path_bend += std::abs(step);
                path_max_step = std::max(path_max_step, std::abs(step));
                ++path_steps;
            }
            if (path_steps > 0 && std::isfinite(path_bend)) {
                const double field_angle_deg = rad_to_deg(std::abs(angle_rad));
                const double bend_budget = chief_path_bend_budget_deg(field_angle_deg);
                const double excess =
                    std::isfinite(bend_budget) && bend_budget > 1.0e-12
                        ? std::max(0.0, path_bend / bend_budget - 1.0)
                        : std::numeric_limits<double>::quiet_NaN();
                chief_path_bend_sum += path_bend;
                chief_path_bend_max = std::max(chief_path_bend_max, path_bend);
                chief_path_bend_max_step =
                    std::max(chief_path_bend_max_step, path_max_step);
                if (std::isfinite(excess)) {
                    chief_path_excess_sum += excess;
                    chief_path_excess_max = std::max(chief_path_excess_max, excess);
                }
                ++chief_path_count;
                if (off_axis_field) {
                    chief_path_bend_off_axis_sum += path_bend;
                    ++chief_path_off_axis_count;
                }
                have_chief_path = true;
            }

            // CRA shares exactly the ray used by distortion, color and path metrics.
            if (ray.valid) {
                const double transverse_slope =
                    std::hypot(ray.d.x, ray.d.y);
                const double chief_angle = rad_to_deg(std::atan2(
                    transverse_slope, std::abs(ray.d.z)));
                if (std::isfinite(chief_angle)) {
                    max_chief_ray_angle = std::max(max_chief_ray_angle, chief_angle);
                    have_chief_ray_angle = true;
                }
            }

            if (have_reference &&
                std::abs(wave.value_um - reference_wave_um) > 1.0e-12 &&
                std::isfinite(ray.p.x) &&
                std::isfinite(ray.p.y)) {
                const double color = std::hypot(ray.p.x - reference_x, ray.p.y - reference_y);
                if (std::isfinite(color)) {
                    max_lateral_color = std::max(max_lateral_color, color);
                    have_lateral_color = true;
                }
            }

            const double ideal_field_radius = use_f_theta ? angle_rad : tan_radius;
            if (ideal_field_radius <= 1.0e-12 ||
                !std::isfinite(reference_focal_length) ||
                reference_focal_length <= kEps) {
                continue;
            }
            const double ideal_magnitude = reference_focal_length * ideal_field_radius;
            if (!std::isfinite(ideal_magnitude) || ideal_magnitude <= 1.0e-12) {
                continue;
            }
            const double sign = input_component < 0.0 ? -1.0 : 1.0;
            const double ideal_height = sign * ideal_magnitude;
            const double spot_height = ray.p.x * unit_x + ray.p.y * unit_y;
            if (image_height_side_flipped(ideal_height, spot_height)) {
                field_sign_flip = true;
            }
            const double distortion = (spot_height - ideal_height) / std::abs(ideal_height);
            if (std::isfinite(distortion)) {
                max_distortion = std::max(max_distortion, std::abs(distortion));
                have_distortion = true;
            }
        }
        if (field_sign_flip) {
            ++distortion_sign_flip_fields;
        }
    }

    if (have_distortion) {
        metrics.max_distortion = max_distortion;
    } else if (have_chief_ray_angle) {
        metrics.max_distortion = 0.0;
    }
    if (have_chief_ray_angle) {
        metrics.max_chief_ray_angle = max_chief_ray_angle;
    }
    if (have_chief_path && chief_path_count > 0) {
        metrics.chief_ray_path_bend_mean_deg =
            chief_path_bend_sum / static_cast<double>(chief_path_count);
        metrics.chief_ray_path_bend_max_deg = chief_path_bend_max;
        metrics.chief_ray_path_bend_max_step_deg = chief_path_bend_max_step;
        metrics.chief_ray_path_excess_mean =
            chief_path_excess_sum / static_cast<double>(chief_path_count);
        metrics.chief_ray_path_excess_max = chief_path_excess_max;
        if (chief_path_off_axis_count > 0) {
            metrics.chief_ray_path_bend_mean_off_axis_deg =
                chief_path_bend_off_axis_sum /
                static_cast<double>(chief_path_off_axis_count);
        }
    }
    if (have_lateral_color) {
        metrics.lateral_color = max_lateral_color;
    } else if (have_lateral_reference) {
        metrics.lateral_color = 0.0;
    }
    metrics.distortion_sign_flip_fields = distortion_sign_flip_fields;
    // An omitted positive-weight wavelength must not masquerade as a small
    // worst-case aberration (especially zero lateral color).
    if (!chiefs_complete) {
        metrics.max_distortion = std::numeric_limits<double>::quiet_NaN();
        metrics.max_chief_ray_angle = std::numeric_limits<double>::quiet_NaN();
    }
    if (!chiefs_complete || !references_complete) {
        metrics.lateral_color = std::numeric_limits<double>::quiet_NaN();
    }
    return metrics;
}

ChiefRayMetrics chief_ray_metrics(const LensSystem& system) {
    MeritConfig config;
    return chief_ray_metrics(system, config);
}

ChiefRayMetrics chief_ray_metrics(const LensSystem& system, const MeritConfig& config) {
    SequentialRayTracer tracer(system);
    AimingSession aiming(system);
    return chief_ray_metrics_with_context(system, config, tracer, aiming);
}

}  // namespace optiland
