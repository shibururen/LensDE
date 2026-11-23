/*
 * Integrates projected pupil samples along a field scan for relative illumination.
 * The public relative_illumination entry point is used by dedicated CLI measurements.
 * Requested wavelength, scan direction, vignetting-factor removal and normalization
 * remain explicit measurement choices, separate from the merit illumination proxy.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

RelativeIlluminationResult relative_illumination(
    const LensSystem& system,
    const RelativeIlluminationConfig& requested_config) {
    RelativeIlluminationResult result;
    result.config = requested_config;
    result.config.ray_density = std::max(5, requested_config.ray_density);
    result.config.field_density = std::max(1, requested_config.field_density);

    Wavelength wave{system.primary_wavelength(), 1.0, true};
    if (std::isfinite(requested_config.wavelength_um) &&
        requested_config.wavelength_um > 0.0) {
        wave.value_um = requested_config.wavelength_um;
    } else if (!system.wavelengths.empty()) {
        const auto primary = std::find_if(
            system.wavelengths.begin(),
            system.wavelengths.end(),
            [](const Wavelength& candidate) { return candidate.primary; });
        wave = primary != system.wavelengths.end() ? *primary : system.wavelengths.front();
    }
    wave.primary = true;
    wave.weight = 1.0;
    result.wavelength_um = wave.value_um;

    double scan_hx = requested_config.scan_hx;
    double scan_hy = requested_config.scan_hy;
    const double scan_norm = std::hypot(scan_hx, scan_hy);
    if (!(scan_norm > 1.0e-14) || !std::isfinite(scan_norm)) {
        scan_hx = 0.0;
        scan_hy = 1.0;
    } else {
        scan_hx /= scan_norm;
        scan_hy /= scan_norm;
    }

    auto interpolated_field = [&](double rho) {
        Field out;
        out.hx = rho * scan_hx;
        out.hy = rho * scan_hy;
        if (requested_config.remove_vignetting_factors || system.fields.empty()) {
            return out;
        }
        struct RadialField {
            double rho = 0.0;
            Field field;
        };
        std::vector<RadialField> radial;
        radial.reserve(system.fields.size());
        for (const Field& field : system.fields) {
            radial.push_back({std::hypot(field.hx, field.hy), field});
        }
        std::sort(
            radial.begin(), radial.end(),
            [](const RadialField& a, const RadialField& b) { return a.rho < b.rho; });
        const auto copy_factors = [&](const Field& source) {
            out.fvcx = source.fvcx;
            out.fvcy = source.fvcy;
            out.fvdx = source.fvdx;
            out.fvdy = source.fvdy;
            out.vignette_x = source.vignette_x;
            out.vignette_y = source.vignette_y;
        };
        if (rho <= radial.front().rho || radial.size() == 1) {
            copy_factors(radial.front().field);
            return out;
        }
        if (rho >= radial.back().rho) {
            copy_factors(radial.back().field);
            return out;
        }
        const auto upper = std::upper_bound(
            radial.begin(), radial.end(), rho,
            [](double value, const RadialField& sample) { return value < sample.rho; });
        const RadialField& hi = *upper;
        const RadialField& lo = *(upper - 1);
        const double span = std::max(1.0e-14, hi.rho - lo.rho);
        const double t = std::clamp((rho - lo.rho) / span, 0.0, 1.0);
        auto blend = [t](double a, double b) { return a + t * (b - a); };
        out.fvcx = blend(lo.field.fvcx, hi.field.fvcx);
        out.fvcy = blend(lo.field.fvcy, hi.field.fvcy);
        out.fvdx = blend(lo.field.fvdx, hi.field.fvdx);
        out.fvdy = blend(lo.field.fvdy, hi.field.fvdy);
        out.vignette_x = out.fvdx;
        out.vignette_y = out.fvdy;
        return out;
    };

    LensSystem analysis_system = system;
    analysis_system.fields.clear();
    analysis_system.fields.reserve(static_cast<std::size_t>(result.config.field_density));
    for (int fi = 0; fi < result.config.field_density; ++fi) {
        const double rho = result.config.field_density == 1
                               ? 0.0
                               : static_cast<double>(fi) /
                                     static_cast<double>(result.config.field_density - 1);
        analysis_system.fields.push_back(interpolated_field(rho));
    }
    analysis_system.wavelengths = {wave};

    struct CosineSample {
        double l = 0.0;
        double m = 0.0;
        double transmission = 0.0;
        bool inside = false;
        bool valid = false;
    };

    const int density = result.config.ray_density;
    const double pupil_step = 2.0 / static_cast<double>(density);
    SequentialRayTracer tracer(analysis_system);
    AimingSession aiming(analysis_system);
    result.points.reserve(analysis_system.fields.size());

    for (std::size_t fi = 0; fi < analysis_system.fields.size(); ++fi) {
        const Field& field = analysis_system.fields[fi];
        RelativeIlluminationPoint point;
        point.field_hx = field.hx;
        point.field_hy = field.hy;
        point.field_angle_deg =
            std::hypot(field.hx, field.hy) * std::abs(analysis_system.max_field_deg);
        point.wavelength_um = wave.value_um;

        // The analysis never changes the configured aiming mode.  In
        // particular, an AdaptivePilotGrid system is sampled exclusively
        // through the same AimingSession used by the rest of the program.
        const PupilPoint chief_pupil{0.0, 0.0, 1.0};
        Ray chief = ray_aiming_uses_session(analysis_system.ray_aiming.mode)
                        ? aiming.generate(
                              fi,
                              0,
                              static_cast<std::size_t>(density * density),
                              field,
                              wave,
                              chief_pupil)
                        : tracer.generate_ray_with_mode(
                              field,
                              chief_pupil,
                              wave.value_um,
                              analysis_system.ray_aiming.mode);
        tracer.trace_ray(chief, nullptr);
        const double image_z = analysis_system.surfaces.empty()
                                   ? chief.p.z
                                   : [&]() {
                                         double z = 0.0;
                                         for (std::size_t si = analysis_system.first_optical_surface();
                                              si + 1 < analysis_system.surfaces.size();
                                              ++si) {
                                             const double thickness =
                                                 analysis_system.surfaces[si].thickness;
                                             if (std::isfinite(thickness)) {
                                                 z += thickness;
                                             }
                                         }
                                         return z;
                                     }();
        const double pupil_z = image_z + tracer.exit_pupil_location(wave.value_um);
        auto reference_direction_cosines = [&](const Ray& ray, double* l, double* m) {
            if (!ray.valid || !chief.valid) {
                return false;
            }
            // Rimmer's forward-ray form: convert the traced image-space
            // direction (l',m') to the direction subtended at the chief-ray
            // image point, including first-order transverse ray aberration.
            const double x = ray.p.x - chief.p.x;
            const double y = ray.p.y - chief.p.y;
            const double distance_to_exit_pupil = std::abs(pupil_z - chief.p.z);
            if (!(distance_to_exit_pupil > kEps) ||
                !std::isfinite(distance_to_exit_pupil)) {
                return false;
            }
            const double lp = ray.d.x;
            const double mp = ray.d.y;
            *l = lp - (1.0 - lp * lp) * x / distance_to_exit_pupil +
                 lp * mp * y / distance_to_exit_pupil;
            *m = mp - (1.0 - mp * mp) * y / distance_to_exit_pupil +
                 lp * mp * x / distance_to_exit_pupil;
            return std::isfinite(*l) && std::isfinite(*m);
        };

        std::vector<CosineSample> samples(
            static_cast<std::size_t>(density) * static_cast<std::size_t>(density));
        auto at = [&](int ix, int iy) -> CosineSample& {
            return samples[static_cast<std::size_t>(iy * density + ix)];
        };
        auto coordinate = [&](int index) {
            return -1.0 + (static_cast<double>(index) + 0.5) * pupil_step;
        };

        for (int iy = 0; iy < density; ++iy) {
            const double py = coordinate(iy);
            for (int ix = 0; ix < density; ++ix) {
                const double px = coordinate(ix);
                CosineSample& sample = at(ix, iy);
                sample.inside = px * px + py * py <= 1.0 + 1.0e-14;
                if (!sample.inside) {
                    continue;
                }
                ++point.attempted_rays;
                const std::size_t pupil_index =
                    static_cast<std::size_t>(iy * density + ix);
                const PupilPoint pupil{px, py, 1.0};
                Ray ray = ray_aiming_uses_session(analysis_system.ray_aiming.mode)
                              ? aiming.generate(fi, 0, pupil_index, field, wave, pupil)
                              : tracer.generate_ray_with_mode(
                                    field,
                                    pupil,
                                    wave.value_um,
                                    analysis_system.ray_aiming.mode);
                tracer.trace_ray(ray, nullptr);
                sample.valid = ray.valid && std::isfinite(ray.intensity) &&
                               reference_direction_cosines(ray, &sample.l, &sample.m);
                if (!sample.valid) {
                    continue;
                }
                sample.transmission = std::max(0.0, ray.intensity);
                ++point.valid_rays;
            }
        }

        // Integrate the effective pupil in image direction-cosine space.
        // Each valid sample contributes the local |d(l,m)/d(px,py)| area.
        // A failed aiming/trace sample stays failed; the analysis never
        // substitutes a different ray-aiming algorithm.
        auto derivative = [&](int ix,
                              int iy,
                              bool x_axis,
                              double* dl,
                              double* dm) {
            const CosineSample& center = at(ix, iy);
            if (!center.valid) {
                return false;
            }
            const int lower_index = x_axis ? ix - 1 : iy - 1;
            const int upper_index = x_axis ? ix + 1 : iy + 1;
            const CosineSample* lower = nullptr;
            const CosineSample* upper = nullptr;
            if (lower_index >= 0) {
                lower = x_axis ? &at(ix - 1, iy) : &at(ix, iy - 1);
                if (!lower->valid) {
                    lower = nullptr;
                }
            }
            if (upper_index < density) {
                upper = x_axis ? &at(ix + 1, iy) : &at(ix, iy + 1);
                if (!upper->valid) {
                    upper = nullptr;
                }
            }
            if (lower && upper) {
                *dl = (upper->l - lower->l) / (2.0 * pupil_step);
                *dm = (upper->m - lower->m) / (2.0 * pupil_step);
                return true;
            }
            if (upper) {
                *dl = (upper->l - center.l) / pupil_step;
                *dm = (upper->m - center.m) / pupil_step;
                return true;
            }
            if (lower) {
                *dl = (center.l - lower->l) / pupil_step;
                *dm = (center.m - lower->m) / pupil_step;
                return true;
            }
            return false;
        };

        double area = std::numeric_limits<double>::quiet_NaN();
        double accumulated_area = 0.0;
        int reference_orientation = 0;
        for (int iy = 0; iy < density; ++iy) {
            for (int ix = 0; ix < density; ++ix) {
                const CosineSample& sample = at(ix, iy);
                if (!sample.valid) {
                    continue;
                }
                double dl_dpx = 0.0;
                double dm_dpx = 0.0;
                double dl_dpy = 0.0;
                double dm_dpy = 0.0;
                if (!derivative(ix, iy, true, &dl_dpx, &dm_dpx) ||
                    !derivative(ix, iy, false, &dl_dpy, &dm_dpy)) {
                    continue;
                }
                const double determinant =
                    dl_dpx * dm_dpy - dl_dpy * dm_dpx;
                if (!std::isfinite(determinant)) {
                    continue;
                }
                const int orientation = determinant < 0.0 ? -1 : 1;
                if (reference_orientation == 0 && std::abs(determinant) > 1.0e-18) {
                    reference_orientation = orientation;
                } else if (reference_orientation != 0 &&
                           orientation != reference_orientation) {
                    ++point.orientation_flip_samples;
                }
                accumulated_area +=
                    std::abs(determinant) * sample.transmission *
                    pupil_step * pupil_step;
                ++point.integrated_samples;
            }
        }
        if (point.integrated_samples > 0 && accumulated_area > 0.0 &&
            std::isfinite(accumulated_area)) {
            area = accumulated_area;
        }
        point.projected_cosine_area =
            area > 0.0 && std::isfinite(area)
                ? area
                : std::numeric_limits<double>::quiet_NaN();
        if (std::isfinite(point.projected_cosine_area)) {
            point.effective_f_number =
                0.5 * std::sqrt(kPi / point.projected_cosine_area);
        }
        result.points.push_back(point);
    }

    if (!result.points.empty()) {
        std::size_t reference_index = 0;
        if (result.config.normalize_to_maximum) {
            for (std::size_t i = 1; i < result.points.size(); ++i) {
                if (result.points[i].projected_cosine_area >
                    result.points[reference_index].projected_cosine_area) {
                    reference_index = i;
                }
            }
        }
        result.normalization_field_index = reference_index;
        result.normalization_area = result.points[reference_index].projected_cosine_area;
        if (std::isfinite(result.normalization_area) && result.normalization_area > 0.0) {
            for (RelativeIlluminationPoint& point : result.points) {
                point.relative_illumination =
                    point.projected_cosine_area / result.normalization_area;
            }
        }
    }
    return result;
}

}  // namespace optiland
