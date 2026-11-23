/*
 * Materializes clear-aperture envelopes and synchronizes eligible stop radii from rays.
 * Used by optimizer preparation, checkpoint rescoring and CLI aperture updates.
 * Fixed clear/mechanical apertures, prescribed stop targets and primary-wavelength
 * vignetting support must retain their distinct physical and sampling contracts.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

void sync_stop_aperture_to_aiming_radius(LensSystem& system) {
    auto stop_it = std::find_if(system.surfaces.begin(), system.surfaces.end(), [](const Surface& surface) {
        return surface.is_stop;
    });
    if (stop_it == system.surfaces.end() || stop_it->clear_aperture_fixed) {
        return;
    }

    double radius = 0.0;
    if (system.aperture.type == ApertureType::FloatByStopSize) {
        radius = stop_it->aperture_radius > 0.0
                     ? stop_it->aperture_radius
                     : stop_it->semi_aperture;
    } else if (mode_uses_prescribed_stop_target(system.ray_aiming.mode)) {
        SequentialRayTracer tracer(system);
        radius = tracer.stop_target_radius(
            system.primary_wavelength(), system.ray_aiming.mode);
    } else {
        // Match CUDA's static-aperture contract: direct/paraxial automatic
        // stops retain their materialized radius. Dynamic sizing is owned by
        // the complete, ordered aperture trace below, not a prefix rescore.
        return;
    }
    if (!std::isfinite(radius) || radius <= 0.0) {
        return;
    }
    stop_it->aperture_radius = radius;
    stop_it->semi_aperture = radius;
    if (!stop_it->mechanical_aperture_fixed) {
        stop_it->mechanical_aperture = radius;
    }
}

void update_clear_apertures_from_trace(
    LensSystem& system,
    int rings,
    const std::string& distribution,
    unsigned int seed,
    RayAimingMode mode,
    double margin_factor,
    bool preserve_explicit_stop,
    int arms,
    bool use_current_stop_surface_radius,
    bool require_full_trace,
    double vignetting_boundary_support_tolerance,
    double vignetting_boundary_support_selectivity_slope,
    bool vignetting_global_trim_realizability,
    bool vignetting_physical_cut_match_enabled,
    bool primary_wavelength_apertures) {
    system.primary_wavelength_apertures = primary_wavelength_apertures;
    if (system.surfaces.empty()) {
        return;
    }

    const ApertureType original_aperture_type = system.aperture.type;
    if (use_current_stop_surface_radius) {
        const auto stop = std::find_if(
            system.surfaces.begin(),
            system.surfaces.end(),
            [](const Surface& surface) { return surface.is_stop; });
        if (stop != system.surfaces.end() &&
            std::isfinite(stop->semi_aperture) &&
            stop->semi_aperture > 0.0) {
            // Trace the clear-aperture envelope with the same physical stop
            // that Set Vignetting used, but restore the user's aperture
            // specification before returning.
            system.aperture.type = ApertureType::FloatByStopSize;
        }
    }

    const auto fields = fields_or_default(system);
    const auto waves = wavelengths_or_default(system);
    auto pupils = make_pupil_distribution(std::max(1, rings), distribution, seed, arms);
    // The semantic aperture guard is exactly the chief ray plus the four
    // cardinal marginal rays.  The ordinary pupil distribution still supplies
    // its requested ring/arm samples; do not silently expand this guard back
    // into a dense meridional or boundary-only layout.
    const PupilPoint chief{0.0, 0.0, 1.0};
    const bool have_chief = std::any_of(
        pupils.begin(),
        pupils.end(),
        [&](const PupilPoint& point) { return same_pupil_point(point, chief); });
    if (!have_chief) {
        pupils.push_back(chief);
    }
    const auto missing_cardinals = missing_single_ray_guard_edge_pupils(pupils);
    pupils.insert(pupils.end(), missing_cardinals.begin(), missing_cardinals.end());
    const double scale = std::max(1.0, margin_factor);

    bool aperture_debug = false;
    if (const char* target_text = std::getenv("OPTILAND_CPU_APERTURE_DEBUG_RADIUS")) {
        const double target = std::strtod(target_text, nullptr);
        if (system.surfaces.size() > 8 && std::isfinite(target) &&
            std::isfinite(system.surfaces[8].radius)) {
            aperture_debug =
                std::abs(system.surfaces[8].radius - target) <=
                1.0e-9 * std::max(1.0, std::abs(target));
        }
    }
    std::vector<double> max_heights(system.surfaces.size(), 0.0);
    const std::size_t support_surface_count = system.surfaces.size();
    std::vector<double> meridional_boundary_heights(
        fields.size() * waves.size() * 2 * support_surface_count,
        0.0);
    std::vector<unsigned char> meridional_boundary_observed(
        fields.size() * waves.size() * 2 * support_surface_count,
        0);
    std::vector<double> meridional_chief_heights(
        fields.size() * waves.size() * support_surface_count,
        0.0);
    std::vector<unsigned char> meridional_chief_observed(
        fields.size() * waves.size() * support_surface_count,
        0);
    // Global-trim ownership still uses the circular radial envelope above,
    // while physical side qualification must retain the sign of y.  Keep the
    // exact -Y/center/+Y coordinates separately so a folded -Y ray cannot
    // masquerade as a +Y aperture owner.  As in CUDA, boundary ordering uses
    // this trio only; all pupil samples still define the radial aperture.
    std::vector<double> meridional_boundary_y(
        fields.size() * waves.size() * 2 * support_surface_count,
        std::numeric_limits<double>::quiet_NaN());
    std::vector<double> meridional_chief_y(
        fields.size() * waves.size() * support_surface_count,
        std::numeric_limits<double>::quiet_NaN());
    for (Field& field : system.fields) {
        field.vignetting_negative_boundary_support_gap =
            std::numeric_limits<double>::quiet_NaN();
        field.vignetting_positive_boundary_support_gap =
            std::numeric_limits<double>::quiet_NaN();
        field.vignetting_negative_boundary_support_surface = -1;
        field.vignetting_positive_boundary_support_surface = -1;
        field.vignetting_negative_boundary_candidate_radius =
            std::numeric_limits<double>::quiet_NaN();
        field.vignetting_positive_boundary_candidate_radius =
            std::numeric_limits<double>::quiet_NaN();
        field.vignetting_negative_boundary_pretrim_radius =
            std::numeric_limits<double>::quiet_NaN();
        field.vignetting_positive_boundary_pretrim_radius =
            std::numeric_limits<double>::quiet_NaN();
        field.vignetting_negative_boundary_protection_radius =
            std::numeric_limits<double>::quiet_NaN();
        field.vignetting_positive_boundary_protection_radius =
            std::numeric_limits<double>::quiet_NaN();
        field.vignetting_negative_boundary_trim_window =
            std::numeric_limits<double>::quiet_NaN();
        field.vignetting_positive_boundary_trim_window =
            std::numeric_limits<double>::quiet_NaN();
    }
    std::vector<std::size_t> max_height_fields(
        aperture_debug ? system.surfaces.size() : 0, 0);
    std::vector<std::size_t> max_height_waves(
        aperture_debug ? system.surfaces.size() : 0, 0);
    std::vector<std::size_t> max_height_pupils(
        aperture_debug ? system.surfaces.size() : 0, 0);
    std::vector<double> max_height_px(
        aperture_debug ? system.surfaces.size() : 0, 0.0);
    std::vector<double> max_height_py(
        aperture_debug ? system.surfaces.size() : 0, 0.0);
    std::vector<double> original;
    original.reserve(system.surfaces.size());
    for (const auto& surface : system.surfaces) {
        original.push_back(surface.semi_aperture);
    }

    if (system.aperture.type == ApertureType::FloatByStopSize ||
        mode_uses_prescribed_stop_target(mode)) {
        sync_stop_aperture_to_aiming_radius(system);
    }

    SequentialRayTracer tracer(system);
    AimingSession aiming(system);
    for (std::size_t fi = 0; fi < fields.size(); ++fi) {
        const auto& field = fields[fi];
        for (std::size_t wi = 0; wi < waves.size(); ++wi) {
            const auto& wave = waves[wi];
            if (primary_wavelength_apertures && wi != primary_wavelength_index(system, waves)) continue;
            for (std::size_t pi = 0; pi < pupils.size(); ++pi) {
                const auto& pupil = pupils[pi];
                Ray ray = ray_aiming_uses_session(mode)
                              ? aiming.generate(fi, wi, pi, field, wave, pupil)
                              : tracer.generate_ray_with_mode(field, pupil, wave.value_um, mode);
                std::vector<TracePoint> history;
                tracer.trace_ray(
                    ray,
                    &history,
                    !mode_uses_prescribed_stop_target(mode));
                // A sequential clear-aperture envelope must only be defined by
                // rays that continue through the system in surface order.  A
                // strongly curved surface can have a mathematically valid
                // vertex-connected intersection far from the useful optical
                // zone, followed by a backward step to the next surface.  The
                // tracer deliberately records that path for diagnostics, but
                // its intermediate height is not a physical aperture demand.
                // Waiting until the full trace has completed lets us discard
                // the complete non-sequential history instead of permanently
                // inflating one surface before the later warning is known.
                if (ray.backward_propagation || (require_full_trace && !ray.valid)) {
                    continue;
                }
                int meridional_side = -1;
                const bool meridional_chief =
                    std::abs(pupil.px) < 1.0e-12 &&
                    std::abs(pupil.py) < 1.0e-12;
                if (std::abs(pupil.px) < 1.0e-12 &&
                    std::abs(pupil.py + 1.0) < 1.0e-12) {
                    meridional_side = 0;
                } else if (std::abs(pupil.px) < 1.0e-12 &&
                           std::abs(pupil.py - 1.0) < 1.0e-12) {
                    meridional_side = 1;
                }
                for (std::size_t i = 0; i < history.size() && i < max_heights.size(); ++i) {
                    const auto& point = history[i];
                    if (!point.valid || !std::isfinite(point.x) || !std::isfinite(point.y)) {
                        continue;
                    }
                    const double height =
                        std::sqrt(point.x * point.x + point.y * point.y);
                    if (meridional_side >= 0) {
                        const std::size_t support_index =
                            (((fi * waves.size() + wi) * 2 +
                              static_cast<std::size_t>(meridional_side)) *
                                 support_surface_count) +
                            i;
                        meridional_boundary_heights[support_index] = std::max(
                            meridional_boundary_heights[support_index],
                            height);
                        meridional_boundary_y[support_index] = point.y;
                        meridional_boundary_observed[support_index] = 1;
                    }
                    if (meridional_chief) {
                        const std::size_t chief_index =
                            (fi * waves.size() + wi) * support_surface_count + i;
                        meridional_chief_heights[chief_index] = std::max(
                            meridional_chief_heights[chief_index],
                            height);
                        meridional_chief_y[chief_index] = point.y;
                        meridional_chief_observed[chief_index] = 1;
                    }
                    if (height > max_heights[i]) {
                        max_heights[i] = height;
                        if (aperture_debug) {
                            max_height_fields[i] = fi;
                            max_height_waves[i] = wi;
                            max_height_pupils[i] = pi;
                            max_height_px[i] = pupil.px;
                            max_height_py[i] = pupil.py;
                        }
                    }
                }
            }

        }
    }

    for (std::size_t i = 0; i < system.surfaces.size(); ++i) {
        if (system.surfaces[i].kind == SurfaceKind::Object) {
            continue;
        }
        const bool explicit_stop_aperture =
            system.surfaces[i].clear_aperture_fixed ||
            (system.aperture.type == ApertureType::FloatByStopSize &&
             system.surfaces[i].aperture_radius > 0.0);
        if (system.surfaces[i].is_stop) {
            if (preserve_explicit_stop && explicit_stop_aperture) {
                if (system.surfaces[i].clear_aperture_fixed &&
                    system.surfaces[i].semi_aperture <= 0.0 &&
                    system.surfaces[i].aperture_radius > 0.0) {
                    system.surfaces[i].semi_aperture = system.surfaces[i].aperture_radius;
                }
                continue;
            }
            const double stop_radius = mode_uses_prescribed_stop_target(mode)
                                           ? tracer.stop_target_radius(
                                                 system.primary_wavelength(), mode)
                                           : max_heights[i];
            if (std::isfinite(stop_radius) && stop_radius > 0.0) {
                system.surfaces[i].semi_aperture = stop_radius;
                if (!system.surfaces[i].clear_aperture_fixed) {
                    system.surfaces[i].aperture_radius = stop_radius;
                }
                if (!system.surfaces[i].mechanical_aperture_fixed) {
                    system.surfaces[i].mechanical_aperture = stop_radius;
                }
            }
            continue;
        }
        if (system.surfaces[i].clear_aperture_fixed) {
            if (system.surfaces[i].semi_aperture <= 0.0 &&
                system.surfaces[i].aperture_radius > 0.0) {
                system.surfaces[i].semi_aperture = system.surfaces[i].aperture_radius;
            }
            continue;
        }
        if (system.surfaces[i].aperture_radius > 0.0) {
            system.surfaces[i].semi_aperture = std::max(original[i], system.surfaces[i].aperture_radius);
            continue;
        }
        if (max_heights[i] > 0.0) {
            system.surfaces[i].semi_aperture = max_heights[i] * scale;
        } else if (original[i] > 0.0) {
            system.surfaces[i].semi_aperture = original[i];
        }
    }
    update_mechanical_apertures_from_groups(system);
    // Evaluate physical support from the same trace history that built the
    // ordinary adaptive circular aperture.  The experiment measures, for every
    // non-zero field-side cut, how much of the requested pupil interval would
    // still pass that exact physical radius.
    const std::size_t support_wave_index =
        waves.empty() ? 0 : primary_wavelength_index(system, waves);
    const double support_tolerance = std::max(
        1.0e-9,
        vignetting_boundary_support_tolerance);
    const double support_selectivity_slope = std::max(
        0.0,
        vignetting_boundary_support_selectivity_slope);
    const bool physical_cut_match_contract =
        vignetting_physical_cut_match_enabled;
    for (std::size_t fi = 0; fi < fields.size() && fi < system.fields.size(); ++fi) {
        double side_deficits[2] = {
            std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity()};
        int side_surfaces[2] = {-1, -1};
        double side_candidate_radii[2] = {
            std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN()};
        double side_pretrim_radii[2] = {
            std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN()};
        double side_protection_radii[2] = {
            std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN()};
        double side_trim_windows[2] = {
            std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN()};
        const double cuts[2] = {
            std::max(0.0, fields[fi].fvdy + fields[fi].fvcy),
            std::max(0.0, fields[fi].fvdy - fields[fi].fvcy)};
        for (int side = 0; side < 2; ++side) {
            if (!waves.empty() && waves[support_wave_index].weight > 0.0) {
                const std::size_t wi = support_wave_index;
                for (std::size_t i = 0; i < system.surfaces.size(); ++i) {
                    const Surface& surface = system.surfaces[i];
                    if (surface.kind == SurfaceKind::Object ||
                        surface.kind == SurfaceKind::Image || surface.is_stop ||
                        surface.vignetting_aperture_excluded ||
                        !std::isfinite(surface.semi_aperture) ||
                        surface.semi_aperture <= kEps) {
                        continue;
                    }
                    const std::size_t support_index =
                        (((fi * waves.size() + wi) * 2 +
                          static_cast<std::size_t>(side)) *
                             support_surface_count) +
                        i;
                    const std::size_t opposite_index =
                        (((fi * waves.size() + wi) * 2 +
                          static_cast<std::size_t>(1 - side)) *
                             support_surface_count) +
                        i;
                    const std::size_t chief_index =
                        (fi * waves.size() + wi) * support_surface_count + i;
                    if (meridional_boundary_observed[support_index] &&
                        meridional_boundary_observed[opposite_index] &&
                        meridional_chief_observed[chief_index]) {
                        const double edge_height =
                            meridional_boundary_heights[support_index];
                        double evidence_deficit =
                            std::numeric_limits<double>::infinity();
                        if (vignetting_global_trim_realizability) {
                            const double keep_height = surface.semi_aperture;
                            const double edge_y =
                                meridional_boundary_y[support_index];
                            const double chief_y =
                                meridional_chief_y[chief_index];
                            if (std::isfinite(keep_height) &&
                                keep_height > kEps &&
                                std::isfinite(edge_y) &&
                                std::isfinite(chief_y)) {
                                const double owner_gap = std::max(
                                    0.0,
                                    (keep_height - edge_height) / keep_height);
                                const double retained_span = std::max(
                                    1.0e-9, 1.0 - fields[fi].fvdy);
                                const double edge_outward =
                                    side == 0 ? -edge_y : edge_y;
                                const double signed_outward_slope =
                                    side == 0
                                        ? (chief_y - edge_y) / retained_span
                                        : (edge_y - chief_y) / retained_span;
                                const double outward_slope = std::max(
                                    0.0, signed_outward_slope);
                                const double predicted_outside_height =
                                    edge_outward + outward_slope * cuts[side];
                                const double available_trim_window =
                                    (predicted_outside_height - keep_height) /
                                    keep_height;
                                constexpr double kHardApertureTraceSlack = 0.01;
                                const double required_trim_window =
                                    physical_cut_match_contract
                                        ? kHardApertureTraceSlack
                                        : kHardApertureTraceSlack +
                                              support_selectivity_slope * cuts[side];
                                // The production contract treats a radial gap
                                // within support_tolerance as ownership.  The
                                // strict physical-cut contract instead
                                // measures the pupil fraction that still fits
                                // between the retained boundary and the exact
                                // radius that the adaptive updater selected.
                                // Its zero therefore remains exact contact;
                                // support_tolerance is not a feasibility band.
                                const double ownership_excess =
                                    physical_cut_match_contract
                                        ? std::min(
                                              cuts[side],
                                              outward_slope > 1.0e-12
                                                  ? std::max(
                                                        0.0,
                                                        (keep_height -
                                                         edge_outward) /
                                                            outward_slope)
                                                  : cuts[side])
                                        : std::max(
                                              0.0,
                                              owner_gap - support_tolerance);
                                const double window_excess = std::max(
                                    0.0,
                                    required_trim_window -
                                        available_trim_window);
                                const double physical_side_excess = std::max(
                                    0.0, -edge_outward / keep_height);
                                double cumulative_rank_excess = 0.0;
                                const std::size_t first =
                                    system.first_optical_surface();
                                for (std::size_t sj = first; sj <= i; ++sj) {
                                    const std::size_t history_support_index =
                                        (((fi * waves.size() + wi) * 2 +
                                          static_cast<std::size_t>(side)) *
                                             support_surface_count) +
                                        sj;
                                    const std::size_t history_opposite_index =
                                        (((fi * waves.size() + wi) * 2 +
                                          static_cast<std::size_t>(1 - side)) *
                                             support_surface_count) +
                                        sj;
                                    const std::size_t history_chief_index =
                                        (fi * waves.size() + wi) *
                                            support_surface_count +
                                        sj;
                                    const double history_edge_y =
                                        meridional_boundary_y[
                                            history_support_index];
                                    const double history_opposite_y =
                                        meridional_boundary_y[
                                            history_opposite_index];
                                    const double history_chief_y =
                                        meridional_chief_y[history_chief_index];
                                    if (!std::isfinite(history_edge_y) ||
                                        !std::isfinite(history_opposite_y) ||
                                        !std::isfinite(history_chief_y)) {
                                        cumulative_rank_excess =
                                            std::numeric_limits<double>::infinity();
                                        break;
                                    }
                                    // Match d_accumulate_vignetting_boundary_support:
                                    // this is signed meridional-trio ordering,
                                    // not an additional full-pupil fold prior.
                                    const double competing_y =
                                        side == 0
                                            ? std::min(history_opposite_y,
                                                       history_chief_y)
                                            : std::max(history_opposite_y,
                                                       history_chief_y);
                                    const double history_scale = std::max(
                                        1.0e-9,
                                        system.surfaces[sj].semi_aperture);
                                    const double rank_gap =
                                        side == 0
                                            ? history_edge_y - competing_y
                                            : competing_y - history_edge_y;
                                    constexpr double kOrderNumericalSlack =
                                        1.0e-6;
                                    cumulative_rank_excess = std::max(
                                        cumulative_rank_excess,
                                        std::max(
                                            0.0,
                                            rank_gap / history_scale -
                                                kOrderNumericalSlack));
                                }
                                const double raw_evidence_deficit = std::sqrt(
                                    ownership_excess * ownership_excess +
                                    window_excess * window_excess +
                                    physical_side_excess *
                                        physical_side_excess +
                                    cumulative_rank_excess *
                                        cumulative_rank_excess);
                                // A physical cut can be unsupported only over
                                // its requested pupil interval. Radial window
                                // and ordering evidence must not impose a
                                // finite error as that interval tends to zero.
                                // In particular, the fixed 1% trace slack is
                                // not an additional pupil cut to be realized.
                                evidence_deficit = physical_cut_match_contract &&
                                        system.primary_wavelength_apertures
                                    ? std::min(cuts[side], raw_evidence_deficit)
                                    : raw_evidence_deficit;
                            }
                        } else {
                            const double competing_height = std::max(
                                meridional_boundary_heights[opposite_index],
                                meridional_chief_heights[chief_index]);
                            const double edge_gap = std::abs(
                                surface.semi_aperture - edge_height) /
                                surface.semi_aperture;
                            const double competing_clearance =
                                (surface.semi_aperture - competing_height) /
                                surface.semi_aperture;
                            const double required_clearance =
                                support_tolerance +
                                support_selectivity_slope * cuts[side];
                            const double contact_excess = std::max(
                                0.0, edge_gap - support_tolerance);
                            const double selectivity_excess = std::max(
                                0.0,
                                required_clearance - competing_clearance);
                            evidence_deficit = std::hypot(
                                contact_excess, selectivity_excess);
                        }
                        if (std::isfinite(evidence_deficit) &&
                            evidence_deficit < side_deficits[side]) {
                            side_deficits[side] = evidence_deficit;
                            side_surfaces[side] = static_cast<int>(i);
                            if (physical_cut_match_contract) {
                                // Materialization freezes the same adaptive
                                // circular radius used by the residual.
                                side_candidate_radii[side] =
                                    surface.semi_aperture;
                                side_pretrim_radii[side] =
                                    surface.semi_aperture;
                                side_protection_radii[side] =
                                    surface.semi_aperture;
                                side_trim_windows[side] =
                                    cuts[side] - evidence_deficit;
                            }
                        }
                    }
                }
            }
        }
        system.fields[fi].vignetting_negative_boundary_support_gap =
            std::isfinite(side_deficits[0])
                ? side_deficits[0]
                : std::numeric_limits<double>::quiet_NaN();
        system.fields[fi].vignetting_positive_boundary_support_gap =
            std::isfinite(side_deficits[1])
                ? side_deficits[1]
                : std::numeric_limits<double>::quiet_NaN();
        system.fields[fi].vignetting_negative_boundary_support_surface =
            side_surfaces[0];
        system.fields[fi].vignetting_positive_boundary_support_surface =
            side_surfaces[1];
        system.fields[fi].vignetting_negative_boundary_candidate_radius =
            side_candidate_radii[0];
        system.fields[fi].vignetting_positive_boundary_candidate_radius =
            side_candidate_radii[1];
        system.fields[fi].vignetting_negative_boundary_pretrim_radius =
            side_pretrim_radii[0];
        system.fields[fi].vignetting_positive_boundary_pretrim_radius =
            side_pretrim_radii[1];
        system.fields[fi].vignetting_negative_boundary_protection_radius =
            side_protection_radii[0];
        system.fields[fi].vignetting_positive_boundary_protection_radius =
            side_protection_radii[1];
        system.fields[fi].vignetting_negative_boundary_trim_window =
            side_trim_windows[0];
        system.fields[fi].vignetting_positive_boundary_trim_window =
            side_trim_windows[1];
    }
    if (aperture_debug) {
        std::fprintf(stderr, "CPU aperture debug surface_count=%zu\n", system.surfaces.size());
        for (std::size_t i = 0; i < system.surfaces.size(); ++i) {
            std::fprintf(
                stderr,
                "  i=%zu max_height=%.17g field=%zu wave=%zu pupil=%zu "
                "px=%.17g py=%.17g semi=%.17g mechanical=%.17g\n",
                i,
                max_heights[i],
                max_height_fields[i],
                max_height_waves[i],
                max_height_pupils[i],
                max_height_px[i],
                max_height_py[i],
                system.surfaces[i].semi_aperture,
                system.surfaces[i].mechanical_aperture);
        }
    }
    system.aperture.type = original_aperture_type;
}

}  // namespace optiland
