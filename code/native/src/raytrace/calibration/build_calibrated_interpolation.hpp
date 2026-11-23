#pragma once
#include "../diagnostics_inline.hpp"
#include "../detail.hpp"
#include "calibration_accept_edge_stop_sample.hpp"
#include "calibration_fallback_edge_seed.hpp"
#include "calibration_solve_seed_space_edge.hpp"
#include "calibration_sweep_interpolation_edges.hpp"
#include "calibration_try_scaled_edge_anchor.hpp"

namespace optiland::raytrace_detail {

struct BuildCalibratedInterpolation {

    inline static constexpr std::array<PupilPoint, 5> kValidationPupils{{
        PupilPoint{0.35, 0.35, 1.0},
        PupilPoint{0.0, 0.5, 1.0},
        PupilPoint{0.0, -0.5, 1.0},
        PupilPoint{0.5, 0.0, 1.0},
        PupilPoint{-0.5, 0.0, 1.0},
    }};
    using CalibratedCenterBeamFootprint = AimingSession::CalibratedCenterBeamFootprint;
    using CalibratedParaxialModel = AimingSession::CalibratedParaxialModel;
    using FieldWaveKey = AimingSession::FieldWaveKey;
    using FirstSurfaceSeed = AimingSession::FirstSurfaceSeed;

