// Solve iterative STOP aiming and evaluate the paired Jacobian traces used by that solve.
// SequentialRayTracer and session helpers call these bounded seed-to-STOP operations.
// Preserve paired-worker ownership, finite-difference order, damping decisions, and accepted residuals.

#include "aiming_geometry_inline.hpp"
#include "diagnostics_inline.hpp"
#include "prepared_geometry_inline.hpp"
#include "trace_diagnostics_inline.hpp"
#include "detail.hpp"
#include "stop_aim_worker.hpp"

namespace optiland {
using namespace raytrace_detail;

void SequentialRayTracer::trace_stop_aim_jacobian_pair(
    Ray ray_x,
    Ray ray_y,
    std::size_t surface_index,
    bool ignore_intermediate_apertures,
    bool count_stop_aim_trace,
    TracePoint* point_x,
    TracePoint* point_y,
    bool* ok_x,
    bool* ok_y) const {
    auto trace_one = [&](Ray ray, TracePoint* point, long long* elapsed_ns) {
        const auto started = std::chrono::steady_clock::now();
        const bool ok = trace_to_surface(
            ray,
            surface_index,
            point,
            true,
            ignore_intermediate_apertures);
        if (elapsed_ns) {
            *elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started)
                              .count();
        }
        return ok;
    };

    long long elapsed_x_ns = 0;
    long long elapsed_y_ns = 0;
    const auto pair_started = std::chrono::steady_clock::now();
    bool local_ok_x = false;
    bool local_ok_y = false;
    constexpr std::size_t kParallelProbePairs = 64;
    constexpr std::size_t kParallelValidationPairs = 256;
    constexpr std::size_t kSerialReprobePairs = 4096;
    constexpr long long kParallelMinimumTraceNs = 8000;
    const bool adaptive_parallel_requested =
        stop_aim_parallel_jacobian_enabled();
    bool parallel_pair =
        adaptive_parallel_requested &&
        stop_aim_parallel_jacobian_decision_ == 2;
    if (parallel_pair) {
        if (!parallel_stop_aim_trace_worker_) {
            parallel_stop_aim_trace_worker_ =
                std::make_shared<ParallelStopAimTraceWorker>(system_);
        }
        parallel_pair = parallel_stop_aim_trace_worker_->available();
    }
    if (parallel_pair) {
        parallel_stop_aim_trace_worker_->submit(
            std::move(ray_y),
            surface_index,
            ignore_intermediate_apertures);
        local_ok_x = trace_one(std::move(ray_x), point_x, &elapsed_x_ns);
        local_ok_y = parallel_stop_aim_trace_worker_->wait(
            point_y, &elapsed_y_ns);
    } else {
        local_ok_x = trace_one(std::move(ray_x), point_x, &elapsed_x_ns);
        local_ok_y = trace_one(std::move(ray_y), point_y, &elapsed_y_ns);
        if (adaptive_parallel_requested &&
            stop_aim_parallel_jacobian_decision_ == 0) {
            ++stop_aim_parallel_jacobian_probe_pairs_;
            stop_aim_parallel_jacobian_probe_trace_ns_ +=
                elapsed_x_ns + elapsed_y_ns;
            if (stop_aim_parallel_jacobian_probe_pairs_ >= kParallelProbePairs) {
                const long long mean_trace_ns =
                    stop_aim_parallel_jacobian_probe_trace_ns_ /
                    static_cast<long long>(
                        2 * stop_aim_parallel_jacobian_probe_pairs_);
                stop_aim_parallel_jacobian_decision_ =
                    mean_trace_ns >= kParallelMinimumTraceNs ? 2 : 1;
                stop_aim_parallel_jacobian_probe_pairs_ = 0;
                stop_aim_parallel_jacobian_probe_trace_ns_ = 0;
                stop_aim_parallel_jacobian_probe_wall_ns_ = 0;
                stop_aim_parallel_jacobian_decision_pairs_ = 0;
            }
        }
    }

