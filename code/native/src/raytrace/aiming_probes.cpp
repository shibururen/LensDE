// Expose chief and calibrated-edge aiming probes without replacing the normal ray generator.
// CLI diagnostic writers consume these records to inspect session seeds and solver progress.
// Probe results retain failure classifications and residuals rather than silently accepting bad rays.

#include "prepared_geometry_inline.hpp"
#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

std::vector<AimingSession::ChiefAimingProbe> AimingSession::probe_chief_aiming(
    int max_budget_iterations) {
    std::vector<ChiefAimingProbe> probes;
    const int max_iter = std::max(0, max_budget_iterations);
    const double hard_radius = first_surface_hard_radius();
    const double soft_radius = first_surface_search_radius();
    const double radius_limit = hard_radius > 0.0 ? hard_radius : soft_radius;
    const PupilPoint chief{0.0, 0.0, 1.0};
    const std::vector<double> inward_factors{
        0.0, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75,
        2.0, 2.25, 2.5, 3.0, 3.5, 4.0, 4.5, 5.0};

    auto physical_stop_radius = [&]() {
        double radius = 0.0;
        const std::size_t stop = tracer_.stop_surface_index();
        if (stop < system_.surfaces.size()) {
            const Surface& stop_surface = system_.surfaces[stop];
            if (stop_surface.aperture_radius > 0.0) {
                radius = stop_surface.aperture_radius;
            } else if (stop_surface.semi_aperture > 0.0) {
                radius = stop_surface.semi_aperture;
            }
        }
        return radius;
    };

    auto trace_summary = [&](const Ray& ray,
                             RayFailureCode* failure_code,
                             int* failure_surface,
                             double* failure_severity) {
        std::vector<TracePoint> history;
        Ray copy = ray;
        tracer_.trace_ray(copy, &history);
        if (failure_code) {
            *failure_code = copy.failure_code;
        }
        if (failure_surface) {
            *failure_surface = copy.failure_surface;
        }
        if (failure_severity) {
            *failure_severity = copy.failure_severity;
        }
        int last = -1;
        for (std::size_t i = 0; i < history.size(); ++i) {
            if (history[i].valid) {
                last = static_cast<int>(i);
            }
        }
        return last;
    };
    auto center_meridional_radius = [&](const Wavelength& wave) {
        if (system_.fields.empty()) {
            return 0.0;
        }
        const auto center_it = std::min_element(
            system_.fields.begin(),
            system_.fields.end(),
            [](const Field& a, const Field& b) {
                return std::hypot(a.hx, a.hy) < std::hypot(b.hx, b.hy);
            });
        const Field& center_field = *center_it;
        const Ray plus = tracer_.generate_ray_with_mode(
            center_field,
            PupilPoint{0.0, 1.0, 1.0},
            wave.value_um,
            RayAimingMode::Paraxial);
        const Ray minus = tracer_.generate_ray_with_mode(
            center_field,
            PupilPoint{0.0, -1.0, 1.0},
            wave.value_um,
            RayAimingMode::Paraxial);
        const FirstSurfaceSeed plus_seed = seed_from_ray(plus);
        const FirstSurfaceSeed minus_seed = seed_from_ray(minus);
        if (!plus_seed.valid || !minus_seed.valid) {
            return 0.0;
        }
        const double radius = 0.5 * std::hypot(plus_seed.x - minus_seed.x,
                                              plus_seed.y - minus_seed.y);
        return std::isfinite(radius) ? radius : 0.0;
    };
    auto inward_seed = [](const FirstSurfaceSeed& seed, double distance) {
        FirstSurfaceSeed out = seed;
        if (!seed.valid || distance <= 0.0) {
            return out;
        }
        const double r = std::hypot(seed.x, seed.y);
        if (!std::isfinite(r) || r <= 1.0e-12) {
            return out;
        }
        const double new_r = std::max(0.0, r - distance);
        const double scale = new_r / r;
        out.x = seed.x * scale;
        out.y = seed.y * scale;
        return out;
    };

    for (std::size_t field_index = 0; field_index < system_.fields.size(); ++field_index) {
        const Field& field = system_.fields[field_index];
        for (std::size_t wave_index = 0; wave_index < system_.wavelengths.size(); ++wave_index) {
            const Wavelength& wave = system_.wavelengths[wave_index];
            ChiefAimingProbe probe;
            probe.field_index = field_index;
            probe.wave_index = wave_index;
            probe.hx = field.hx;
            probe.hy = field.hy;
            probe.wavelength_um = wave.value_um;
            probe.stop_target_radius =
                tracer_.stop_target_radius(wave.value_um, RayAimingMode::CalibratedParaxial);
            probe.physical_stop_radius = physical_stop_radius();
            probe.target_stop_x = field.fvcx * probe.stop_target_radius;
            probe.target_stop_y = field.fvcy * probe.stop_target_radius;

            bool singular = false;
            const FirstSurfaceSeed abcd_seed = estimate_abcd_chief_seed(field, wave, &singular);
            probe.abcd_singular = singular;
            probe.abcd_seed_valid = abcd_seed.valid;
            probe.abcd_seed_x = abcd_seed.x;
            probe.abcd_seed_y = abcd_seed.y;
            probe.abcd_seed_oob =
                abcd_seed.valid &&
                radius_limit > 0.0 &&
                (hard_radius > 0.0
                     ? aperture_clip_exceeded(
                           std::hypot(abcd_seed.x, abcd_seed.y),
                           hard_radius)
                     : std::hypot(abcd_seed.x, abcd_seed.y) >
                           radius_limit * (1.0 + 1.0e-10));

            if (abcd_seed.valid && !probe.abcd_seed_oob) {
                const double center_radius = center_meridional_radius(wave);
                for (const double factor : inward_factors) {
                    const FirstSurfaceSeed trial_seed =
                        inward_seed(abcd_seed, factor * center_radius);
                    const bool trial_oob =
                        trial_seed.valid &&
                        radius_limit > 0.0 &&
                        (hard_radius > 0.0
                             ? aperture_clip_exceeded(
                                   std::hypot(trial_seed.x, trial_seed.y),
                                   hard_radius)
                             : std::hypot(trial_seed.x, trial_seed.y) >
                                   radius_limit * (1.0 + 1.0e-10));
                    if (!trial_seed.valid || trial_oob) {
                        continue;
                    }
                    for (int iterations = 0; iterations <= max_iter; ++iterations) {
                        Ray ray = tracer_.generate_ray_through_first_surface_point(
                            field,
                            wave.value_um,
                            trial_seed.x,
                            trial_seed.y,
                            1.0);
                        if (iterations > 0) {
                            ray = tracer_.solve_stop_aim_from_initial(
                                field,
                                chief,
                                wave.value_um,
                                ray,
                                false,
                                iterations);
                        }
                        ChiefAimingProbeStep step;
                        step.strategy = factor == 0.0 ? "abcd" : "inward_center_radius";
                        step.inward_factor = factor;
                        step.center_meridional_radius = center_radius;
                        step.iterations = iterations;
                        step.aim_converged = ray.aim_converged;
                        step.aim_residual = ray.aim_residual;
                        step.failure_code = ray.failure_code;
                        step.failure_surface = ray.failure_surface;
                        step.failure_severity = ray.failure_severity;
                        step.last_valid_surface = trace_summary(
                            ray,
                            &step.trace_failure_code,
                            &step.trace_failure_surface,
                            &step.trace_failure_severity);
                        const FirstSurfaceSeed step_seed = seed_from_ray(ray);
                        step.first_surface_seed_valid = step_seed.valid;
                        step.first_surface_x = step_seed.x;
                        step.first_surface_y = step_seed.y;
                        double sx = 0.0;
                        double sy = 0.0;
                        if (sample_stop_from_ray(ray, &sx, &sy)) {
                            step.trace_to_stop = true;
                            step.stop_x = sx;
                            step.stop_y = sy;
                            step.stop_radial = std::hypot(sx, sy);
                            step.stop_residual =
                                std::hypot(sx - probe.target_stop_x, sy - probe.target_stop_y);
                        }
                        probe.budget_steps.push_back(step);
                    }
                }
            }

            ChiefAnchor full_anchor = ensure_chief_anchor(field_index, wave_index, field, wave);
            probe.full_anchor_valid = full_anchor.valid;
            if (full_anchor.valid) {
                probe.full_seed_x = full_anchor.seed.x;
                probe.full_seed_y = full_anchor.seed.y;
                double sx = 0.0;
                double sy = 0.0;
                if (sample_stop_from_ray(full_anchor.ray, &sx, &sy)) {
                    probe.full_stop_x = sx;
                    probe.full_stop_y = sy;
                    probe.full_stop_radial = std::hypot(sx, sy);
                    probe.full_stop_residual =
                        std::hypot(sx - probe.target_stop_x, sy - probe.target_stop_y);
                }
            }
            probes.push_back(probe);
        }
    }
    return probes;
}