    template <
        class Capture_existing,
        class Capture_finish_model,
        class Capture_seed_in_first_surface_bounds
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
        Capture_finish_model& finish_model,
        const CalibratedParaxialConfig& cfg,
        const Field& field,
        const double& rs,
        const double& stop_scale,
        const double& chief_target_x,
        const double& chief_target_y,
        const double& radius,
        Capture_seed_in_first_surface_bounds& seed_in_first_surface_bounds
    ) -> CalibratedParaxialModel& {
        for (auto& anchor : model.edge_anchors) {
            anchor = {};
        }
        const std::array<PupilPoint, 4> edge_pupils{{
            PupilPoint{1.0, 0.0, 1.0},
            PupilPoint{-1.0, 0.0, 1.0},
            PupilPoint{0.0, 1.0, 1.0},
            PupilPoint{0.0, -1.0, 1.0},
        }};
        auto field_center_score = [](const Field& candidate) {
            double score = std::hypot(candidate.hx, candidate.hy);
            if (!std::isfinite(score)) {
                score = std::numeric_limits<double>::infinity();
            }
            return score;
        };
        bool current_is_center_reference = false;
        if (!session.system_.fields.empty()) {
            std::size_t center_field_index = 0;
            auto best_score = field_center_score(session.system_.fields.front());
            for (std::size_t i = 1; i < session.system_.fields.size(); ++i) {
                const auto score = field_center_score(session.system_.fields[i]);
                if (score < best_score) {
                    best_score = score;
                    center_field_index = i;
                }
            }
            current_is_center_reference = field_index == center_field_index;
        } else {
            auto best_score = field_center_score(field);
            current_is_center_reference = true;
            for (const auto& entry : session.fields_seen_) {
                const auto score = field_center_score(entry.second);
                if (score < best_score) {
                    current_is_center_reference = entry.first == field_index;
                    best_score = score;
                }
            }
        }
        const CalibratedCenterBeamFootprint* center_footprint = nullptr;
        if (!current_is_center_reference) {
            const CalibratedCenterBeamFootprint& cached_footprint =
                session.ensure_calibrated_center_beam_footprint(wave_index, wave);
            if (!cached_footprint.valid) {
                return finish_model();
            }
            center_footprint = &cached_footprint;
        }
        auto physical_stop_radius = [&]() {
            double physical_radius = stop_scale;
            const std::size_t stop = session.tracer_.stop_surface_index();
            if (stop < session.system_.surfaces.size()) {
                const Surface& stop_surface = session.system_.surfaces[stop];
                if (stop_surface.aperture_radius > 0.0) {
                    physical_radius = stop_surface.aperture_radius;
                } else if (stop_surface.semi_aperture > 0.0) {
                    physical_radius = stop_surface.semi_aperture;
                }
            }
            return physical_radius;
        };
        const double residual_limit = std::max(
            std::max(0.0, session.system_.ray_aiming.tolerance),
            std::max(0.0, cfg.edge_calibration_residual_stop_fraction) * physical_stop_radius());
        const int max_iterations = std::max(1, cfg.edge_calibration_iterations);
        const double edge_radius = std::clamp(
            std::max(1.0e-9, session.tracer_.entrance_pupil_radius(wave.value_um)),
            1.0e-9,
            std::max(1.0e-9, radius));
        double first_surface_edge_radius = edge_radius;
        const double center_beam_edge_radius = session.center_meridional_radius(wave);
        if (std::isfinite(center_beam_edge_radius) && center_beam_edge_radius > 1.0e-9) {
            first_surface_edge_radius = std::min(edge_radius, center_beam_edge_radius);
        }
        const std::size_t first_aiming_surface =
            session.system_.first_ray_aiming_surface(wave.value_um);
        const bool enforce_edge_branch_continuity =
            first_aiming_surface < session.system_.surfaces.size() &&
            session.system_.surfaces[first_aiming_surface].kind == SurfaceKind::EvenAsphere;
        const std::string edge_solver = cfg.edge_calibration_solver;
        const bool fixed_jacobian_edge_solver =
            edge_solver == "fixed_jacobian" ||
            edge_solver == "fixed_jacobian_only" ||
            edge_solver == "fixed" ||
            edge_solver == "linear" ||
            edge_solver == "one_step";
        const bool secant_edge_solver =
            edge_solver == "secant" ||
            edge_solver == "broyden" ||
            edge_solver == "secant_only" ||
            edge_solver == "broyden_only";
        const bool fixed_then_iterative_edge_solver =
            edge_solver == "fixed_jacobian_then_iterative" ||
            edge_solver == "fixed_then_iterative" ||
            edge_solver == "linear_then_iterative";
        const bool secant_then_iterative_edge_solver =
            edge_solver == "secant_then_iterative" ||
            edge_solver == "broyden_then_iterative";
        double max_residual = std::hypot(
            model.chief_stop_x - chief_target_x,
            model.chief_stop_y - chief_target_y);

        struct DeferredEdgeSample {
            bool valid = false;
            FirstSurfaceSeed seed;
            double stop_x = 0.0;
            double stop_y = 0.0;
            double residual = std::numeric_limits<double>::infinity();
            double continuity_distance_sq = std::numeric_limits<double>::infinity();
        };
        std::array<DeferredEdgeSample, 4> deferred_edge_samples{};

        auto commit_edge_stop_sample = [&](CalibratedParaxialModel::EdgeAnchor& anchor,
                                           const FirstSurfaceSeed& seed,
                                           double residual, double sx, double sy) {
            anchor.seed = seed;
            anchor.stop_x = sx;
            anchor.stop_y = sy;
            anchor.correction_x = 0.0;
            anchor.correction_y = 0.0;
            anchor.valid = true;
            max_residual = std::max(max_residual, residual);
            add_diag_residual(
                diag.calibrated_edge_residual_count,
                diag.calibrated_edge_residual_sum_scaled,
                diag.calibrated_edge_residual_max_scaled,
                residual);
            return true;
        };

        auto accept_edge_stop_sample = [&](CalibratedParaxialModel::EdgeAnchor& anchor,
                                           const PupilPoint& edge,
                                           const FirstSurfaceSeed& seed,
                                           double sx,
                                           double sy) {
        return ::optiland::raytrace_detail::CalibrationAcceptEdgeStopSample::run<DeferredEdgeSample>(
            session,
            model,
            field,
            rs,
            seed_in_first_surface_bounds,
            residual_limit,
            first_surface_edge_radius,
            enforce_edge_branch_continuity,
            deferred_edge_samples,
            commit_edge_stop_sample,
            anchor,
            edge,
            seed,
            sx,
            sy
        );
    };

        auto accept_edge_seed = [&](CalibratedParaxialModel::EdgeAnchor& anchor,
                                    const PupilPoint& edge,
                                    const FirstSurfaceSeed& seed) {
            double sx = 0.0;
            double sy = 0.0;
            if (!session.sample_stop_from_seed(field, wave, seed, &sx, &sy, nullptr, true, true)) {
                return false;
            }
            return accept_edge_stop_sample(anchor, edge, seed, sx, sy);
        };

        auto accept_edge_ray = [&](CalibratedParaxialModel::EdgeAnchor& anchor,
                                   const PupilPoint& edge,
                                   const Ray& ray) {
            const FirstSurfaceSeed seed = session.seed_from_ray(ray);
            if (!seed.valid) {
                return false;
            }
            double sx = 0.0;
            double sy = 0.0;
            if (!session.sample_stop_from_ray(ray, &sx, &sy, true, true)) {
                return false;
            }
            return accept_edge_stop_sample(anchor, edge, seed, sx, sy);
        };

        struct EdgeStopSample {
            bool valid = false;
            FirstSurfaceSeed seed;
            double sx = 0.0;
            double sy = 0.0;
            double residual = std::numeric_limits<double>::infinity();
        };

        // Zemax Set Vignetting is a primary-wavelength contract.  A secondary
        // wavelength can lose the mathematical unit-edge root at a hard
        // traceability boundary even though the scored pupil remains inside
        // that boundary.  Reuse the closest sample already paid for by the
        // failed unit solve as a wavelength-local physical-domain anchor.  Do
        // not move a primary anchor and do not launch a scan.
        auto accept_reachable_secondary_edge =
            [&](CalibratedParaxialModel::EdgeAnchor& anchor,
                const PupilPoint& edge,
                const EdgeStopSample& sample) {
                if (wave.primary || !sample.valid || !sample.seed.valid ||
                    !std::isfinite(sample.sx) || !std::isfinite(sample.sy)) {
                    return false;
                }
                const bool x_axis = std::abs(edge.px) >= std::abs(edge.py);
                const double axis_denominator =
                    (x_axis ? (1.0 - field.fvdx) : (1.0 - field.fvdy)) * rs;
                if (!std::isfinite(axis_denominator) ||
                    std::abs(axis_denominator) <= 1.0e-12) {
                    return false;
                }
                const double actual_axis =
                    x_axis
                        ? (sample.sx - field.fvcx * rs) / axis_denominator
                        : (sample.sy - field.fvcy * rs) / axis_denominator;
                constexpr double kMinimumReachableEdgeFraction = 0.98;
                const double requested_sign = x_axis ? edge.px : edge.py;
                if (!std::isfinite(actual_axis) ||
                    actual_axis * requested_sign <= 0.0 ||
                    std::abs(actual_axis) < kMinimumReachableEdgeFraction ||
                    std::abs(actual_axis) > 1.0 + 1.0e-8) {
                    return false;
                }
                const double cross_residual =
                    x_axis
                        ? std::abs(sample.sy - field.fvcy * rs)
                        : std::abs(sample.sx - field.fvcx * rs);
                const double cross_limit = std::max(
                    residual_limit,
                    std::max(0.0, cfg.validation_residual_stop_fraction) *
                        physical_stop_radius());
                if (!std::isfinite(cross_residual) || cross_residual > cross_limit) {
                    return false;
                }
                anchor.px = x_axis ? actual_axis : 0.0;
                anchor.py = x_axis ? 0.0 : actual_axis;
                return commit_edge_stop_sample(anchor, sample.seed, cross_residual, sample.sx, sample.sy);
            };

        auto solve_seed_space_edge = [&](CalibratedParaxialModel::EdgeAnchor& anchor,
                                         const PupilPoint& edge,
                                         FirstSurfaceSeed seed,
                                         bool use_secant,
                                         const EdgeStopSample* initial_sample,
                                         const EdgeStopSample* previous_sample,
                                         const std::array<double, 4>* initial_inverse,
                                         double step_cap,
                                         int failure_backtrack_trials) {
        return ::optiland::raytrace_detail::CalibrationSolveSeedSpaceEdge::run<EdgeStopSample>(
            session,
            wave,
            diag,
            model,
            field,
            rs,
            seed_in_first_surface_bounds,
            residual_limit,
            max_iterations,
            accept_edge_stop_sample,
            accept_edge_seed,
            anchor,
            edge,
            seed,
            use_secant,
            initial_sample,
            previous_sample,
            initial_inverse,
            step_cap,
            failure_backtrack_trials
        );
    };

        auto scaled_edge_seed = [&](const FirstSurfaceSeed& full_seed, double scale) {
            FirstSurfaceSeed seed;
            if (!full_seed.valid || !std::isfinite(scale) || scale <= 0.0) {
                return seed;
            }
            seed.x = model.chief_seed.x + scale * (full_seed.x - model.chief_seed.x);
            seed.y = model.chief_seed.y + scale * (full_seed.y - model.chief_seed.y);
            seed.valid = std::isfinite(seed.x) && std::isfinite(seed.y);
            return seed;
        };

        auto relative_edge_scale = [&](const FirstSurfaceSeed& full_seed,
                                       const FirstSurfaceSeed& seed) {
            if (!full_seed.valid || !seed.valid) {
                return std::numeric_limits<double>::quiet_NaN();
            }
            const double full_dx = full_seed.x - model.chief_seed.x;
            const double full_dy = full_seed.y - model.chief_seed.y;
            const double denom = full_dx * full_dx + full_dy * full_dy;
            if (!std::isfinite(denom) || denom <= 1.0e-24) {
                return std::numeric_limits<double>::quiet_NaN();
            }
            const double seed_dx = seed.x - model.chief_seed.x;
            const double seed_dy = seed.y - model.chief_seed.y;
            const double scale = (seed_dx * full_dx + seed_dy * full_dy) / denom;
            return std::isfinite(scale) ? scale : std::numeric_limits<double>::quiet_NaN();
        };

        struct EdgeBackoffResult {
            bool accepted = false;
            bool have_seed = false;
            FirstSurfaceSeed seed;
            EdgeStopSample sample;
            EdgeStopSample previous_sample;
            double residual = std::numeric_limits<double>::infinity();
        };

        auto try_scaled_edge_anchor =
            [&](CalibratedParaxialModel::EdgeAnchor& anchor,
                const PupilPoint& edge,
                const FirstSurfaceSeed& full_seed,
                const std::vector<double>& scale_hints,
                bool include_default_backoff) {
        return ::optiland::raytrace_detail::CalibrationTryScaledEdgeAnchor::run<EdgeBackoffResult>(
            session,
            wave,
            existing,
            model,
            field,
            rs,
            seed_in_first_surface_bounds,
            accept_edge_stop_sample,
            scaled_edge_seed,
            anchor,
            edge,
            full_seed,
            scale_hints,
            include_default_backoff
        );
    };

        if (auto* completed = ::optiland::raytrace_detail::CalibrationSweepInterpolationEdges::run<EdgeBackoffResult>(
            session,
            field_index,
            wave_index,
            wave,
            diag,
            existing,
            continuation_mode,
            continuation_repair_build,
            model,
            cfg,
            field,
            rs,
            seed_in_first_surface_bounds,
            edge_pupils,
            center_footprint,
            residual_limit,
            max_iterations,
            first_surface_edge_radius,
            fixed_jacobian_edge_solver,
            secant_edge_solver,
            fixed_then_iterative_edge_solver,
            secant_then_iterative_edge_solver,
            commit_edge_stop_sample,
            accept_edge_ray,
            accept_reachable_secondary_edge,
            solve_seed_space_edge,
            relative_edge_scale,
            try_scaled_edge_anchor
        )) {
            return *completed;
        }
        // For an even asphere, continuity is a branch preference rather than a
        // hard rejection. Only use a distant but stop-valid solution after the
        // bounded candidate search failed to find a locally continuous one;
        // the model validation probes below remain the final physical gate.
        for (std::size_t i = 0; i < model.edge_anchors.size() &&
                                i < deferred_edge_samples.size(); ++i) {
            auto& anchor = model.edge_anchors[i];
            const DeferredEdgeSample& deferred = deferred_edge_samples[i];
            if ((!anchor.valid || !anchor.seed.valid) && deferred.valid) {
                commit_edge_stop_sample(anchor, deferred.seed, deferred.residual, deferred.stop_x, deferred.stop_y);
            }
        }

        // A primary-wave Set Vignetting state may place a secondary unit edge
        // just beyond that wavelength's traceable branch.  If the ordinary
        // bounded solve produced no reusable sample, take exactly one damped
        // probe from the already-built primary unit-edge family.  This keeps
        // the normal hot path unchanged and avoids any pupil scan.
        if (!wave.primary) {
            const CalibratedParaxialModel* primary_model = nullptr;
            std::size_t primary_wave_index = 0;
            for (std::size_t wi = 0; wi < session.system_.wavelengths.size(); ++wi) {
                if (session.system_.wavelengths[wi].primary) {
                    primary_wave_index = wi;
                    break;
                }
            }
            const auto primary_it = session.calibrated_models_.find(
                FieldWaveKey{field_index, primary_wave_index});
            if (primary_it != session.calibrated_models_.end() &&
                primary_it->second.built &&
                primary_it->second.chief_seed.valid) {
                primary_model = &primary_it->second;
            }
            if (primary_model) {
                constexpr double kSecondaryPrimarySeedDamping = 0.997;
                for (std::size_t i = 0;
                     i < model.edge_anchors.size() && i < edge_pupils.size();
                     ++i) {
                    auto& anchor = model.edge_anchors[i];
                    const auto& primary_anchor = primary_model->edge_anchors[i];
                    if ((anchor.valid && anchor.seed.valid) ||
                        !primary_anchor.valid || !primary_anchor.seed.valid) {
                        continue;
                    }
                    FirstSurfaceSeed candidate{
                        model.chief_seed.x + kSecondaryPrimarySeedDamping *
                            (primary_anchor.seed.x - primary_model->chief_seed.x),
                        model.chief_seed.y + kSecondaryPrimarySeedDamping *
                            (primary_anchor.seed.y - primary_model->chief_seed.y),
                        true};
                    double sx = 0.0;
                    double sy = 0.0;
                    if (!seed_in_first_surface_bounds(candidate) ||
                        !session.sample_stop_from_seed(
                            field,
                            wave,
                            candidate,
                            &sx,
                            &sy,
                            nullptr,
                            true,
                            true)) {
                        continue;
                    }
                    const PupilPoint& edge = edge_pupils[i];
                    const double target_x =
                        (field.fvcx + edge.px * (1.0 - field.fvdx)) * rs;
                    const double target_y =
                        (field.fvcy + edge.py * (1.0 - field.fvdy)) * rs;
                    const EdgeStopSample sample{
                        true,
                        candidate,
                        sx,
                        sy,
                        std::hypot(sx - target_x, sy - target_y)};
                    accept_reachable_secondary_edge(anchor, edge, sample);
                }
            }
        }

        bool calibrated_edges_all_valid = std::all_of(
            model.edge_anchors.begin(),
            model.edge_anchors.end(),
            [](const CalibratedParaxialModel::EdgeAnchor& anchor) {
                return anchor.valid && anchor.seed.valid;
        });
        // During vignetting optimization the primary wavelength defines the
        // physical aperture.  Its unit-edge anchors therefore have to be
        // actual STOP-calibrated solutions; a geometric completion is only a
        // sampling aid for secondary wavelengths.  Keep the historical
        // completion outside that mode so ordinary calibrated aiming is not
        // changed by the vignetting contract.
        const bool allow_unvalidated_edge_completion =
            !session.system_.primary_wavelength_apertures || !wave.primary;
        if (!calibrated_edges_all_valid && allow_unvalidated_edge_completion) {
            auto fallback_edge_seed = [&](std::size_t edge_index) -> FirstSurfaceSeed {
        return ::optiland::raytrace_detail::CalibrationFallbackEdgeSeed::run(
            session,
            wave,
            model,
            field,
            seed_in_first_surface_bounds,
            edge_pupils,
            first_surface_edge_radius,
            center_beam_edge_radius,
            edge_index
        );
    };

            for (std::size_t i = 0; i < model.edge_anchors.size() && i < edge_pupils.size(); ++i) {
                auto& anchor = model.edge_anchors[i];
                if (anchor.valid && anchor.seed.valid) {
                    continue;
                }
                const FirstSurfaceSeed fallback = fallback_edge_seed(i);
                if (!fallback.valid) {
                    continue;
                }
                anchor.px = edge_pupils[i].px;
                anchor.py = edge_pupils[i].py;
                anchor.seed = fallback;
                anchor.correction_x = 0.0;
                anchor.correction_y = 0.0;
                anchor.valid = true;
            }

            // The fallback loop above deliberately completes a secondary-wave
            // edge family from already available local first-surface seeds
            // while vignetting optimization is active.
            // Re-evaluate the gate after those anchors are installed; retaining
            // the pre-fallback value would invalidate the whole field/wave model
            // even though all four reusable anchors now exist.
            calibrated_edges_all_valid = std::all_of(
                model.edge_anchors.begin(),
                model.edge_anchors.end(),
                [](const CalibratedParaxialModel::EdgeAnchor& anchor) {
                    return anchor.valid && anchor.seed.valid;
                });

        }
        model.fit_rms = max_residual;
        model.valid = model.chief_seed.valid && calibrated_edges_all_valid;
        if (model.valid && diagnostics_active()) {
            // These interior probes measure interpolation quality only. The
            // chief ray and four calibrated edge anchors are the complete
            // convergence gate for chief_edge_interpolation.

            const int validation_count = std::min<int>(
                std::max(0, cfg.validation_sample_count),
                static_cast<int>(kValidationPupils.size()));
            const double validation_limit = std::max(
                std::max(0.0, session.system_.ray_aiming.tolerance),
                std::max(0.0, cfg.validation_residual_stop_fraction) *
                    physical_stop_radius());
            auto add_edge_delta = [&](FirstSurfaceSeed* seed,
                                      bool x_axis,
                                      double coord) {
                if (!seed || std::abs(coord) <= 1.0e-14) {
                    return true;
                }
                const bool positive = coord > 0.0;
                const CalibratedParaxialModel::EdgeAnchor* selected = nullptr;
                for (const auto& anchor : model.edge_anchors) {
                    if (!anchor.valid || !anchor.seed.valid) {
                        continue;
                    }
                    if ((x_axis && positive && anchor.px > 0.5) ||
                        (x_axis && !positive && anchor.px < -0.5) ||
                        (!x_axis && positive && anchor.py > 0.5) ||
                        (!x_axis && !positive && anchor.py < -0.5)) {
                        selected = &anchor;
                        break;
                    }
                }
                if (!selected) {
                    return false;
                }
                const double anchor_coord =
                    std::abs(x_axis ? selected->px : selected->py);
                if (!std::isfinite(anchor_coord) || anchor_coord <= 1.0e-12 ||
                    std::abs(coord) > anchor_coord * (1.0 + 1.0e-10)) {
                    return false;
                }
                const double weight = std::abs(coord) / anchor_coord;
                seed->x += weight * (selected->seed.x - model.chief_seed.x);
                seed->y += weight * (selected->seed.y - model.chief_seed.y);
                return std::isfinite(seed->x) && std::isfinite(seed->y);
            };
            for (int i = 0; i < validation_count; ++i) {
                add_diag(diag.calibrated_interpolation_probe_calls);
                const PupilPoint& pupil = kValidationPupils[static_cast<std::size_t>(i)];
                FirstSurfaceSeed seed = model.chief_seed;
                if (!add_edge_delta(&seed, true, pupil.px) ||
                    !add_edge_delta(&seed, false, pupil.py) ||
                    !seed_in_first_surface_bounds(seed)) {
                    add_diag(diag.calibrated_interpolation_probe_seed_failures);
                    continue;
                }
                double sx = 0.0;
                double sy = 0.0;
                if (!session.sample_stop_from_seed(
                        field, wave, seed, &sx, &sy, nullptr, true, true)) {
                    add_diag(diag.calibrated_interpolation_probe_trace_failures);
                    continue;
                }
                const double target_x =
                    (field.fvcx + pupil.px * (1.0 - field.fvdx)) * rs;
                const double target_y =
                    (field.fvcy + pupil.py * (1.0 - field.fvdy)) * rs;
                const double residual = std::hypot(sx - target_x, sy - target_y);
                if (!std::isfinite(residual)) {
                    add_diag(diag.calibrated_interpolation_probe_trace_failures);
                    continue;
                }
                add_diag(diag.calibrated_interpolation_probe_successes);
                add_diag_residual(
                    diag.calibrated_interpolation_residual_count,
                    diag.calibrated_interpolation_residual_sum_scaled,
                    diag.calibrated_interpolation_residual_max_scaled,
                    residual);
                if (residual > validation_limit) {
                    add_diag(diag.calibrated_interpolation_residual_over_limit);
                }
            }
        }
        if (model.valid && calibrated_edges_all_valid && current_is_center_reference) {
            CalibratedCenterBeamFootprint& cached_footprint =
                session.calibrated_center_beam_footprints_[wave_index];
            cached_footprint = {};
            cached_footprint.built = true;
            cached_footprint.valid = true;
            cached_footprint.wavelength_um = wave.value_um;
            cached_footprint.chief_seed = model.chief_seed;
            for (std::size_t i = 0; i < model.edge_anchors.size(); ++i) {
                const auto& anchor = model.edge_anchors[i];
                cached_footprint.edge_vectors[i] = {
                    anchor.seed.x - model.chief_seed.x,
                    anchor.seed.y - model.chief_seed.y,
                    true};
            }
        }
        return finish_model();
    }
};

} // namespace optiland::raytrace_detail