    const long long pair_wall_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - pair_started)
            .count();
    if (adaptive_parallel_requested) {
        if (parallel_pair) {
            ++stop_aim_parallel_jacobian_probe_pairs_;
            stop_aim_parallel_jacobian_probe_trace_ns_ +=
                elapsed_x_ns + elapsed_y_ns;
            stop_aim_parallel_jacobian_probe_wall_ns_ += pair_wall_ns;
            if (stop_aim_parallel_jacobian_probe_pairs_ >=
                kParallelValidationPairs) {
                // Keep the worker only while measured wall time is at least
                // five percent below executing both prefixes serially.
                stop_aim_parallel_jacobian_decision_ =
                    100 * stop_aim_parallel_jacobian_probe_wall_ns_ <
                            95 * stop_aim_parallel_jacobian_probe_trace_ns_
                        ? 2
                        : 1;
                stop_aim_parallel_jacobian_probe_pairs_ = 0;
                stop_aim_parallel_jacobian_probe_trace_ns_ = 0;
                stop_aim_parallel_jacobian_probe_wall_ns_ = 0;
                stop_aim_parallel_jacobian_decision_pairs_ = 0;
            }
        } else if (stop_aim_parallel_jacobian_decision_ == 1) {
            ++stop_aim_parallel_jacobian_decision_pairs_;
            if (stop_aim_parallel_jacobian_decision_pairs_ >=
                kSerialReprobePairs) {
                stop_aim_parallel_jacobian_decision_ = 0;
                stop_aim_parallel_jacobian_probe_pairs_ = 0;
                stop_aim_parallel_jacobian_probe_trace_ns_ = 0;
                stop_aim_parallel_jacobian_probe_wall_ns_ = 0;
                stop_aim_parallel_jacobian_decision_pairs_ = 0;
            }
        }
    }

    auto& diag = diagnostics_state();
    add_diag(diag.stop_residual_eval_calls, 2);
    add_diag(diag.stop_residual_eval_ns, elapsed_x_ns + elapsed_y_ns);
    add_diag(diag.stop_residual_eval_successes,
             static_cast<long long>(local_ok_x) + static_cast<long long>(local_ok_y));
    if (count_stop_aim_trace) {
        add_diag(diag.stop_aim_trace_residual_calls, 2);
        add_diag(diag.stop_aim_trace_residual_ns, elapsed_x_ns + elapsed_y_ns);
        add_diag(
            diag.stop_aim_trace_residual_successes,
            static_cast<long long>(local_ok_x) + static_cast<long long>(local_ok_y));
    }
    if (parallel_pair) {
        add_diag(diag.stop_aim_parallel_jacobian_pairs);
        add_diag(
            diag.stop_aim_parallel_jacobian_ns,
            pair_wall_ns);
    }
    if (ok_x) {
        *ok_x = local_ok_x && point_x && point_x->valid;
    }
    if (ok_y) {
        *ok_y = local_ok_y && point_y && point_y->valid;
    }
}

bool ray_aiming_uses_session(RayAimingMode mode) {
    return mode == RayAimingMode::Robust ||
           mode == RayAimingMode::AdaptivePilotGrid ||
           mode == RayAimingMode::CalibratedParaxial ||
           mode == RayAimingMode::ThreeStep;
}

