// Trace the explicit center/edge iterative diagnostic sequence and retain each solver step.
// The CLI center-edge probe uses these records to inspect behavior outside ordinary optimization.
// Keep the recorded seed, STOP target, failure, and residual values tied to the same probe step.

#include "aiming_geometry_inline.hpp"
#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

std::vector<AimingSession::CalibratedEdgeIterativeProbeStep>
AimingSession::probe_center_edge_iterative_steps() {
    std::vector<CalibratedEdgeIterativeProbeStep> probes;
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

    const Field field = system_.fields[center_field_index];
    const std::array<PupilPoint, 4> edge_pupils{{
        PupilPoint{1.0, 0.0, 1.0},
        PupilPoint{-1.0, 0.0, 1.0},
        PupilPoint{0.0, 1.0, 1.0},
        PupilPoint{0.0, -1.0, 1.0},
    }};

    for (std::size_t wave_index = 0; wave_index < system_.wavelengths.size(); ++wave_index) {
        const Wavelength& wave = system_.wavelengths[wave_index];
        CalibratedParaxialModel& model = ensure_calibrated_paraxial_model(
            center_field_index,
            wave_index,
            field,
            wave);
        const auto& cfg = system_.ray_aiming.calibrated_paraxial;
        const double search_radius = first_surface_search_radius();
        const double edge_radius = std::clamp(
            std::max(1.0e-9, tracer_.entrance_pupil_radius(wave.value_um)),
            1.0e-9,
            std::max(1.0e-9, search_radius));
        const double rs =
            tracer_.stop_target_radius(wave.value_um, RayAimingMode::CalibratedParaxial);
        const std::size_t si = tracer_.stop_surface_index();
        auto physical_stop_radius = [&]() {
            double radius = rs;
            if (si < system_.surfaces.size()) {
                const Surface& stop_surface = system_.surfaces[si];
                if (stop_surface.aperture_radius > 0.0) {
                    radius = stop_surface.aperture_radius;
                } else if (stop_surface.semi_aperture > 0.0) {
                    radius = stop_surface.semi_aperture;
                }
            }
            return radius;
        };
        const double residual_limit = std::max(
            std::max(0.0, system_.ray_aiming.tolerance),
            std::max(0.0, cfg.edge_calibration_residual_stop_fraction) * physical_stop_radius());
        const int max_iterations = std::max(1, cfg.edge_calibration_iterations);

        for (std::size_t edge_index = 0; edge_index < edge_pupils.size(); ++edge_index) {
            const PupilPoint& edge = edge_pupils[edge_index];
            const double target_x = (field.fvcx + edge.px * (1.0 - field.fvdx)) * rs;
            const double target_y = (field.fvcy + edge.py * (1.0 - field.fvdy)) * rs;
            FirstSurfaceSeed seed = {
                model.chief_seed.x + edge_radius * edge.px,
                model.chief_seed.y + edge_radius * edge.py,
                true};
            Ray guess = tracer_.generate_ray_through_first_surface_point(
                field,
                wave.value_um,
                seed.x,
                seed.y,
                edge.weight);

            auto append_step = [&](int iteration,
                                   const FirstSurfaceSeed& current_seed,
                                   const Ray& current_ray,
                                   bool trace_ok,
                                   const TracePoint& stop_hit,
                                   double residual,
                                   bool accepted,
                                   bool terminated,
                                   const char* reason,
                                   const char* trace_failure_reason,
                                   int trace_failure_surface) {
                CalibratedEdgeIterativeProbeStep step;
                step.field_index = center_field_index;
                step.wave_index = wave_index;
                step.edge_index = edge_index;
                step.iteration = iteration;
                step.hx = field.hx;
                step.hy = field.hy;
                step.wavelength_um = wave.value_um;
                step.px = edge.px;
                step.py = edge.py;
                step.target_stop_x = target_x;
                step.target_stop_y = target_y;
                step.seed_x = current_seed.x;
                step.seed_y = current_seed.y;
                step.guess_valid = current_ray.valid;
                step.trace_ok = trace_ok;
                step.stop_valid = stop_hit.valid;
                step.stop_x = stop_hit.x;
                step.stop_y = stop_hit.y;
                step.residual = residual;
                step.accepted = accepted;
                step.terminated = terminated;
                step.failure_code = current_ray.failure_code;
                step.failure_surface = current_ray.failure_surface;
                step.failure_severity = current_ray.failure_severity;
                step.trace_failure_reason = trace_failure_reason;
                step.trace_failure_surface = trace_failure_surface;
                step.reason = reason;
                probes.push_back(step);
            };

            if (!guess.valid) {
                TracePoint none{};
                append_step(0, seed, guess, false, none,
                            std::numeric_limits<double>::infinity(),
                            false, true, "initial_guess_invalid", "", -1);
                continue;
            }

            const double object_distance = system_.object_distance();
            const bool finite_object = std::isfinite(object_distance);
            const double delta_pos = 1.0e-4 * std::max(1.0, tracer_.entrance_pupil_radius(wave.value_um));
            const double delta_slope = 1.0e-5;
            const double delta = finite_object ? delta_slope : delta_pos;
            const double position_step_cap =
                finite_object ? 0.5 : infinite_object_aim_position_step_cap(system_, rs);
            double j11 = 1.0;
            double j12 = 0.0;
            double j21 = 0.0;
            double j22 = 1.0;
            bool has_jacobian = false;
            bool has_previous = false;
            double previous_cx = 0.0;
            double previous_cy = 0.0;
            double previous_ex = 0.0;
            double previous_ey = 0.0;
            double previous_error = std::numeric_limits<double>::infinity();
            Ray ray = guess;

            auto control = [&](const Ray& item) {
                if (finite_object) {
                    const double dz = std::max(kEps, std::abs(item.d.z));
                    return std::pair<double, double>{item.d.x / dz, item.d.y / dz};
                }
                return std::pair<double, double>{item.p.x, item.p.y};
            };
            auto set_control = [&](Ray& item, double cx, double cy) {
                if (finite_object) {
                    item.d = normalize({cx, cy, 1.0});
                } else {
                    item.p.x = cx;
                    item.p.y = cy;
                }
            };
            auto trace_residual = [&](const Ray& item,
                                      double* ex_out,
                                      double* ey_out,
                                      TracePoint* stop_hit,
                                      const char** trace_failure_reason,
                                      int* trace_failure_surface) {
                TracePoint hit;
                Ray full_trace = item;
                tracer_.trace_ray(full_trace, nullptr);
                if (trace_failure_reason) {
                    *trace_failure_reason = ray_failure_code_probe_label(full_trace.failure_code);
                }
                if (trace_failure_surface) {
                    *trace_failure_surface = full_trace.failure_surface;
                }
                const bool trace_ok = tracer_.trace_to_surface(item, si, &hit, true, true);
                if (stop_hit) {
                    *stop_hit = hit;
                }
                if (!trace_ok || !hit.valid) {
                    return false;
                }
                *ex_out = hit.x - target_x;
                *ey_out = hit.y - target_y;
                return true;
            };
            auto refresh_jacobian = [&](double ex, double ey) {
                const auto [cx, cy] = control(ray);
                Ray rx = ray;
                Ray ry = ray;
                set_control(rx, cx + delta, cy);
                set_control(ry, cx, cy + delta);
                double ex_x = ex;
                double ey_x = ey;
                double ex_y = ex;
                double ey_y = ey;
                TracePoint hit_x;
                TracePoint hit_y;
                const char* fail_reason_x = "";
                const char* fail_reason_y = "";
                int fail_surface_x = -1;
                int fail_surface_y = -1;
                const bool ok_x = trace_residual(
                    rx, &ex_x, &ey_x, &hit_x, &fail_reason_x, &fail_surface_x);
                const bool ok_y = trace_residual(
                    ry, &ex_y, &ey_y, &hit_y, &fail_reason_y, &fail_surface_y);
                if (!ok_x || !ok_y) {
                    j11 = 1.0;
                    j21 = 0.0;
                    j12 = 0.0;
                    j22 = 1.0;
                    has_jacobian = true;
                    return false;
                }
                j11 = (ex_x - ex) / delta;
                j21 = (ey_x - ey) / delta;
                j12 = (ex_y - ex) / delta;
                j22 = (ey_y - ey) / delta;
                has_jacobian = true;
                return true;
            };

            bool terminated = false;
            for (int iter = 0; iter < max_iterations; ++iter) {
                double ex = 0.0;
                double ey = 0.0;
                TracePoint stop_hit{};
                const char* trace_failure_reason = "";
                int trace_failure_surface = -1;
                const bool trace_ok = trace_residual(
                    ray, &ex, &ey, &stop_hit, &trace_failure_reason, &trace_failure_surface);
                const double error = trace_ok ? (ex * ex + ey * ey)
                                              : std::numeric_limits<double>::infinity();
                const double residual =
                    trace_ok ? std::sqrt(std::max(0.0, error))
                             : std::numeric_limits<double>::infinity();
                const FirstSurfaceSeed current_seed = seed_from_ray(ray);
                const bool accepted = trace_ok && std::isfinite(residual) && residual <= residual_limit;
                append_step(
                    iter,
                    current_seed,
                    ray,
                    trace_ok,
                    stop_hit,
                    residual,
                    accepted,
                    false,
                    trace_ok ? "trace_ok" : "trace_failed",
                    trace_failure_reason,
                    trace_failure_surface);
                if (!trace_ok) {
                    probes.back().terminated = true;
                    probes.back().reason = "trace_residual_failed";
                    terminated = true;
                    break;
                }
                if (accepted) {
                    probes.back().terminated = true;
                    probes.back().reason = "accepted";
                    terminated = true;
                    break;
                }
                const auto [cx, cy] = control(ray);

                if (has_jacobian && has_previous) {
                    const double sx = cx - previous_cx;
                    const double sy = cy - previous_cy;
                    const double ss = sx * sx + sy * sy;
                    if (ss > 1.0e-24 && error <= previous_error * 16.0) {
                        const double yx = ex - previous_ex;
                        const double yy = ey - previous_ey;
                        const double js_x = j11 * sx + j12 * sy;
                        const double js_y = j21 * sx + j22 * sy;
                        const double ux = (yx - js_x) / ss;
                        const double uy = (yy - js_y) / ss;
                        j11 += ux * sx;
                        j12 += ux * sy;
                        j21 += uy * sx;
                        j22 += uy * sy;
                    } else {
                        has_jacobian = false;
                    }
                }

                bool jacobian_refreshed = true;
                if (!has_jacobian) {
                    jacobian_refreshed = refresh_jacobian(ex, ey);
                }
                double det = j11 * j22 - j12 * j21;
                if (std::abs(det) <= 1.0e-14 || !std::isfinite(det)) {
                    jacobian_refreshed = refresh_jacobian(ex, ey);
                    det = j11 * j22 - j12 * j21;
                }
                if (std::abs(det) <= 1.0e-14 || !std::isfinite(det)) {
                    append_step(
                        iter,
                        current_seed,
                        ray,
                        true,
                        stop_hit,
                        residual,
                        false,
                        true,
                        jacobian_refreshed ? "singular_after_refresh" : "jacobian_refresh_failed",
                        "",
                        -1);
                    terminated = true;
                    break;
                }
                const double dx = -(j22 * ex - j12 * ey) / det;
                const double dy = -(-j21 * ex + j11 * ey) / det;
                previous_cx = cx;
                previous_cy = cy;
                previous_ex = ex;
                previous_ey = ey;
                previous_error = error;
                has_previous = true;
                if (finite_object) {
                    set_control(ray, cx + std::clamp(dx, -0.5, 0.5), cy + std::clamp(dy, -0.5, 0.5));
                } else {
                    set_control(
                        ray,
                        cx + std::clamp(dx, -position_step_cap, position_step_cap),
                        cy + std::clamp(dy, -position_step_cap, position_step_cap));
                }
            }

            if (!terminated) {
                double ex = 0.0;
                double ey = 0.0;
                TracePoint stop_hit{};
                const char* trace_failure_reason = "";
                int trace_failure_surface = -1;
                const bool trace_ok = trace_residual(
                    ray, &ex, &ey, &stop_hit, &trace_failure_reason, &trace_failure_surface);
                const double residual =
                    trace_ok ? std::sqrt(std::max(0.0, ex * ex + ey * ey))
                             : std::numeric_limits<double>::infinity();
                append_step(
                    max_iterations,
                    seed_from_ray(ray),
                    ray,
                    trace_ok,
                    stop_hit,
                    residual,
                    trace_ok && residual <= residual_limit,
                    true,
                    trace_ok ? "max_iterations" : "final_trace_failed",
                    trace_failure_reason,
                    trace_failure_surface);
            }
        }
    }
    return probes;
}

} // namespace optiland