std::vector<AimingSession::ThreeStepChiefProbe> AimingSession::probe_three_step_chief_aiming() {
    std::vector<ThreeStepChiefProbe> probes;
    const auto& cfg = system_.ray_aiming.three_step;
    const PupilPoint chief{0.0, 0.0, 1.0};
    (void)chief;

    for (std::size_t field_index = 0; field_index < system_.fields.size(); ++field_index) {
        const Field& field = system_.fields[field_index];
        const double field_radius = std::hypot(field.hx, field.hy);
        double mer_x = 0.0;
        double mer_y = 1.0;
        if (field_radius > 1.0e-12) {
            mer_x = field.hx / field_radius;
            mer_y = field.hy / field_radius;
        }
        const double target_angle_deg =
            std::max(0.0, system_.max_field_deg) * field_radius;
        for (std::size_t wave_index = 0; wave_index < system_.wavelengths.size(); ++wave_index) {
            const Wavelength& wave = system_.wavelengths[wave_index];
            ThreeStepChiefProbe probe;
            probe.field_index = field_index;
            probe.wave_index = wave_index;
            probe.hx = field.hx;
            probe.hy = field.hy;
            probe.wavelength_um = wave.value_um;
            probe.field_radius = field_radius;
            probe.target_angle_deg = target_angle_deg;
            probe.trial_rays = std::max(2, cfg.chief_reverse_trial_rays);
            probe.max_reverse_angle_deg = std::clamp(cfg.chief_reverse_max_angle_deg, 0.0, 89.999);

            const ThreeStepReverseMap& reverse_map = ensure_three_step_reverse_map(wave_index, wave);
            const auto& unique_samples = reverse_map.samples;
            auto seed_from_reverse_radius = [&](double radius) {
                return FirstSurfaceSeed{radius * mer_x, radius * mer_y, true};
            };
            probe.reverse_sample_count = static_cast<int>(unique_samples.size());
            if (!unique_samples.empty()) {
                probe.min_sample_angle_deg = unique_samples.front().first;
                probe.max_sample_angle_deg = unique_samples.back().first;
            }

            auto set_prediction = [&](const FirstSurfaceSeed& seed) {
                probe.predicted_valid = seed.valid;
                probe.predicted_x = seed.x;
                probe.predicted_y = seed.y;
            };
            if (!unique_samples.empty()) {
                if (unique_samples.size() == 1 ||
                    target_angle_deg <= unique_samples.front().first) {
                    probe.clamped_low = target_angle_deg < unique_samples.front().first;
                    probe.lo_angle_deg = unique_samples.front().first;
                    probe.hi_angle_deg = unique_samples.front().first;
                    probe.interpolation_t = 0.0;
                    set_prediction(seed_from_reverse_radius(unique_samples.front().second));
                } else if (target_angle_deg >= unique_samples.back().first) {
                    probe.clamped_high = target_angle_deg > unique_samples.back().first;
                    probe.lo_angle_deg = unique_samples.back().first;
                    probe.hi_angle_deg = unique_samples.back().first;
                    probe.interpolation_t = 1.0;
                    set_prediction(seed_from_reverse_radius(unique_samples.back().second));
                } else {
                    for (std::size_t i = 1; i < unique_samples.size(); ++i) {
                        const auto& lo = unique_samples[i - 1];
                        const auto& hi = unique_samples[i];
                        if (target_angle_deg > hi.first) {
                            continue;
                        }
                        const double span = std::max(1.0e-12, hi.first - lo.first);
                        const double t = std::clamp((target_angle_deg - lo.first) / span, 0.0, 1.0);
                        probe.bracketed = true;
                        probe.lo_angle_deg = lo.first;
                        probe.hi_angle_deg = hi.first;
                        probe.interpolation_t = t;
                        set_prediction(seed_from_reverse_radius(lo.second + t * (hi.second - lo.second)));
                        break;
                    }
                }
            }

            auto stop_direction_angle_deg = [&](Ray ray, double* out_angle) {
                if (!out_angle) {
                    return false;
                }
                TracePoint hit;
                if (!tracer_.trace_to_surface(
                        ray,
                        tracer_.stop_surface_index(),
                        &hit,
                        true,
                        true) ||
                    !hit.valid) {
                    return false;
                }
                const double angle = rad_to_deg(std::atan2(
                    std::hypot(hit.l, hit.m),
                    hit.n));
                if (!std::isfinite(angle)) {
                    return false;
                }
                *out_angle = angle;
                return true;
            };

            if (probe.predicted_valid) {
                Ray predicted = tracer_.generate_ray_through_first_surface_point(
                    field,
                    wave.value_um,
                    probe.predicted_x,
                    probe.predicted_y,
                    1.0);
                (void)stop_direction_angle_deg(
                    predicted,
                    &probe.predicted_stop_direction_angle_deg);
                double sx = 0.0;
                double sy = 0.0;
                if (sample_stop_from_ray(predicted, &sx, &sy)) {
                    const double stop_radius =
                        tracer_.stop_target_radius(wave.value_um, RayAimingMode::ThreeStep);
                    const double target_x = field.fvcx * stop_radius;
                    const double target_y = field.fvcy * stop_radius;
                    probe.predicted_trace_to_stop = true;
                    probe.predicted_stop_residual = std::hypot(sx - target_x, sy - target_y);
                } else {
                    tracer_.trace_ray(predicted);
                    probe.predicted_failure_code = predicted.failure_code;
                    probe.predicted_failure_surface = predicted.failure_surface;
                    probe.predicted_failure_severity = predicted.failure_severity;
                }
            }

            ChiefAnchor actual = ensure_chief_anchor(field_index, wave_index, field, wave);
            probe.actual_valid = actual.valid && actual.seed.valid;
            if (probe.actual_valid) {
                probe.actual_x = actual.seed.x;
                probe.actual_y = actual.seed.y;
                (void)stop_direction_angle_deg(
                    actual.ray,
                    &probe.actual_stop_direction_angle_deg);
                if (std::isfinite(probe.actual_stop_direction_angle_deg)) {
                    auto reverse_angle_probe = [&](double stop_angle_deg,
                                                   double* object_angle_deg,
                                                   Vec3* first_point_out = nullptr) {
                        if (!std::isfinite(stop_angle_deg) || !object_angle_deg) {
                            return false;
                        }
                        Vec3 first_point;
                        Vec3 object_space_forward_direction;
                        if (!reverse_trace_stop_center_to_first(
                                system_,
                                wave.value_um,
                                mer_x,
                                mer_y,
                                stop_angle_deg,
                                &first_point,
                                &object_space_forward_direction)) {
                            return false;
                        }
                        const double object_angle = rad_to_deg(std::atan2(
                            std::hypot(
                                object_space_forward_direction.x,
                                object_space_forward_direction.y),
                            object_space_forward_direction.z));
                        if (!std::isfinite(object_angle)) {
                            return false;
                        }
                        *object_angle_deg = object_angle;
                        if (first_point_out) {
                            *first_point_out = first_point;
                        }
                        return true;
                    };
                    Vec3 first_point;
                    if (reverse_angle_probe(
                            probe.actual_stop_direction_angle_deg,
                            &probe.reverse_actual_stop_angle_object_angle_deg,
                            &first_point)) {
                        probe.reverse_actual_stop_angle_valid = true;
                        probe.reverse_actual_stop_angle_x = first_point.x;
                        probe.reverse_actual_stop_angle_y = first_point.y;
                        probe.reverse_actual_stop_angle_delta_r = std::hypot(
                            first_point.x - actual.seed.x,
                            first_point.y - actual.seed.y);
                    }
                    probe.reverse_floor_stop_angle_deg =
                        std::floor(probe.actual_stop_direction_angle_deg);
                    probe.reverse_ceil_stop_angle_deg =
                        std::ceil(probe.actual_stop_direction_angle_deg);
                    (void)reverse_angle_probe(
                        probe.reverse_floor_stop_angle_deg,
                        &probe.reverse_floor_object_angle_deg);
                    (void)reverse_angle_probe(
                        probe.reverse_ceil_stop_angle_deg,
                        &probe.reverse_ceil_object_angle_deg);
                    (void)reverse_angle_probe(
                        probe.actual_stop_direction_angle_deg - 0.1,
                        &probe.reverse_minus_0p1_object_angle_deg);
                    (void)reverse_angle_probe(
                        probe.actual_stop_direction_angle_deg + 0.1,
                        &probe.reverse_plus_0p1_object_angle_deg);
                }
                double sx = 0.0;
                double sy = 0.0;
                if (sample_stop_from_ray(actual.ray, &sx, &sy)) {
                    const double stop_radius =
                        tracer_.stop_target_radius(wave.value_um, RayAimingMode::ThreeStep);
                    const double target_x = field.fvcx * stop_radius;
                    const double target_y = field.fvcy * stop_radius;
                    probe.actual_stop_residual = std::hypot(sx - target_x, sy - target_y);
                }
            }
            ThreeStepModel& model =
                ensure_three_step_model(field_index, wave_index, field, wave);
            probe.model_valid = model.valid;
            probe.model_fit_residual = model.fit_residual;
            probe.meridional_x = model.meridional_x;
            probe.meridional_y = model.meridional_y;
            probe.sagittal_x = model.sagittal_x;
            probe.sagittal_y = model.sagittal_y;
            probe.meridional_radius = std::hypot(model.meridional_x, model.meridional_y);
            probe.sagittal_radius = std::hypot(model.sagittal_x, model.sagittal_y);
            probe.delta_x = probe.predicted_x - probe.actual_x;
            probe.delta_y = probe.predicted_y - probe.actual_y;
            probe.delta_r = std::hypot(probe.delta_x, probe.delta_y);
            probes.push_back(probe);
        }
    }
    return probes;
}

