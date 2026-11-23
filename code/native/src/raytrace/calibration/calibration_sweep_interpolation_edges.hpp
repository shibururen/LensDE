#pragma once
#include "../aiming_geometry_inline.hpp"
#include "../diagnostics_inline.hpp"
#include "../detail.hpp"
#include "calibration_cached_same_field_edge_seed.hpp"

namespace optiland::raytrace_detail {

struct CalibrationSweepInterpolationEdges {
    using CalibratedParaxialModel = AimingSession::CalibratedParaxialModel;
    using FieldWaveKey = AimingSession::FieldWaveKey;
    using FirstSurfaceSeed = AimingSession::FirstSurfaceSeed;

    template <
        class Type_EdgeBackoffResult,
        class Capture_existing,
        class Capture_seed_in_first_surface_bounds,
        class Capture_edge_pupils,
        class Capture_center_footprint,
        class Capture_first_surface_edge_radius,
        class Capture_fixed_jacobian_edge_solver,
        class Capture_secant_edge_solver,
        class Capture_fixed_then_iterative_edge_solver,
        class Capture_secant_then_iterative_edge_solver,
        class Capture_commit_edge_stop_sample,
        class Capture_accept_edge_ray,
        class Capture_accept_reachable_secondary_edge,
        class Capture_solve_seed_space_edge,
        class Capture_relative_edge_scale,
        class Capture_try_scaled_edge_anchor
    >
    static auto run(
        AimingSession& session,
        std::size_t& field_index,
        std::size_t& wave_index,
        const Wavelength& wave,
        RayAimingDiagnosticsState& diag,
        Capture_existing& existing,
        const int& continuation_mode,
        bool& continuation_repair_build,
        CalibratedParaxialModel& model,
        const CalibratedParaxialConfig& cfg,
        const Field& field,
        const double& rs,
        Capture_seed_in_first_surface_bounds& seed_in_first_surface_bounds,
        Capture_edge_pupils& edge_pupils,
        Capture_center_footprint& center_footprint,
        const double& residual_limit,
        const int& max_iterations,
        Capture_first_surface_edge_radius& first_surface_edge_radius,
        Capture_fixed_jacobian_edge_solver& fixed_jacobian_edge_solver,
        Capture_secant_edge_solver& secant_edge_solver,
        Capture_fixed_then_iterative_edge_solver& fixed_then_iterative_edge_solver,
        Capture_secant_then_iterative_edge_solver& secant_then_iterative_edge_solver,
        Capture_commit_edge_stop_sample& commit_edge_stop_sample,
        Capture_accept_edge_ray& accept_edge_ray,
        Capture_accept_reachable_secondary_edge& accept_reachable_secondary_edge,
        Capture_solve_seed_space_edge& solve_seed_space_edge,
        Capture_relative_edge_scale& relative_edge_scale,
        Capture_try_scaled_edge_anchor& try_scaled_edge_anchor
    ) -> CalibratedParaxialModel* {
        if (cfg.edge_calibration_enabled && cfg.edge_calibration_iterations > 0) {
            std::array<double, 2> axis_scale_hints{{
                std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN(),
            }};
            double common_scale_hint = std::numeric_limits<double>::quiet_NaN();
            auto remember_scale_hint = [&](std::size_t axis,
                                           const FirstSurfaceSeed& full_seed,
                                           const FirstSurfaceSeed& accepted_seed) {
                const double scale = relative_edge_scale(full_seed, accepted_seed);
                if (!std::isfinite(scale) || scale <= 0.0) {
                    return;
                }
                if (axis < axis_scale_hints.size()) {
                    axis_scale_hints[axis] = scale;
                }
                common_scale_hint = scale;
            };
            for (std::size_t i = 0; i < edge_pupils.size(); ++i) {
                const PupilPoint& edge = edge_pupils[i];
                auto& anchor = model.edge_anchors[i];
                add_diag(diag.calibrated_edge_solve_calls);
                anchor.px = edge.px;
                anchor.py = edge.py;
                FirstSurfaceSeed seed;
                const bool use_transverse_center_radius =
                    center_footprint &&
                    center_footprint->edge_vectors[i].valid &&
                    std::abs(edge.py) > std::abs(edge.px);
                FirstSurfaceSeed canonical_full_seed;
                struct EdgeSeedCandidate {
                    FirstSurfaceSeed seed;
                    bool include_default_backoff = true;
                    bool use_transverse_scale_hints = false;
                    bool stop_after_traceable = false;
                };
                std::vector<EdgeSeedCandidate> full_seed_candidates;
                auto push_seed_candidate = [&](const FirstSurfaceSeed& candidate,
                                               bool include_default_backoff,
                                               bool use_transverse_scale_hints,
                                               bool stop_after_traceable) {
                    if (!candidate.valid ||
                        !std::isfinite(candidate.x) ||
                        !std::isfinite(candidate.y)) {
                        return;
                    }
                    for (const EdgeSeedCandidate& existing : full_seed_candidates) {
                        if (std::hypot(existing.seed.x - candidate.x,
                                       existing.seed.y - candidate.y) <=
                            1.0e-9) {
                            return;
                        }
                    }
                    full_seed_candidates.push_back({
                        candidate,
                        include_default_backoff,
                        use_transverse_scale_hints,
                        stop_after_traceable});
                };
                auto cached_same_field_edge_seed = [&](std::size_t edge_index) {
        return ::optiland::raytrace_detail::CalibrationCachedSameFieldEdgeSeed::run(session, field_index, wave_index, wave, continuation_mode, model, anchor, edge_index);
    };
                auto push_scale_hint = [](std::vector<double>& scales, double scale) {
                    if (!std::isfinite(scale) || scale <= 0.0) {
                        return;
                    }
                    const double clamped = std::clamp(scale, 1.0e-3, 1.25);
                    for (const double existing : scales) {
                        if (std::abs(existing - clamped) <=
                            1.0e-7 * std::max(1.0, std::abs(clamped))) {
                            return;
                        }
                    }
                    scales.push_back(clamped);
                };
                auto push_damped_scale_hint = [&](std::vector<double>& scales,
                                                  double scale) {
                    push_scale_hint(scales, scale);
                    push_scale_hint(scales, 0.99 * scale);
                    push_scale_hint(scales, 0.98 * scale);
                };
                push_seed_candidate(
                    cached_same_field_edge_seed(i),
                    false,
                    false,
                    true);
                if (use_transverse_center_radius) {
                    const FirstSurfaceSeed& footprint_vector = center_footprint->edge_vectors[i];
                    const double center_beam_radius =
                        std::hypot(footprint_vector.x, footprint_vector.y);
                    canonical_full_seed = session.shifted_chief_seed_by_transverse_radius(
                        field,
                        wave,
                        model.chief_seed,
                        edge,
                        center_beam_radius);
                    std::vector<double> transverse_radius_scales;
                    const std::size_t transverse_axis_index =
                        std::abs(edge.px) >= std::abs(edge.py) ? 0 : 1;
                    push_damped_scale_hint(
                        transverse_radius_scales,
                        axis_scale_hints[transverse_axis_index]);
                    if (std::isfinite(axis_scale_hints[transverse_axis_index])) {
                        push_damped_scale_hint(
                            transverse_radius_scales,
                            common_scale_hint);
                    }
                    push_scale_hint(transverse_radius_scales, 1.0);
                    push_scale_hint(transverse_radius_scales, 0.5);
                    push_scale_hint(transverse_radius_scales, 0.25);
                    push_scale_hint(transverse_radius_scales, 0.125);
                    for (const double radius_scale : transverse_radius_scales) {
                        push_seed_candidate(session.shifted_chief_seed_by_transverse_radius(
                            field,
                            wave,
                            model.chief_seed,
                            edge,
                            center_beam_radius * radius_scale),
                                            false,
                                            true,
                                            false);
                    }
                } else {
                    canonical_full_seed = {
                        model.chief_seed.x + first_surface_edge_radius * edge.px,
                        model.chief_seed.y + first_surface_edge_radius * edge.py,
                        true};
                    push_seed_candidate(canonical_full_seed,
                                        true,
                                        false,
                                        false);
                }
                const std::size_t axis_index =
                    std::abs(edge.px) >= std::abs(edge.py) ? 0 : 1;
                std::vector<double> scale_hints;
                if (std::isfinite(axis_scale_hints[axis_index])) {
                    push_damped_scale_hint(scale_hints, axis_scale_hints[axis_index]);
                }
                if (std::isfinite(common_scale_hint)) {
                    push_damped_scale_hint(scale_hints, common_scale_hint);
                }
                Type_EdgeBackoffResult backoff;
                FirstSurfaceSeed backoff_full_seed;
                bool backoff_accepted = false;
                bool have_backoff_seed = false;
                // Experimental matched CPU/GPU continuation. Cold and failed
                // predictions retain the complete original candidate/fallback path.
                const int warm_mode = continuation_mode;
                if (i==1 && (warm_mode&512) && (warm_mode&8) && !continuation_repair_build) {
                    const auto& from=model.edge_anchors[0];
                    FirstSurfaceSeed reflected=from.seed;
                    reflected.x=-reflected.x;
                    const double tx=(field.fvcx+edge.px*(1.0-field.fvdx))*rs;
                    const double ty=(field.fvcy+edge.py*(1.0-field.fvdy))*rs;
                    const double residual=std::hypot(-from.stop_x-tx,from.stop_y-ty);
                    if (field.hx==0.0 && field.fvcx==0.0 && model.chief_seed.x==0.0 &&
                        from.valid && from.px==1.0 && from.py==0.0 &&
                        std::isfinite(residual) && residual<=residual_limit &&
                        seed_in_first_surface_bounds(reflected)) {
                        commit_edge_stop_sample(anchor,reflected,residual,-from.stop_x,from.stop_y);
                        remember_scale_hint(axis_index,canonical_full_seed,anchor.seed);
                        continue;
                    }
                }
                const bool warm_enabled=(i==0 && (warm_mode&512)) ||
                    (i>=2 && (warm_mode&1024));
                if (warm_enabled && !continuation_repair_build && field_index>=2 && field_index<session.system_.fields.size() &&
                    wave.primary && session.system_.primary_wavelength_apertures && secant_edge_solver) {
                    auto p1=session.calibrated_models_.find(FieldWaveKey{field_index-1,wave_index});
                    auto p2=session.calibrated_models_.find(FieldWaveKey{field_index-2,wave_index});
                    if (p1!=session.calibrated_models_.end() && p2!=session.calibrated_models_.end() &&
                        p1->second.built && p2->second.built) {
                        double scale=0.0;
                        const auto warm=cpu_vignetted_past_field_edge_seed<FirstSurfaceSeed>(
                            session.system_,field,model,static_cast<int>(i),rs,residual_limit,
                            &p1->second,&session.system_.fields[field_index-1],
                            &p2->second,&session.system_.fields[field_index-2],&scale);
                        if (warm.valid && seed_in_first_surface_bounds(warm)) {
                            const double tx=(field.fvcx+edge.px*(1.0-field.fvdx))*rs;
                            const double ty=(field.fvcy+edge.py*(1.0-field.fvdy))*rs;
                            auto probe=[&](const FirstSurfaceSeed& trial,double* sx,double* sy) {
                                return session.sample_stop_from_seed(field,wave,trial,sx,sy,nullptr,true,true)
                                    && std::isfinite(*sx) && std::isfinite(*sy);
                            };
                            double sx=0.0,sy=0.0;
                            FirstSurfaceSeed solved;
                            double solved_sx=0.0,solved_sy=0.0;
                            if (probe(warm,&sx,&sy) && cpu_xy_continuation_solve(
                                    warm,sx,sy,tx,ty,residual_limit,scale,max_iterations,probe,
                                    seed_in_first_surface_bounds,&solved,&solved_sx,&solved_sy)) {
                                commit_edge_stop_sample(anchor,solved,
                                    std::hypot(solved_sx-tx,solved_sy-ty),solved_sx,solved_sy);
                                remember_scale_hint(axis_index,canonical_full_seed,anchor.seed);
                                continue;
                            }
                        }
                    }
                }
                const std::vector<double> transverse_scale_hints;
                for (const EdgeSeedCandidate& candidate : full_seed_candidates) {
                    const bool use_hint_only =
                        candidate.include_default_backoff &&
                        !candidate.use_transverse_scale_hints &&
                        !scale_hints.empty();
                    Type_EdgeBackoffResult candidate_backoff =
                        try_scaled_edge_anchor(
                            anchor,
                            edge,
                            candidate.seed,
                            // A cached cross-wave anchor already has its own physical
                            // displacement. CUDA probes it at scale 1, then its
                            // measured STOP projection. Canonical axis/common
                            // hints belong to a different full-seed family.
                            (candidate.use_transverse_scale_hints ||
                             ((continuation_mode&512) && candidate.stop_after_traceable))
                                ? transverse_scale_hints : scale_hints,
                            candidate.include_default_backoff && !use_hint_only);
                    if (!candidate_backoff.accepted && use_hint_only) {
                        Type_EdgeBackoffResult default_backoff = try_scaled_edge_anchor(
                            anchor,
                            edge,
                            candidate.seed,
                            transverse_scale_hints,
                            true);
                        if (default_backoff.accepted) {
                            candidate_backoff = default_backoff;
                        } else if (default_backoff.have_seed &&
                                   (!candidate_backoff.have_seed ||
                                    default_backoff.residual <
                                        candidate_backoff.residual)) {
                            if (!default_backoff.previous_sample.valid &&
                                candidate_backoff.sample.valid) {
                                default_backoff.previous_sample =
                                    candidate_backoff.sample;
                            }
                            candidate_backoff = default_backoff;
                        } else if (candidate_backoff.have_seed &&
                                   !candidate_backoff.previous_sample.valid &&
                                   default_backoff.sample.valid) {
                            candidate_backoff.previous_sample =
                                default_backoff.sample;
                        }
                    }
                    if (candidate_backoff.accepted) {
                        remember_scale_hint(axis_index, canonical_full_seed, anchor.seed);
                        backoff = candidate_backoff;
                        backoff_accepted = true;
                        break;
                    }
                    if (candidate_backoff.have_seed &&
                        (!have_backoff_seed ||
                         candidate_backoff.residual < backoff.residual)) {
                        if (!candidate_backoff.previous_sample.valid &&
                            have_backoff_seed && backoff.sample.valid &&
                            std::hypot(
                                backoff.sample.seed.x - candidate_backoff.sample.seed.x,
                                backoff.sample.seed.y - candidate_backoff.sample.seed.y) >
                                1.0e-12) {
                            candidate_backoff.previous_sample = backoff.sample;
                        }
                        backoff = candidate_backoff;
                        backoff_full_seed = candidate.seed;
                        have_backoff_seed = true;
                    } else if (
                        candidate_backoff.have_seed && have_backoff_seed &&
                        backoff.sample.valid && candidate_backoff.sample.valid &&
                        std::hypot(
                            backoff.sample.seed.x - candidate_backoff.sample.seed.x,
                            backoff.sample.seed.y - candidate_backoff.sample.seed.y) >
                            1.0e-12 &&
                        (!backoff.previous_sample.valid ||
                         candidate_backoff.residual <
                             backoff.previous_sample.residual)) {
                        backoff.previous_sample = candidate_backoff.sample;
                    }
                    if (candidate.stop_after_traceable && candidate_backoff.have_seed) {
                        break;
                    }
                }
                if (backoff_accepted) {
                    continue;
                }
                if (!have_backoff_seed) {
                    continue;
                }
                seed = backoff.seed;
                const bool use_seed_space =
                    fixed_jacobian_edge_solver ||
                    secant_edge_solver ||
                    fixed_then_iterative_edge_solver ||
                    secant_then_iterative_edge_solver;
                if (use_seed_space) {
                    const bool use_secant = secant_edge_solver || secant_then_iterative_edge_solver;
                    bool accepted = solve_seed_space_edge(
                        anchor,
                        edge,
                        seed,
                        use_secant,
                        backoff.sample.valid ? &backoff.sample : nullptr,
                        backoff.previous_sample.valid ? &backoff.previous_sample : nullptr,
                        nullptr,
                        std::numeric_limits<double>::infinity(),
                        2);
                    if (!accepted && use_secant && backoff.previous_sample.valid) {
                        accepted = solve_seed_space_edge(
                            anchor,
                            edge,
                            seed,
                            use_secant,
                            backoff.sample.valid ? &backoff.sample : nullptr,
                            nullptr,
                            nullptr,
                            std::numeric_limits<double>::infinity(),
                            2);
                    }
                    // A primary edge defines the physical aperture during
                    // vignetting optimization. If the cheap scalar-inverse
                    // secant misses it, make one bounded retry from a local
                    // finite-difference inverse Jacobian. CUDA uses the same
                    // two probes, step cap and backtrack budget.
                    const bool primary_vignetting_edge =
                        session.system_.primary_wavelength_apertures && wave.primary;
                    if (!accepted && primary_vignetting_edge &&
                        backoff.sample.valid && backoff.sample.seed.valid) {
                        const double delta = 1.0e-4 * std::max(
                            1.0, session.tracer_.entrance_pupil_radius(wave.value_um));
                        FirstSurfaceSeed seed_x = backoff.sample.seed;
                        FirstSurfaceSeed seed_y = backoff.sample.seed;
                        seed_x.x += delta;
                        seed_y.y += delta;
                        double sx_x = 0.0;
                        double sy_x = 0.0;
                        double sx_y = 0.0;
                        double sy_y = 0.0;
                        if (session.sample_stop_from_seed(
                                field, wave, seed_x, &sx_x, &sy_x,
                                nullptr, true, true) &&
                            session.sample_stop_from_seed(
                                field, wave, seed_y, &sx_y, &sy_y,
                                nullptr, true, true)) {
                            const double j11 = (sx_x - backoff.sample.sx) / delta;
                            const double j21 = (sy_x - backoff.sample.sy) / delta;
                            const double j12 = (sx_y - backoff.sample.sx) / delta;
                            const double j22 = (sy_y - backoff.sample.sy) / delta;
                            const double det = j11 * j22 - j12 * j21;
                            if (std::isfinite(det) && std::abs(det) > 1.0e-14) {
                                const std::array<double, 4> inverse{{
                                    j22 / det,
                                    -j12 / det,
                                    -j21 / det,
                                    j11 / det,
                                }};
                                const double step_cap =
                                    infinite_object_aim_position_step_cap(session.system_, rs);
                                accepted = solve_seed_space_edge(
                                    anchor,
                                    edge,
                                    backoff.sample.seed,
                                    true,
                                    &backoff.sample,
                                    nullptr,
                                    &inverse,
                                    step_cap,
                                    4);
                            }
                        }
                    }
                    // Preserve the historical CPU recovery outside the
                    // primary-wavelength vignetting contract. Inside that
                    // contract the shared bounded retry above is the final
                    // solver so CPU and CUDA cannot select different branches.
                    if (!accepted && !primary_vignetting_edge &&
                        seed_in_first_surface_bounds(seed)) {
                        Ray guess = session.tracer_.generate_ray_through_first_surface_point(
                            field,
                            wave.value_um,
                            seed.x,
                            seed.y,
                            edge.weight);
                        if (guess.valid) {
                            add_diag(diag.calibrated_edge_correction_calls);
                            Ray solved;
                            {
                                ScopedDiagTimer edge_correction_timer(
                                    diag.calibrated_edge_correction_ns);
                                solved = session.tracer_.solve_stop_aim_from_initial(
                                    field,
                                    edge,
                                    wave.value_um,
                                    guess,
                                    false,
                                    max_iterations,
                                    true,
                                    true,
                                    seed.x,
                                    seed.y);
                            }
                            add_diag(
                                diag.calibrated_edge_correction_iterations,
                                std::max(0, solved.aim_iterations));
                            accepted = accept_edge_ray(anchor, edge, solved);
                        }
                    }
                    if (!accepted) {
                        accepted = accept_reachable_secondary_edge(
                            anchor,
                            edge,
                            backoff.sample);
                    }
                    if (accepted) {
                        remember_scale_hint(axis_index, canonical_full_seed, anchor.seed);
                    }
                    if (accepted || fixed_jacobian_edge_solver || secant_edge_solver) {
                        continue;
                    }
                }
                if (!seed_in_first_surface_bounds(seed)) {
                    continue;
                }
                Ray guess = session.tracer_.generate_ray_through_first_surface_point(
                    field,
                    wave.value_um,
                    seed.x,
                    seed.y,
                    edge.weight);
                if (!guess.valid) {
                    continue;
                }
                add_diag(diag.calibrated_edge_correction_calls);
                Ray solved;
                {
                    ScopedDiagTimer edge_correction_timer(diag.calibrated_edge_correction_ns);
                    solved = session.tracer_.solve_stop_aim_from_initial(
                        field,
                        edge,
                        wave.value_um,
                        guess,
                        false,
                        max_iterations,
                        true);
                }
                add_diag(diag.calibrated_edge_correction_iterations,
                         std::max(0, solved.aim_iterations));
                const bool ray_accepted = accept_edge_ray(anchor, edge, solved);
                if (ray_accepted) {
                    remember_scale_hint(axis_index, canonical_full_seed, anchor.seed);
                }
            }
        }
        return nullptr;
    }
};

} // namespace optiland::raytrace_detail
