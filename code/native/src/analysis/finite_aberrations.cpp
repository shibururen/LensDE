/*
 * Measures finite-ray aberrations and focus shifts from traced field/pupil ray pairs.
 * Called by chief/image-quality analysis when those diagnostic metrics are needed.
 * Axis conventions, wavelength selection and failed-ray handling must preserve the
 * same physical interpretation as the paraxial and full-field measurements.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

double ray_axis_coord(const Ray& ray, double ax, double ay) {
    return ray.p.x * ax + ray.p.y * ay;
}

double ray_axis_slope(const Ray& ray, double ax, double ay) {
    if (std::abs(ray.d.z) <= 1.0e-12 || !std::isfinite(ray.d.z)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return (ray.d.x / ray.d.z) * ax + (ray.d.y / ray.d.z) * ay;
}

bool finite_image_ray(const Ray& ray) {
    return ray.valid &&
           std::isfinite(ray.p.x) &&
           std::isfinite(ray.p.y) &&
           std::isfinite(ray.d.x) &&
           std::isfinite(ray.d.y) &&
           std::isfinite(ray.d.z) &&
           std::abs(ray.d.z) > 1.0e-12;
}

double focus_shift_between(const Ray& ray, const Ray& chief, double ax, double ay) {
    const double q = ray_axis_coord(ray, ax, ay) - ray_axis_coord(chief, ax, ay);
    const double dq = ray_axis_slope(ray, ax, ay) - ray_axis_slope(chief, ax, ay);
    if (!std::isfinite(q) || !std::isfinite(dq) || std::abs(dq) <= 1.0e-12) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return -q / dq;
}

double finite_mean(std::initializer_list<double> values) {
    double sum = 0.0;
    int count = 0;
    for (double value : values) {
        if (std::isfinite(value)) {
            sum += value;
            ++count;
        }
    }
    return count > 0 ? sum / static_cast<double>(count)
                     : std::numeric_limits<double>::quiet_NaN();
}

double finite_spread(std::initializer_list<double> values) {
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    int count = 0;
    for (double value : values) {
        if (std::isfinite(value)) {
            lo = std::min(lo, value);
            hi = std::max(hi, value);
            ++count;
        }
    }
    return count >= 2 ? hi - lo : std::numeric_limits<double>::quiet_NaN();
}

Ray traced_finite_ray(
    SequentialRayTracer& tracer,
    AimingSession& aiming,
    const LensSystem& system,
    std::size_t field_index,
    std::size_t wave_index,
    std::size_t pupil_index,
    const Field& field,
    const Wavelength& wave,
    const PupilPoint& pupil) {
    Ray ray = ray_aiming_uses_session(system.ray_aiming.mode)
                  ? aiming.generate_for_trace(field_index, wave_index, pupil_index, field, wave, pupil)
                  : tracer.generate_ray(field, pupil, wave.value_um);
    Ray initial = ray;
    tracer.trace_ray(ray, nullptr);
    if (system.ray_aiming.mode == RayAimingMode::AdaptivePilotGrid) {
        aiming.finish_trace(field_index, wave_index, pupil_index, field, wave, pupil, initial, ray);
    }
    return ray;
}

FiniteRayFieldMetric finite_ray_field_metric(
    SequentialRayTracer& tracer,
    AimingSession& aiming,
    const LensSystem& system,
    std::size_t field_index,
    std::size_t wave_index,
    const Field& field,
    const Wavelength& wave,
    int* attempted,
    int* valid) {
    auto trace = [&](std::size_t pupil_index, const PupilPoint& pupil) {
        if (attempted) {
            ++(*attempted);
        }
        Ray ray = traced_finite_ray(
            tracer,
            aiming,
            system,
            field_index,
            wave_index,
            pupil_index,
            field,
            wave,
            pupil);
        if (finite_image_ray(ray) && valid) {
            ++(*valid);
        }
        return ray;
    };

    const Ray chief = trace(0, PupilPoint{0.0, 0.0, 1.0});
    const Ray t_pos = trace(1, PupilPoint{0.0, 1.0, 1.0});
    const Ray t_neg = trace(2, PupilPoint{0.0, -1.0, 1.0});
    const Ray s_pos = trace(3, PupilPoint{1.0, 0.0, 1.0});
    const Ray s_neg = trace(4, PupilPoint{-1.0, 0.0, 1.0});

    FiniteRayFieldMetric out;
    if (!finite_image_ray(chief)) {
        return out;
    }
    const double field_radius = std::hypot(field.hx, field.hy);
    const double ux = field_radius > 1.0e-12 ? field.hx / field_radius : 0.0;
    const double uy = field_radius > 1.0e-12 ? field.hy / field_radius : 1.0;
    const double vx = -uy;
    const double vy = ux;

    const double t_focus_pos = finite_image_ray(t_pos)
                                   ? focus_shift_between(t_pos, chief, ux, uy)
                                   : std::numeric_limits<double>::quiet_NaN();
    const double t_focus_neg = finite_image_ray(t_neg)
                                   ? focus_shift_between(t_neg, chief, ux, uy)
                                   : std::numeric_limits<double>::quiet_NaN();
    const double s_focus_pos = finite_image_ray(s_pos)
                                   ? focus_shift_between(s_pos, chief, vx, vy)
                                   : std::numeric_limits<double>::quiet_NaN();
    const double s_focus_neg = finite_image_ray(s_neg)
                                   ? focus_shift_between(s_neg, chief, vx, vy)
                                   : std::numeric_limits<double>::quiet_NaN();
    const double t_focus = finite_mean({t_focus_pos, t_focus_neg});
    const double s_focus = finite_mean({s_focus_pos, s_focus_neg});

    out.field_radius = field_radius;
    out.chief_t = ray_axis_coord(chief, ux, uy);
    out.chief_s = ray_axis_coord(chief, vx, vy);
    out.tangential_focus_shift_mm = t_focus;
    out.sagittal_focus_shift_mm = s_focus;
    out.best_focus_shift_mm = finite_mean({t_focus, s_focus});
    out.astig_focus_gap_mm = std::isfinite(t_focus) && std::isfinite(s_focus)
                                 ? std::abs(t_focus - s_focus)
                                 : std::numeric_limits<double>::quiet_NaN();
    out.spherical_focus_spread_mm =
        std::abs(finite_spread({t_focus_pos, t_focus_neg, s_focus_pos, s_focus_neg}));
    if (finite_image_ray(t_pos) && finite_image_ray(t_neg)) {
        const double tp = ray_axis_coord(t_pos, ux, uy) - ray_axis_coord(chief, ux, uy);
        const double tn = ray_axis_coord(t_neg, ux, uy) - ray_axis_coord(chief, ux, uy);
        if (std::isfinite(tp) && std::isfinite(tn)) {
            out.tangential_coma_mm = std::abs(0.5 * (tp + tn));
        }
    }
    out.valid = true;
    return out;
}

FiniteRayAberrationMetrics finite_ray_aberration_metrics_with_context(
    const LensSystem& system,
    const MeritConfig& config,
    SequentialRayTracer& tracer,
    AimingSession& aiming) {
    (void)config;
    FiniteRayAberrationMetrics out;
    const auto fields = fields_or_default(system);
    const auto waves = wavelengths_or_default(system);
    if (fields.empty() || waves.empty()) {
        return out;
    }
    std::size_t primary_wave_index = 0;
    const double primary_wave = system.primary_wavelength();
    for (std::size_t wi = 0; wi < waves.size(); ++wi) {
        if (std::abs(waves[wi].value_um - primary_wave) <= 1.0e-12) {
            primary_wave_index = wi;
            break;
        }
    }
    std::vector<std::vector<FiniteRayFieldMetric>> metrics(
        fields.size(),
        std::vector<FiniteRayFieldMetric>(waves.size()));
    int attempted = 0;
    int valid = 0;
    for (std::size_t fi = 0; fi < fields.size(); ++fi) {
        for (std::size_t wi = 0; wi < waves.size(); ++wi) {
            if (waves[wi].weight <= 0.0) {
                continue;
            }
            metrics[fi][wi] = finite_ray_field_metric(
                tracer,
                aiming,
                system,
                fi,
                wi,
                fields[fi],
                waves[wi],
                &attempted,
                &valid);
            if (metrics[fi][wi].valid) {
                out.metric_count += 1.0;
            }
        }
    }
    out.valid_fraction = attempted > 0 ? static_cast<double>(valid) / static_cast<double>(attempted)
                                       : std::numeric_limits<double>::quiet_NaN();

    std::size_t on_axis_index = 0;
    double min_field_radius = std::numeric_limits<double>::infinity();
    for (std::size_t fi = 0; fi < fields.size(); ++fi) {
        const auto& metric = metrics[fi][primary_wave_index];
        if (metric.valid && metric.field_radius < min_field_radius) {
            min_field_radius = metric.field_radius;
            on_axis_index = fi;
        }
    }
    const double on_axis_focus = metrics[on_axis_index][primary_wave_index].best_focus_shift_mm;
    double max_field_focus = 0.0;
    bool have_field_focus = false;
    double max_astig = 0.0;
    bool have_astig = false;
    double max_coma = 0.0;
    bool have_coma = false;
    for (std::size_t fi = 0; fi < fields.size(); ++fi) {
        const auto& metric = metrics[fi][primary_wave_index];
        if (!metric.valid) {
            continue;
        }
        if (std::isfinite(metric.best_focus_shift_mm) && std::isfinite(on_axis_focus)) {
            max_field_focus = std::max(max_field_focus, std::abs(metric.best_focus_shift_mm - on_axis_focus));
            have_field_focus = true;
        }
        if (metric.field_radius > 1.0e-8 && std::isfinite(metric.astig_focus_gap_mm)) {
            max_astig = std::max(max_astig, std::abs(metric.astig_focus_gap_mm));
            have_astig = true;
        }
        if (metric.field_radius > 1.0e-8 && std::isfinite(metric.tangential_coma_mm)) {
            max_coma = std::max(max_coma, std::abs(metric.tangential_coma_mm));
            have_coma = true;
        }
    }
    if (have_field_focus) {
        out.field_focus_spread_mm = max_field_focus;
    }
    if (have_astig) {
        out.astig_focus_gap_mm = max_astig;
    }
    if (have_coma) {
        out.coma_tangential_mm = max_coma;
    }
    const double spherical = metrics[on_axis_index][primary_wave_index].spherical_focus_spread_mm;
    if (std::isfinite(spherical)) {
        out.spherical_focus_spread_mm = std::abs(spherical);
    }

    double max_axial = 0.0;
    bool have_axial = false;
    double max_lateral = 0.0;
    bool have_lateral = false;
    for (std::size_t fi = 0; fi < fields.size(); ++fi) {
        const auto& primary = metrics[fi][primary_wave_index];
        if (!primary.valid) {
            continue;
        }
        for (std::size_t wi = 0; wi < waves.size(); ++wi) {
            if (wi == primary_wave_index) {
                continue;
            }
            const auto& metric = metrics[fi][wi];
            if (!metric.valid) {
                continue;
            }
            if (std::isfinite(primary.best_focus_shift_mm) &&
                std::isfinite(metric.best_focus_shift_mm)) {
                max_axial = std::max(max_axial, std::abs(metric.best_focus_shift_mm - primary.best_focus_shift_mm));
                have_axial = true;
            }
            if (std::isfinite(primary.chief_t) &&
                std::isfinite(primary.chief_s) &&
                std::isfinite(metric.chief_t) &&
                std::isfinite(metric.chief_s)) {
                max_lateral = std::max(
                    max_lateral,
                    std::hypot(metric.chief_t - primary.chief_t, metric.chief_s - primary.chief_s));
                have_lateral = true;
            }
        }
    }
    if (have_axial) {
        out.axial_color_focus_shift_mm = max_axial;
    }
    if (have_lateral) {
        out.lateral_color_mm = max_lateral;
    }
    return out;
}

FiniteRayAberrationMetrics finite_ray_aberration_metrics(
    const LensSystem& system,
    const MeritConfig& config) {
    SequentialRayTracer tracer(system);
    AimingSession aiming(system);
    return finite_ray_aberration_metrics_with_context(system, config, tracer, aiming);
}

}  // namespace optiland