std::vector<AimingSession::CalibratedEdgeInitialProbe>
AimingSession::probe_calibrated_edge_initials() {
    std::vector<CalibratedEdgeInitialProbe> probes;
    if (system_.fields.empty() || system_.wavelengths.empty()) {
        return probes;
    }
    for (std::size_t i = 0; i < system_.fields.size(); ++i) {
        fields_seen_[i] = system_.fields[i];
    }

    auto field_center_score = [](const Field& candidate) {
        double score = std::hypot(candidate.hx, candidate.hy);
        if (!std::isfinite(score)) {
            score = std::numeric_limits<double>::infinity();
        }
        return score;
    };
    std::size_t center_field_index = 0;
    auto best_center_score = field_center_score(system_.fields.front());
    for (std::size_t i = 1; i < system_.fields.size(); ++i) {
        const auto score = field_center_score(system_.fields[i]);
        if (score < best_center_score) {
            best_center_score = score;
            center_field_index = i;
        }
    }

    const double search_radius = first_surface_search_radius();
    const std::array<PupilPoint, 4> edge_pupils{{
        PupilPoint{1.0, 0.0, 1.0},
        PupilPoint{-1.0, 0.0, 1.0},
        PupilPoint{0.0, 1.0, 1.0},
        PupilPoint{0.0, -1.0, 1.0},
    }};
    auto fill_seed_delta = [](bool seed_valid,
                              double seed_x,
                              double seed_y,
                              bool truth_valid,
                              double truth_x,
                              double truth_y,
                              double* dx,
                              double* dy,
                              double* dr) {
        if (!dx || !dy || !dr) {
            return;
        }
        if (!seed_valid || !truth_valid) {
            *dx = 0.0;
            *dy = 0.0;
            *dr = std::numeric_limits<double>::infinity();
            return;
        }
        *dx = seed_x - truth_x;
        *dy = seed_y - truth_y;
        *dr = std::hypot(*dx, *dy);
    };

    auto stop_residual_from_seed = [&](const Field& field,
                                       const Wavelength& wave,
                                       const FirstSurfaceSeed& seed,
                                       double target_x,
                                       double target_y) {
        double sx = 0.0;
        double sy = 0.0;
        if (!sample_stop_from_seed(field, wave, seed, &sx, &sy)) {
            return std::numeric_limits<double>::infinity();
        }
        return std::hypot(sx - target_x, sy - target_y);
    };

    auto stop_residual_from_ray = [&](Ray ray, double target_x, double target_y) {
        double sx = 0.0;
        double sy = 0.0;
        if (!sample_stop_from_ray(ray, &sx, &sy)) {
            return std::numeric_limits<double>::infinity();
        }
        return std::hypot(sx - target_x, sy - target_y);
    };

    for (std::size_t wave_index = 0; wave_index < system_.wavelengths.size(); ++wave_index) {
        const Wavelength& wave = system_.wavelengths[wave_index];
        const Field& center_field = system_.fields[center_field_index];
        (void)ensure_calibrated_paraxial_model(
            center_field_index,
            wave_index,
            center_field,
            wave);
        const auto footprint_it = calibrated_center_beam_footprints_.find(wave_index);
        const CalibratedCenterBeamFootprint* center_footprint =
            footprint_it != calibrated_center_beam_footprints_.end()
                ? &footprint_it->second
                : nullptr;
        const bool center_footprint_valid =
            center_footprint &&
            center_footprint->valid &&
            std::abs(center_footprint->wavelength_um - wave.value_um) <= 1.0e-12;

        const double ep_radius =
            std::max(1.0e-9, tracer_.entrance_pupil_radius(wave.value_um));
        const double edge_radius = std::clamp(
            ep_radius,
            1.0e-9,
            std::max(1.0e-9, search_radius));
        const double rs =
            tracer_.stop_target_radius(wave.value_um, RayAimingMode::CalibratedParaxial);

        for (std::size_t field_index = 0; field_index < system_.fields.size(); ++field_index) {
            const Field field = system_.fields[field_index];
            CalibratedParaxialModel& model = ensure_calibrated_paraxial_model(
                field_index,
                wave_index,
                field,
                wave);
            for (std::size_t edge_index = 0; edge_index < edge_pupils.size(); ++edge_index) {
                const PupilPoint& edge = edge_pupils[edge_index];
                CalibratedEdgeInitialProbe probe;
                probe.field_index = field_index;
                probe.wave_index = wave_index;
                probe.edge_index = edge_index;
                probe.hx = field.hx;
                probe.hy = field.hy;
                probe.wavelength_um = wave.value_um;
                probe.px = edge.px;
                probe.py = edge.py;
                probe.is_center_field = field_index == center_field_index;
                probe.stop_target_radius = rs;
                probe.target_stop_x = (field.fvcx + edge.px * (1.0 - field.fvdx)) * rs;
                probe.target_stop_y = (field.fvcy + edge.py * (1.0 - field.fvdy)) * rs;
                probe.entrance_pupil_radius = ep_radius;
                probe.near_pupil_edge_radius = edge_radius;
                probe.model_valid = model.valid;
                probe.center_footprint_valid = center_footprint_valid;
                probe.chief_seed_valid = model.chief_seed.valid;
                probe.chief_x = model.chief_seed.x;
                probe.chief_y = model.chief_seed.y;

                Ray truth_ray = tracer_.generate_ray_with_mode(
                    field,
                    edge,
                    wave.value_um,
                    RayAimingMode::Iterative);
                const FirstSurfaceSeed truth_seed = seed_from_ray(truth_ray);
                probe.truth_valid = truth_ray.valid && truth_seed.valid;
                if (probe.truth_valid) {
                    probe.truth_x = truth_seed.x;
                    probe.truth_y = truth_seed.y;
                    probe.truth_stop_residual = stop_residual_from_ray(
                        truth_ray,
                        probe.target_stop_x,
                        probe.target_stop_y);
                }

                if (model.chief_seed.valid) {
                    const FirstSurfaceSeed near_seed{
                        model.chief_seed.x + edge_radius * edge.px,
                        model.chief_seed.y + edge_radius * edge.py,
                        true};
                    probe.near_seed_valid = true;
                    probe.near_seed_x = near_seed.x;
                    probe.near_seed_y = near_seed.y;
                    probe.near_seed_stop_residual = stop_residual_from_seed(
                        field,
                        wave,
                        near_seed,
                        probe.target_stop_x,
                        probe.target_stop_y);
                }
                fill_seed_delta(
                    probe.near_seed_valid,
                    probe.near_seed_x,
                    probe.near_seed_y,
                    probe.truth_valid,
                    probe.truth_x,
                    probe.truth_y,
                    &probe.near_seed_to_truth_dx,
                    &probe.near_seed_to_truth_dy,
                    &probe.near_seed_to_truth_dr);

                if (center_footprint_valid &&
                    center_footprint->edge_vectors[edge_index].valid &&
                    model.chief_seed.valid &&
                    std::abs(edge.py) > std::abs(edge.px)) {
                    const FirstSurfaceSeed& vector =
                        center_footprint->edge_vectors[edge_index];
                    probe.center_vector_x = vector.x;
                    probe.center_vector_y = vector.y;
                    probe.center_vector_radius = std::hypot(vector.x, vector.y);
                    const FirstSurfaceSeed center_seed =
                        shifted_chief_seed_by_transverse_radius(
                            field,
                            wave,
                            model.chief_seed,
                            edge,
                            probe.center_vector_radius);
                    probe.center_seed_valid = center_seed.valid;
                    if (center_seed.valid) {
                        probe.center_seed_x = center_seed.x;
                        probe.center_seed_y = center_seed.y;
                        probe.center_seed_stop_residual = stop_residual_from_seed(
                            field,
                            wave,
                            center_seed,
                            probe.target_stop_x,
                            probe.target_stop_y);
                    }
                }
                fill_seed_delta(
                    probe.center_seed_valid,
                    probe.center_seed_x,
                    probe.center_seed_y,
                    probe.truth_valid,
                    probe.truth_x,
                    probe.truth_y,
                    &probe.center_seed_to_truth_dx,
                    &probe.center_seed_to_truth_dy,
                    &probe.center_seed_to_truth_dr);

                if (edge_index < model.edge_anchors.size()) {
                    const auto& anchor = model.edge_anchors[edge_index];
                    probe.calibrated_anchor_valid = anchor.valid && anchor.seed.valid;
                    if (probe.calibrated_anchor_valid) {
                        probe.calibrated_anchor_x = anchor.seed.x;
                        probe.calibrated_anchor_y = anchor.seed.y;
                    }
                }
                fill_seed_delta(
                    probe.calibrated_anchor_valid,
                    probe.calibrated_anchor_x,
                    probe.calibrated_anchor_y,
                    probe.truth_valid,
                    probe.truth_x,
                    probe.truth_y,
                    &probe.calibrated_anchor_to_truth_dx,
                    &probe.calibrated_anchor_to_truth_dy,
                    &probe.calibrated_anchor_to_truth_dr);

                probes.push_back(probe);
            }
        }
    }
    return probes;
}

} // namespace optiland