Ray SequentialRayTracer::solve_stop_aim_from_initial(
    const Field& field,
    const PupilPoint& pupil,
    double wavelength_um,
    Ray initial,
    bool strict_failure,
    int max_iterations_override,
    bool ignore_intermediate_apertures,
    bool honor_first_surface_seed,
    double first_surface_seed_x,
    double first_surface_seed_y) const {
    const std::size_t si = stop_index();
    const double rs = stop_target_radius(wavelength_um, RayAimingMode::AdaptivePilotGrid);
    const double object_distance = system_.object_distance();
    const bool finite_object = std::isfinite(object_distance);
    const std::size_t first_aiming_surface =
        system_.first_ray_aiming_surface(wavelength_um);
    const bool can_use_first_surface_seed_control =
        honor_first_surface_seed &&
        !finite_object &&
        first_aiming_surface == system_.first_optical_surface() &&
        first_aiming_surface < system_.surfaces.size() &&
        std::isfinite(first_surface_seed_x) &&
        std::isfinite(first_surface_seed_y);
    bool use_first_surface_seed_control = can_use_first_surface_seed_control;
    double first_surface_seed_backstep = 0.0;
    if (can_use_first_surface_seed_control) {
        const Surface& first_surface = system_.surfaces[first_aiming_surface];
        const double first_surface_scale = std::max({
            1.0,
            std::abs(first_surface.aperture_radius),
            std::abs(first_surface.semi_aperture)});
        first_surface_seed_backstep = 1.0e-4 * first_surface_scale;
    }
    auto ray_from_first_surface_seed = [&](double seed_x, double seed_y, double weight) {
        Ray seeded = generate_ray_through_surface_point(
            field,
            wavelength_um,
            first_aiming_surface,
            seed_x,
            seed_y,
            weight);
        if (!use_first_surface_seed_control) {
            return seeded;
        }
        const Surface& first_surface = system_.surfaces[first_aiming_surface];
        const double sag = surface_sag(first_surface, seed_x, seed_y);
        seeded.d = field_direction_cosines(system_, field);
        seeded.p.x = seed_x - seeded.d.x * first_surface_seed_backstep;
        seeded.p.y = seed_y - seeded.d.y * first_surface_seed_backstep;
        if (!std::isfinite(sag)) {
            seeded.p.z = 0.0;
            mark_ray_failure(
                seeded,
                RayFailureCode::SagInvalid,
                static_cast<int>(first_aiming_surface),
                1.0);
            return seeded;
        }
        const Vec3 target{
            seed_x,
            seed_y,
            surface_vertex_z(first_aiming_surface) + sag};
        seeded.p = target - seeded.d * first_surface_seed_backstep;
        seeded.valid =
            std::isfinite(seeded.p.x) &&
            std::isfinite(seeded.p.y) &&
            std::isfinite(seeded.p.z);
        if (!seeded.valid) {
            mark_ray_failure(seeded, RayFailureCode::GeometryInvalid, -1, 1.0);
        }
        return seeded;
    };
    const double pxf = field.fvcx + pupil.px * (1.0 - field.fvdx);
    const double pyf = field.fvcy + pupil.py * (1.0 - field.fvdy);
    const double target_x = pxf * rs;
    const double target_y = pyf * rs;
    Ray ray = initial;
    ray.wavelength_um = wavelength_um;
    ray.nominal_weight = std::max(0.0, pupil.weight);
    if (ray.intensity > 0.0) {
        ray.intensity = ray.nominal_weight;
    }

    const double delta_pos = 1.0e-4 * std::max(1.0, entrance_pupil_radius(wavelength_um));
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
    Ray best_ray = ray;
    bool has_best_ray = false;
    double best_error = std::numeric_limits<double>::infinity();
    const int max_iterations = max_iterations_override > 0
                                   ? max_iterations_override
                                   : system_.ray_aiming.max_iter;
    const double first_surface_seed_control_offset_x =
        can_use_first_surface_seed_control
            ? first_surface_seed_x - initial.p.x
            : 0.0;
    const double first_surface_seed_control_offset_y =
        can_use_first_surface_seed_control
            ? first_surface_seed_y - initial.p.y
            : 0.0;

    auto control = [&](const Ray& item) {
        if (finite_object) {
            const double dz = std::max(kEps, std::abs(item.d.z));
            return std::pair<double, double>{item.d.x / dz, item.d.y / dz};
        }
        if (use_first_surface_seed_control) {
            return std::pair<double, double>{
                item.p.x + item.d.x * first_surface_seed_backstep,
                item.p.y + item.d.y * first_surface_seed_backstep};
        }
        return std::pair<double, double>{item.p.x, item.p.y};
    };
    auto set_control = [&](Ray& item, double cx, double cy) {
        if (finite_object) {
            item.d = normalize({cx, cy, 1.0});
        } else if (use_first_surface_seed_control) {
            item = ray_from_first_surface_seed(cx, cy, item.nominal_weight);
        } else {
            item.p.x = cx;
            item.p.y = cy;
        }
    };
    auto activate_first_surface_seed_control = [&](Ray& candidate) {
        if (!can_use_first_surface_seed_control || use_first_surface_seed_control) {
            return false;
        }
        const Surface& first_surface = system_.surfaces[first_aiming_surface];
        const double clip_radius =
            effective_aperture_clip_radius(first_aiming_surface, wavelength_um);
        if (!(clip_radius > 0.0)) {
            return false;
        }
        Ray local = candidate;
        local.p.z -= surface_vertex_z(first_aiming_surface);
        double t = 0.0;
        if (!intersect_surface(first_surface, local, &t)) {
            return false;
        }
        const double hit_x = local.p.x + local.d.x * t;
        const double hit_y = local.p.y + local.d.y * t;
        if (!std::isfinite(hit_x) ||
            !std::isfinite(hit_y) ||
            !aperture_clip_exceeded(std::hypot(hit_x, hit_y), clip_radius)) {
            return false;
        }
        const double seed_x = candidate.p.x + first_surface_seed_control_offset_x;
        const double seed_y = candidate.p.y + first_surface_seed_control_offset_y;
        if (!std::isfinite(seed_x) || !std::isfinite(seed_y)) {
            return false;
        }
        use_first_surface_seed_control = true;
        if (has_previous) {
            previous_cx += first_surface_seed_control_offset_x;
            previous_cy += first_surface_seed_control_offset_y;
        }
        candidate = ray_from_first_surface_seed(seed_x, seed_y, candidate.nominal_weight);
        return true;
    };
    auto trace_residual = [&](const Ray& item, double* ex_out, double* ey_out) {
        TracePoint point;
        auto& diag = diagnostics_state();
        add_diag(diag.stop_residual_eval_calls);
        add_diag(diag.stop_aim_trace_residual_calls);
        bool trace_ok = false;
        {
            ScopedDiagTimer eval_timer(diag.stop_residual_eval_ns);
            ScopedDiagTimer trace_timer(diag.stop_aim_trace_residual_ns);
            trace_ok = trace_to_surface(item, si, &point, true, ignore_intermediate_apertures);
        }
        if (!trace_ok || !point.valid) {
            return false;
        }
        add_diag(diag.stop_residual_eval_successes);
        add_diag(diag.stop_aim_trace_residual_successes);
        *ex_out = point.x - target_x;
        *ey_out = point.y - target_y;
        return true;
    };
    auto remember_traceable = [&](const Ray& candidate, double error) {
        if (!has_best_ray || error < best_error) {
            best_ray = candidate;
            best_ray.valid = true;
            best_error = error;
            has_best_ray = true;
        }
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
        TracePoint point_x;
        TracePoint point_y;
        bool ok_x = false;
        bool ok_y = false;
        trace_stop_aim_jacobian_pair(
            std::move(rx),
            std::move(ry),
            si,
            ignore_intermediate_apertures,
            true,
            &point_x,
            &point_y,
            &ok_x,
            &ok_y);
        if (ok_x) {
            ex_x = point_x.x - target_x;
            ey_x = point_x.y - target_y;
        }
        if (ok_y) {
            ex_y = point_y.x - target_x;
            ey_y = point_y.y - target_y;
        }
        if (!ok_x || !ok_y) {
            j11 = 1.0;
            j21 = 0.0;
            j12 = 0.0;
            j22 = 1.0;
            has_jacobian = true;
            return;
        }
        j11 = (ex_x - ex) / delta;
        j21 = (ey_x - ey) / delta;
        j12 = (ex_y - ex) / delta;
        j22 = (ey_y - ey) / delta;
        has_jacobian = true;
    };
    auto stamp = [&](Ray out, bool converged, double error, int iterations) {
        out.aim_converged = converged;
        out.aim_residual = std::isfinite(error)
                               ? std::sqrt(std::max(0.0, error))
                               : std::numeric_limits<double>::infinity();
        out.aim_target_radius = rs;
        out.homotopy_fraction = 1.0;
        out.aim_iterations = iterations;
        if (!converged) {
            const double scale = std::max(1.0e-9, std::abs(rs));
            mark_ray_failure(
                out,
                RayFailureCode::AimNotConverged,
                static_cast<int>(si),
                out.aim_residual / scale,
                strict_failure);
        }
        return out;
    };
    // AdaptivePilotGrid supplies robust multi-start seeds, but a full Newton
    // step can still leave a narrow traceable corridor. Backtrack only after a
    // failed or non-decreasing trial, so the normal improving path performs
    // exactly the same residual traces as before.
    constexpr int kFailureBacktrackTrials = 10;
    auto recover_rejected_step = [&](const Ray& rejected, double* ex_out, double* ey_out) {
        if (!has_previous || !ex_out || !ey_out || !std::isfinite(previous_error)) {
            return false;
        }
        const auto [rejected_cx, rejected_cy] = control(rejected);
        double fraction = 0.5;
        for (int trial = 0; trial < kFailureBacktrackTrials; ++trial, fraction *= 0.5) {
            Ray candidate = rejected;
            set_control(
                candidate,
                previous_cx + fraction * (rejected_cx - previous_cx),
                previous_cy + fraction * (rejected_cy - previous_cy));
            double candidate_ex = 0.0;
            double candidate_ey = 0.0;
            if (!trace_residual(candidate, &candidate_ex, &candidate_ey)) {
                continue;
            }
            const double candidate_error =
                candidate_ex * candidate_ex + candidate_ey * candidate_ey;
            remember_traceable(candidate, candidate_error);
            if (!std::isfinite(candidate_error) || candidate_error >= previous_error) {
                continue;
            }
            ray = candidate;
            *ex_out = candidate_ex;
            *ey_out = candidate_ey;
            return true;
        }
        return false;
    };

    for (int iter = 0; iter < max_iterations; ++iter) {
        double ex = 0.0;
        double ey = 0.0;
        bool trace_ok = trace_residual(ray, &ex, &ey);
        if (!trace_ok && activate_first_surface_seed_control(ray)) {
            trace_ok = trace_residual(ray, &ex, &ey);
        }
        if (!trace_ok) {
            if (!recover_rejected_step(ray, &ex, &ey)) {
                if (has_best_ray) {
                    return stamp(best_ray, false, best_error, iter + 1);
                }
                return stamp(ray, false, std::numeric_limits<double>::infinity(), iter + 1);
            }
        }
        double error = ex * ex + ey * ey;
        if (has_previous &&
            std::isfinite(error) &&
            error >= previous_error) {
            double recovered_ex = ex;
            double recovered_ey = ey;
            if (recover_rejected_step(ray, &recovered_ex, &recovered_ey)) {
                ex = recovered_ex;
                ey = recovered_ey;
                error = ex * ex + ey * ey;
            }
        }
        remember_traceable(ray, error);
        if (error < system_.ray_aiming.tolerance * system_.ray_aiming.tolerance) {
            return stamp(ray, true, error, iter + 1);
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

        if (!has_jacobian) {
            refresh_jacobian(ex, ey);
        }
        double det = j11 * j22 - j12 * j21;
        if (std::abs(det) <= 1.0e-14 || !std::isfinite(det)) {
            refresh_jacobian(ex, ey);
            det = j11 * j22 - j12 * j21;
        }
        if (std::abs(det) <= 1.0e-14 || !std::isfinite(det)) {
            j11 = 1.0;
            j22 = 1.0;
            j12 = 0.0;
            j21 = 0.0;
            det = 1.0;
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

    double ex = 0.0;
    double ey = 0.0;
    if (trace_residual(ray, &ex, &ey)) {
        const double error = ex * ex + ey * ey;
        remember_traceable(ray, error);
        if (error < system_.ray_aiming.tolerance * system_.ray_aiming.tolerance) {
            return stamp(ray, true, error, max_iterations);
        }
    }
    if (has_best_ray) {
        return stamp(best_ray, false, best_error, max_iterations);
    }
    return stamp(ray, false, std::numeric_limits<double>::infinity(), max_iterations);
}

Ray SequentialRayTracer::aim_iterative(
    const Field& field,
    const PupilPoint& pupil,
    double wavelength_um,
    bool robust) const {
    const std::size_t si = stop_index();
    const RayAimingMode aiming_mode = robust ? RayAimingMode::Robust : RayAimingMode::Iterative;
    const double rs = stop_target_radius(wavelength_um, aiming_mode);
    const double object_distance = system_.object_distance();
    const bool finite_object = std::isfinite(object_distance);
    struct AimSolveResult {
        Ray ray;
        bool converged = false;
        double best_error = std::numeric_limits<double>::infinity();
        int iterations = 0;
        double homotopy_fraction = 1.0;
    };
    auto solve_once = [&](const Field& f, const PupilPoint& p, Ray initial) -> AimSolveResult {
        const double pxf = f.fvcx + p.px * (1.0 - f.fvdx);
        const double pyf = f.fvcy + p.py * (1.0 - f.fvdy);
        const double target_x = pxf * rs;
        const double target_y = pyf * rs;
        Ray ray = initial;
        const double delta_pos = 1.0e-4 * std::max(1.0, entrance_pupil_radius(wavelength_um));
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
        Ray best_ray = ray;
        bool has_best_ray = false;
        double best_error = std::numeric_limits<double>::infinity();

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
        auto trace_residual = [&](const Ray& item, double* ex_out, double* ey_out) {
            TracePoint point;
            auto& diag = diagnostics_state();
            add_diag(diag.stop_residual_eval_calls);
            bool trace_ok = false;
            {
                ScopedDiagTimer eval_timer(diag.stop_residual_eval_ns);
                trace_ok = trace_to_surface(item, si, &point, true);
            }
            if (!trace_ok || !point.valid) {
                return false;
            }
            add_diag(diag.stop_residual_eval_successes);
            *ex_out = point.x - target_x;
            *ey_out = point.y - target_y;
            return true;
        };
        auto remember_traceable = [&](const Ray& candidate, double error) {
            if (!has_best_ray || error < best_error) {
                best_ray = candidate;
                best_ray.valid = true;
                best_error = error;
                has_best_ray = true;
            }
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
            TracePoint point_x;
            TracePoint point_y;
            bool ok_x = false;
            bool ok_y = false;
            trace_stop_aim_jacobian_pair(
                std::move(rx),
                std::move(ry),
                si,
                false,
                false,
                &point_x,
                &point_y,
                &ok_x,
                &ok_y);
            if (ok_x) {
                ex_x = point_x.x - target_x;
                ey_x = point_x.y - target_y;
            }
            if (ok_y) {
                ex_y = point_y.x - target_x;
                ey_y = point_y.y - target_y;
            }
            if (!ok_x || !ok_y) {
                j11 = 1.0;
                j21 = 0.0;
                j12 = 0.0;
                j22 = 1.0;
                has_jacobian = true;
                return;
            }
            j11 = (ex_x - ex) / delta;
            j21 = (ey_x - ey) / delta;
            j12 = (ex_y - ex) / delta;
            j22 = (ey_y - ey) / delta;
            has_jacobian = true;
        };

        for (int iter = 0; iter < system_.ray_aiming.max_iter; ++iter) {
            double ex = 0.0;
            double ey = 0.0;
            if (!trace_residual(ray, &ex, &ey)) {
                if (has_best_ray) {
                    return {best_ray, false, best_error, iter + 1, 1.0};
                }
                mark_ray_failure(ray, RayFailureCode::SurfaceMiss, static_cast<int>(si), 1.0);
                return {ray, false, std::numeric_limits<double>::infinity(), iter + 1, 1.0};
            }
            const double error = ex * ex + ey * ey;
            remember_traceable(ray, error);
            if (error < system_.ray_aiming.tolerance * system_.ray_aiming.tolerance) {
                return {ray, true, error, iter + 1, 1.0};
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

            if (!has_jacobian) {
                refresh_jacobian(ex, ey);
            }
            double det = j11 * j22 - j12 * j21;
            if (std::abs(det) <= 1.0e-14 || !std::isfinite(det)) {
                refresh_jacobian(ex, ey);
                det = j11 * j22 - j12 * j21;
            }
            if (std::abs(det) <= 1.0e-14 || !std::isfinite(det)) {
                j11 = 1.0;
                j22 = 1.0;
                j12 = 0.0;
                j21 = 0.0;
                det = 1.0;
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
        double ex = 0.0;
        double ey = 0.0;
        if (trace_residual(ray, &ex, &ey)) {
            const double error = ex * ex + ey * ey;
            remember_traceable(ray, error);
            if (error < system_.ray_aiming.tolerance * system_.ray_aiming.tolerance) {
                return {ray, true, error, system_.ray_aiming.max_iter, 1.0};
            }
        }
        if (has_best_ray) {
            return {best_ray, false, best_error, system_.ray_aiming.max_iter, 1.0};
        }
        mark_ray_failure(ray, RayFailureCode::SurfaceMiss, static_cast<int>(si), 1.0);
        return {ray, false, std::numeric_limits<double>::infinity(), system_.ray_aiming.max_iter, 1.0};
    };

    Ray initial = generate_paraxial_ray(field, pupil, wavelength_um);
    const bool strict_stop_aiming = robust && system_.ray_aiming.require_convergence;
    auto stamp_result = [&](AimSolveResult result, double fraction) {
        result.ray.aim_converged = result.converged;
        result.ray.aim_residual = std::isfinite(result.best_error)
                                      ? std::sqrt(std::max(0.0, result.best_error))
                                      : std::numeric_limits<double>::infinity();
        result.ray.aim_target_radius = rs;
        result.ray.homotopy_fraction = fraction;
        result.ray.aim_iterations = result.iterations;
        if (!result.converged) {
            const double scale = std::max(1.0e-9, std::abs(rs));
            mark_ray_failure(
                result.ray,
                RayFailureCode::AimNotConverged,
                static_cast<int>(si),
                result.ray.aim_residual / scale,
                strict_stop_aiming);
        }
        return result.ray;
    };
    if (!robust) {
        AimSolveResult result = solve_once(field, pupil, initial);
        return stamp_result(result, 1.0);
    }

    auto scaled_field = [&](double t) {
        Field out = field;
        out.hx *= t;
        out.hy *= t;
        out.fvcx *= t;
        out.fvcy *= t;
        out.fvdx *= t;
        out.fvdy *= t;
        return out;
    };
    auto scaled_pupil = [&](double t) {
        return PupilPoint{pupil.px * t, pupil.py * t, pupil.weight};
    };
    auto predictor_guess = [&](const Ray& solved0, const Ray& paraxial0, const Ray& paraxial1) {
        Ray guess = paraxial1;
        guess.p.x = paraxial1.p.x + (solved0.p.x - paraxial0.p.x);
        guess.p.y = paraxial1.p.y + (solved0.p.y - paraxial0.p.y);
        guess.p.z = paraxial1.p.z + (solved0.p.z - paraxial0.p.z);
        if (finite_object) {
            double gx = paraxial1.d.x + (solved0.d.x - paraxial0.d.x);
            double gy = paraxial1.d.y + (solved0.d.y - paraxial0.d.y);
            double transverse = gx * gx + gy * gy;
            if (transverse > 1.0) {
                const double scale = std::sqrt(transverse);
                gx /= scale;
                gy /= scale;
                transverse = gx * gx + gy * gy;
            }
            const double gz_abs = std::sqrt(std::max(0.0, 1.0 - transverse));
            const double gz = paraxial1.d.z >= 0.0 ? gz_abs : -gz_abs;
            guess.d = normalize({gx, gy, gz});
        } else {
            guess.d = paraxial1.d;
        }
        return guess;
    };

    Field f0 = scaled_field(0.0);
    PupilPoint p0 = scaled_pupil(0.0);
    Ray paraxial0 = generate_paraxial_ray(f0, p0, wavelength_um);
    AimSolveResult sol0{paraxial0, true, 0.0, 0, 0.0};
    constexpr double kRobustMinInterval = 1.0e-3;
    const int max_depth = std::max(12, system_.ray_aiming.continuation_steps + 4);
    auto solve_robust = [&](auto&& self,
                            double t0,
                            double t1,
                            AimSolveResult lower_solution,
                            Ray lower_paraxial,
                            int depth) -> AimSolveResult {
        if ((t1 - t0) < kRobustMinInterval || depth > max_depth) {
            lower_solution.converged = false;
            lower_solution.best_error = std::numeric_limits<double>::infinity();
            lower_solution.homotopy_fraction = t1;
            return lower_solution;
        }

        Field ft = scaled_field(t1);
        PupilPoint pt = scaled_pupil(t1);
        Ray paraxial1 = generate_paraxial_ray(ft, pt, wavelength_um);
        Ray guess = predictor_guess(lower_solution.ray, lower_paraxial, paraxial1);
        AimSolveResult trial = solve_once(ft, pt, guess);
        trial.homotopy_fraction = t1;
        if (trial.converged) {
            return trial;
        }

        const double tm = 0.5 * (t0 + t1);
        AimSolveResult middle_solution =
            self(self, t0, tm, lower_solution, lower_paraxial, depth + 1);
        Field fm = scaled_field(tm);
        PupilPoint pm = scaled_pupil(tm);
        Ray middle_paraxial = generate_paraxial_ray(fm, pm, wavelength_um);
        return self(self, tm, t1, middle_solution, middle_paraxial, depth + 1);
    };

    AimSolveResult robust_result = solve_robust(solve_robust, 0.0, 1.0, sol0, paraxial0, 0);
    return stamp_result(robust_result, robust_result.homotopy_fraction);
}

} // namespace optiland
