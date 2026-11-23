// Construct ABCD chief estimates and search the bounded chief-anchor seed families.
// AimingSession model preparation uses these solves before fitting calibrated edge behavior.
// Preserve seed traversal, residual comparisons, continuation order, and successful-anchor reuse.

#include "aiming_geometry_inline.hpp"
#include "asphere_domain_inline.hpp"
#include "asphere_refinement_inline.hpp"
#include "asphere_topology_inline.hpp"
#include "diagnostics_inline.hpp"
#include "prepared_geometry_inline.hpp"
#include "detail.hpp"
#include "calibration/budget_chief_append_same_field_cached_chief_seeds.hpp"
#include "calibration/budget_chief_append_field_continuation_seeds.hpp"
#include "calibration/budget_chief_solve_budget_chief_fixed_or_secant.hpp"
#include "calibration/budget_chief_try_trial_seed_range_for_field.hpp"
#include "calibration/budget_chief_append_angle_bridge_seeds.hpp"

namespace optiland {
using namespace raytrace_detail;

AimingSession::FirstSurfaceSeed AimingSession::shifted_chief_seed_by_transverse_radius(
    const Field& field,
    const Wavelength& wave,
    const FirstSurfaceSeed& chief_seed,
    const PupilPoint& edge,
    double beam_radius) const {
    if (!chief_seed.valid ||
        !std::isfinite(chief_seed.x) ||
        !std::isfinite(chief_seed.y) ||
        !std::isfinite(beam_radius) ||
        beam_radius <= 0.0) {
        return {};
    }
    const std::size_t first = system_.first_ray_aiming_surface(wave.value_um);
    if (first >= system_.surfaces.size()) {
        return {};
    }
    const Surface& first_surface = system_.surfaces[first];
    const double chief_sag = surface_sag(first_surface, chief_seed.x, chief_seed.y);
    if (!std::isfinite(chief_sag)) {
        return {};
    }

    Ray chief_ray = tracer_.generate_ray_through_first_surface_point(
        field,
        wave.value_um,
        chief_seed.x,
        chief_seed.y,
        1.0);
    Vec3 direction = chief_ray.valid ? normalize(chief_ray.d)
                                     : field_direction_cosines(system_, field);
    if (!std::isfinite(direction.x) ||
        !std::isfinite(direction.y) ||
        !std::isfinite(direction.z) ||
        norm(direction) <= kEps) {
        return {};
    }
    direction = normalize(direction);

    const double field_radius = std::hypot(field.hx, field.hy);
    double mer_x = 0.0;
    double mer_y = 1.0;
    if (field_radius > 1.0e-12) {
        mer_x = field.hx / field_radius;
        mer_y = field.hy / field_radius;
    }
    const double transverse_sin = std::hypot(direction.x, direction.y);
    const Vec3 meridional = normalize({
        direction.z * mer_x,
        direction.z * mer_y,
        -transverse_sin,
    });
    const Vec3 sagittal = normalize({mer_y, -mer_x, 0.0});
    Vec3 transverse = sagittal * edge.px + meridional * edge.py;
    const double transverse_norm = norm(transverse);
    if (!std::isfinite(transverse_norm) || transverse_norm <= 1.0e-12) {
        return {};
    }
    transverse = transverse * (1.0 / transverse_norm);

    const Vec3 chief_point{chief_seed.x, chief_seed.y, chief_sag};
    const Vec3 shifted_point = chief_point + transverse * beam_radius;
    const double backstep = std::max(1.0, 4.0 * first_surface_search_radius(wave.value_um));
    Ray local_ray;
    local_ray.valid = true;
    local_ray.p = shifted_point - direction * backstep;
    local_ray.d = direction;
    local_ray.wavelength_um = wave.value_um;
    local_ray.nominal_weight = std::max(0.0, edge.weight);
    local_ray.intensity = local_ray.nominal_weight;

    double t = 0.0;
    bool local_hit = false;
    if (first_surface.kind == SurfaceKind::EvenAsphere &&
        asphere_root_policy() ==
            AsphereRootPolicy::SlopeDomainTopologySeed) {
        local_hit = refine_production_asphere_root_fast(
            first_surface, local_ray, backstep, &t);
        if (local_hit) {
            const double domain_radius_squared =
                production_asphere_slope_domain_radius_squared(first_surface);
            if (std::isfinite(domain_radius_squared)) {
                const double hit_x = local_ray.p.x + t * local_ray.d.x;
                const double hit_y = local_ray.p.y + t * local_ray.d.y;
                const double hit_radius_squared =
                    hit_x * hit_x + hit_y * hit_y;
                local_hit = std::isfinite(hit_radius_squared) &&
                    hit_radius_squared <=
                        domain_radius_squared * (1.0 + 2.0e-12);
            }
        }
        if (!local_hit) {
            local_hit = intersect_surface(first_surface, local_ray, &t);
        }
    } else if (first_surface.kind == SurfaceKind::EvenAsphere &&
               asphere_root_policy() ==
                   AsphereRootPolicy::UnifiedCertifiedExperimental) {
        local_hit = intersect_surface(first_surface, local_ray, &t);
    } else {
        local_hit =
            first_surface.kind == SurfaceKind::EvenAsphere
                ? intersect_even_asphere_from_local_seed(
                      first_surface, local_ray, backstep, &t)
                : intersect_surface(first_surface, local_ray, &t);
    }
    if (!local_hit ||
        !std::isfinite(t) ||
        t < -1.0e-9) {
        return {};
    }
    const Vec3 hit = local_ray.p + local_ray.d * t;
    if (!std::isfinite(hit.x) ||
        !std::isfinite(hit.y) ||
        (!system_.defer_secondary_aperture_clipping(wave.value_um) &&
         !surface_aperture_allows(first_surface, hit.x, hit.y))) {
        return {};
    }
    return {hit.x, hit.y, true};
}

AimingSession::FirstSurfaceSeed AimingSession::estimate_abcd_chief_seed(
    const Field& field,
    const Wavelength& wave,
    bool* singular) const {
    if (singular) {
        *singular = false;
    }
    const auto& cfg = system_.ray_aiming.calibrated_paraxial;
    double a = 0.0;
    double b = 0.0;
    if (!first_surface_to_stop_abcd(
            system_,
            wave.value_um,
            tracer_.stop_surface_index(),
            &a,
            &b) ||
        !std::isfinite(a) || std::abs(a) < std::max(1.0e-12, cfg.budget_chief_min_a)) {
        if (singular) {
            *singular = true;
        }
        return {};
    }

    const Vec3 direction = field_direction_cosines(system_, field);
    if (!std::isfinite(direction.z) || std::abs(direction.z) <= kEps) {
        return {};
    }
    const double ux = direction.x / direction.z;
    const double uy = direction.y / direction.z;
    const double rs = tracer_.stop_target_radius(wave.value_um, RayAimingMode::CalibratedParaxial);
    const double target_x = field.fvcx * rs;
    const double target_y = field.fvcy * rs;
    FirstSurfaceSeed seed{
        (target_x - b * ux) / a,
        (target_y - b * uy) / a,
        true};
    if (!std::isfinite(seed.x) || !std::isfinite(seed.y)) {
        return {};
    }
    return seed;
}

AimingSession::ChiefAnchor AimingSession::try_abcd_budget_chief_anchor(
    std::size_t field_index,
    std::size_t wave_index,
    const Field& field,
    const Wavelength& wave) {
    auto& diag = diagnostics_state();
    add_diag(diag.calibrated_budget_chief_attempts);
    ScopedDiagTimer timer(diag.calibrated_budget_chief_ns);

    ChiefAnchor anchor;
    const auto& cfg = system_.ray_aiming.calibrated_paraxial;
    const PupilPoint chief{0.0, 0.0, 1.0};
    const double field_angle_deg =
        std::hypot(field.hx, field.hy) * std::max(0.0, system_.max_field_deg);
    auto should_skip_abcd_seed_for_angle = [&](double angle_deg) {
        return cfg.budget_chief_skip_abcd_angle_deg > 0.0 &&
               angle_deg > cfg.budget_chief_skip_abcd_angle_deg + 1.0e-9;
    };
    const bool skip_abcd_seed = should_skip_abcd_seed_for_angle(field_angle_deg);
    bool singular = false;
    FirstSurfaceSeed seed;
    if (!skip_abcd_seed) {
        seed = estimate_abcd_chief_seed(field, wave, &singular);
    }
    if (singular) {
        add_diag(diag.calibrated_budget_chief_singular);
    }
    const bool abcd_seed_invalid = !skip_abcd_seed && !seed.valid;
    const bool abcd_seed_unusable = skip_abcd_seed || abcd_seed_invalid;
    if (!abcd_seed_unusable) {
        add_diag(diag.calibrated_budget_chief_seed_valid);
    }

    const double hard_radius = first_surface_hard_radius(wave.value_um);
    const double soft_radius = first_surface_search_radius(wave.value_um);
    const double radius_limit = hard_radius > 0.0 ? hard_radius : soft_radius;
    const bool abcd_seed_oob =
        !abcd_seed_unusable &&
        radius_limit > 0.0 &&
        (hard_radius > 0.0
             ? aperture_clip_exceeded(std::hypot(seed.x, seed.y), hard_radius)
             : std::hypot(seed.x, seed.y) > radius_limit * (1.0 + 1.0e-10));
    if (abcd_seed_oob) {
        add_diag(diag.calibrated_budget_chief_oob);
    }

    const int iterations = std::max(0, cfg.budget_chief_iterations);
    const double rs = tracer_.stop_target_radius(wave.value_um, RayAimingMode::CalibratedParaxial);
    const double stop_scale = std::max(1.0e-9, std::abs(rs));
    const double target_x = field.fvcx * rs;
    const double target_y = field.fvcy * rs;
    const bool budget_chief_ignore_clip = cfg.budget_chief_ignore_aperture_clip;
    const double limit = std::max(
        system_.ray_aiming.tolerance,
        cfg.budget_chief_accept_stop_fraction * stop_scale);
    const std::string& budget_solver = cfg.budget_chief_solver;
    const bool hybrid_budget_solver =
        budget_solver.empty() ||
        budget_solver == "auto" ||
        budget_solver == "hybrid" ||
        budget_solver == "iterative_secant" ||
        budget_solver == "iterative_secant_fallback";
    const bool iterative_budget_solver =
        hybrid_budget_solver ||
        budget_solver == "iterative" ||
        budget_solver == "newton";
    const bool fixed_budget_solver =
        budget_solver == "fixed_jacobian" ||
        budget_solver == "fixed_jacobian_only" ||
        budget_solver == "fixed" ||
        budget_solver == "linear" ||
        budget_solver == "one_step";
    const bool force_seed_space_budget_solver =
        (abcd_seed_unusable || abcd_seed_oob) && iterative_budget_solver;
    const bool secant_budget_solver =
        // CUDA calibrated scoring uses the secant/Broyden chief solve for the
        // supported hybrid modes. Prefer the same path on CPU when the ABCD
        // inverse is usable, with the iterative path remaining the fallback.
        hybrid_budget_solver || force_seed_space_budget_solver ||
        budget_solver == "secant" ||
        budget_solver == "broyden" ||
        budget_solver == "secant_only" ||
        budget_solver == "broyden_only";
    double abcd_a = 0.0;
    double abcd_b = 0.0;
    const bool has_abcd_inverse =
        (fixed_budget_solver || secant_budget_solver) &&
        first_surface_to_stop_abcd(
            system_,
            wave.value_um,
            tracer_.stop_surface_index(),
            &abcd_a,
            &abcd_b) &&
        std::isfinite(abcd_a) &&
        std::abs(abcd_a) >= std::max(1.0e-12, cfg.budget_chief_min_a);
    auto stop_radius = [&]() {
        double radius = stop_scale;
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
    const double convergence_limit = std::max(
        std::max(0.0, system_.ray_aiming.tolerance),
        std::max(0.0, cfg.budget_chief_residual_stop_fraction) * stop_radius());
    struct ChiefTargetParams {
        double x = 0.0;
        double y = 0.0;
    };
    auto target_params_for = [&](const Field& solve_field) {
        return ChiefTargetParams{
            solve_field.fvcx * rs,
            solve_field.fvcy * rs,
        };
    };
    auto seed_in_first_surface_bounds = [&](const FirstSurfaceSeed& item) {
        if (!item.valid || !std::isfinite(item.x) || !std::isfinite(item.y)) {
            return false;
        }
        if (hard_radius > 0.0) {
            return !aperture_clip_exceeded(std::hypot(item.x, item.y), hard_radius);
        }
        return true;
    };
    auto generate_chief_from_seed = [&](const Field& solve_field, const FirstSurfaceSeed& item) {
        return tracer_.generate_ray_through_first_surface_point(
            solve_field,
            wave.value_um,
            item.x,
            item.y,
            1.0);
    };
    auto stamp_budget_solver_ray = [&](Ray out,
                                       double residual,
                                       int used_iterations,
                                       bool converged) {
        out.aim_iterations = used_iterations;
        out.aim_residual = std::isfinite(residual)
                               ? residual
                               : std::numeric_limits<double>::infinity();
        out.aim_target_radius = rs;
        out.aim_converged = converged;
        out.homotopy_fraction = 1.0;
        return out;
    };
    struct BudgetChiefSolveResult {
        Ray ray;
        bool stop_valid = false;
        double stop_x = 0.0;
        double stop_y = 0.0;
    };
    auto solve_budget_chief_fixed_or_secant = [&](const Field& solve_field,
                                                  const ChiefTargetParams& solve_target,
                                                  const FirstSurfaceSeed& start_seed,
                                                  bool use_secant) {
        return ::optiland::raytrace_detail::BudgetChiefSolveBudgetChiefFixedOrSecant::run<ChiefTargetParams, BudgetChiefSolveResult>(*this, 
            wave,
            iterations,
            budget_chief_ignore_clip,
            abcd_a,
            convergence_limit,
            seed_in_first_surface_bounds,
            generate_chief_from_seed,
            stamp_budget_solver_ray,
            solve_field,
            solve_target,
            start_seed,
            use_secant
        );
    };
    auto inward_seed = [](const FirstSurfaceSeed& item, double distance) {
        FirstSurfaceSeed out = item;
        if (!item.valid || distance <= 0.0) {
            return out;
        }
        const double r = std::hypot(item.x, item.y);
        if (!std::isfinite(r) || r <= 1.0e-12) {
            return out;
        }
        const double new_r = std::max(0.0, r - distance);
        const double scale = new_r / r;
        out.x = item.x * scale;
        out.y = item.y * scale;
        return out;
    };
    const std::string& chief_mode = cfg.chief_anchor_mode;
    const bool use_angle_bridge =
        chief_mode == "abcd_budget_angle_bridge" ||
        chief_mode == "budget_abcd_angle_bridge" ||
        chief_mode == "abcd_budget_angle_bridge_fast" ||
        chief_mode == "budget_abcd_angle_bridge_fast" ||
        chief_mode == "abcd_budget_angle_bridge_then_full" ||
        chief_mode == "budget_abcd_angle_bridge_then_full" ||
        chief_mode == "abcd_budget_angle_bridge_fast_then_full" ||
        chief_mode == "budget_abcd_angle_bridge_fast_then_full";
    const bool use_fast_angle_bridge =
        chief_mode == "abcd_budget_angle_bridge_fast" ||
        chief_mode == "budget_abcd_angle_bridge_fast" ||
        chief_mode == "abcd_budget_angle_bridge_fast_then_full" ||
        chief_mode == "budget_abcd_angle_bridge_fast_then_full";
    const bool use_field_continuation =
        chief_mode == "abcd_budget_field_continuation" ||
        chief_mode == "budget_abcd_field_continuation" ||
        chief_mode == "abcd_budget_field_extrapolate" ||
        chief_mode == "budget_abcd_field_extrapolate" ||
        chief_mode == "abcd_budget_field_continuation_then_full" ||
        chief_mode == "budget_abcd_field_continuation_then_full" ||
        use_angle_bridge;
    const bool use_inward_scan =
        chief_mode == "abcd_budget_inward" ||
        chief_mode == "budget_abcd_inward" ||
        chief_mode == "abcd_budget_center_inward" ||
        chief_mode == "budget_abcd_center_inward" ||
        chief_mode == "abcd_budget_inward_wide" ||
        chief_mode == "budget_abcd_inward_wide" ||
        chief_mode == "abcd_budget_center_inward_wide" ||
        chief_mode == "budget_abcd_center_inward_wide" ||
        use_field_continuation;
    const bool use_wide_inward_scan =
        chief_mode == "abcd_budget_inward_wide" ||
        chief_mode == "budget_abcd_inward_wide" ||
        chief_mode == "abcd_budget_center_inward_wide" ||
        chief_mode == "budget_abcd_center_inward_wide" ||
        use_field_continuation;
    static constexpr std::array<double, 15> inward_factors{{
        0.0, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75,
        2.0, 2.25, 2.5, 3.0, 3.5, 4.0, 4.5, 5.0}};
    const std::size_t inward_factor_count = use_wide_inward_scan ? 15 : (use_inward_scan ? 11 : 1);
    double center_radius_value = 0.0;
    bool center_radius_ready = !use_inward_scan;
    auto center_radius = [&]() {
        if (!center_radius_ready) {
            center_radius_value = center_meridional_radius(wave);
            center_radius_ready = true;
        }
        return center_radius_value;
    };
    std::vector<FirstSurfaceSeed> trial_seeds;
    trial_seeds.reserve(64);
    const bool prefer_field_continuation =
        use_field_continuation &&
        (abcd_seed_unusable ||
         abcd_seed_oob ||
         field_angle_deg >= cfg.budget_chief_field_continuation_angle_deg);
    const bool prefer_angle_bridge =
        use_angle_bridge &&
        (abcd_seed_unusable || abcd_seed_oob || field_angle_deg > 35.0 + 1.0e-9);
    auto append_clipped_seed = [&](std::vector<FirstSurfaceSeed>& seeds, FirstSurfaceSeed item) {
        if (!item.valid || !std::isfinite(item.x) || !std::isfinite(item.y)) {
            return;
        }
        if (hard_radius > 0.0) {
            const double r = std::hypot(item.x, item.y);
            if (std::isfinite(r) && aperture_clip_exceeded(r, hard_radius)) {
                const double scale = (1.01 * hard_radius * (1.0 - 1.0e-8)) / r;
                item.x *= scale;
                item.y *= scale;
            }
        }
        append_seed_with_bounds(seeds, item, hard_radius, soft_radius);
    };
    auto append_field_seed = [&](FirstSurfaceSeed item) {
        append_clipped_seed(trial_seeds, item);
    };
    auto append_same_field_cached_chief_seeds = [&]() {
        return ::optiland::raytrace_detail::BudgetChiefAppendSameFieldCachedChiefSeeds::run(*this, field_index, wave_index, wave, append_field_seed);
    };
    auto append_boundary_scan_to = [&](std::vector<FirstSurfaceSeed>& seeds,
                                       const FirstSurfaceSeed& start,
                                       double dx,
                                       double dy) {
        if (!start.valid || !std::isfinite(start.x) || !std::isfinite(start.y)) {
            return;
        }
        double norm = std::hypot(dx, dy);
        if (!std::isfinite(norm) || norm <= 1.0e-12) {
            return;
        }
        dx /= norm;
        dy /= norm;
        const double scan_radius = hard_radius > 0.0 ? hard_radius : soft_radius;
        if (!(scan_radius > 0.0)) {
            return;
        }
        const double boundary = scan_radius * (1.0 - 1.0e-8);
        const double b = start.x * dx + start.y * dy;
        const double c = start.x * start.x + start.y * start.y - boundary * boundary;
        const double disc = b * b - c;
        if (!std::isfinite(disc) || disc <= 0.0) {
            return;
        }
        const double distance = -b + std::sqrt(disc);
        if (!std::isfinite(distance) || distance <= 0.0) {
            return;
        }
        for (const double frac : std::array<double, 4>{{0.25, 0.5, 0.75, 1.0}}) {
            append_clipped_seed(
                seeds,
                {start.x + frac * distance * dx,
                 start.y + frac * distance * dy,
                 true});
        }
    };
    auto append_boundary_scan = [&](const FirstSurfaceSeed& start, double dx, double dy) {
        append_boundary_scan_to(trial_seeds, start, dx, dy);
    };
    auto scale_seed_radial = [&](const FirstSurfaceSeed& item, double factor) {
        if (!item.valid || !std::isfinite(factor) || factor <= 0.0) {
            return FirstSurfaceSeed{};
        }
        return FirstSurfaceSeed{item.x * factor, item.y * factor, true};
    };
    auto field_rho = [&](const Field& solve_field) {
        return std::hypot(solve_field.hx, solve_field.hy);
    };
    auto same_field_direction = [&](const Field& a, const Field& b) {
        const double ra = field_rho(a);
        const double rb = field_rho(b);
        if (ra <= 1.0e-12 || rb <= 1.0e-12) {
            return true;
        }
        const double cross = std::abs(a.hx * b.hy - a.hy * b.hx);
        const double dot = a.hx * b.hx + a.hy * b.hy;
        return dot >= 0.0 && cross <= 1.0e-6 * ra * rb;
    };
    auto interpolate_field_by_rho = [&](const Field& inner_field,
                                        double inner_rho,
                                        const Field& outer_field,
                                        double outer_rho,
                                        double target_rho_value) {
        const double denom = std::max(1.0e-12, outer_rho - inner_rho);
        const double t = std::clamp((target_rho_value - inner_rho) / denom, 0.0, 1.0);
        Field out = inner_field;
        out.hx = inner_field.hx + t * (outer_field.hx - inner_field.hx);
        out.hy = inner_field.hy + t * (outer_field.hy - inner_field.hy);
        out.fvcx = inner_field.fvcx + t * (outer_field.fvcx - inner_field.fvcx);
        out.fvcy = inner_field.fvcy + t * (outer_field.fvcy - inner_field.fvcy);
        out.fvdx = inner_field.fvdx + t * (outer_field.fvdx - inner_field.fvdx);
        out.fvdy = inner_field.fvdy + t * (outer_field.fvdy - inner_field.fvdy);
        out.vignette_x =
            inner_field.vignette_x + t * (outer_field.vignette_x - inner_field.vignette_x);
        out.vignette_y =
            inner_field.vignette_y + t * (outer_field.vignette_y - inner_field.vignette_y);
        return out;
    };
    auto remember_bridge_anchor = [&](const Field& bridge_field,
                                      double bridge_rho,
                                      const ChiefAnchor& bridge_anchor) {
        if (!bridge_anchor.valid) {
            return;
        }
        for (auto& entry : chief_bridge_anchors_) {
            if (entry.wave_index != wave_index || !entry.anchor.valid) {
                continue;
            }
            if (!same_field_direction(entry.field, bridge_field)) {
                continue;
            }
            if (std::abs(entry.rho - bridge_rho) > 1.0e-9) {
                continue;
            }
            entry.field = bridge_field;
            entry.rho = bridge_rho;
            entry.anchor = bridge_anchor;
            return;
        }
        chief_bridge_anchors_.push_back(
            ChiefBridgeAnchor{wave_index, bridge_field, bridge_rho, bridge_anchor});
    };
    auto append_field_continuation_seeds = [&]() {
        return ::optiland::raytrace_detail::BudgetChiefAppendFieldContinuationSeeds::run(*this, field_index, wave_index, wave, append_field_seed, append_boundary_scan);
    };
    enum class BudgetChiefRejectReason {
        EmptySeeds,
        TraceFail,
        StopOutOfBounds,
        ResidualReject,
        SeedExtractFail,
    };

    auto try_trial_seed_range_for_field = [&](const Field& solve_field,
                                              const ChiefTargetParams& solve_target,
                                              const std::vector<FirstSurfaceSeed>& seeds,
                                              std::size_t begin,
                                              std::size_t end,
                                              BudgetChiefRejectReason* reason,
                                              ChiefAnchor* solved_anchor) {
        return ::optiland::raytrace_detail::BudgetChiefTryTrialSeedRangeForField::run<ChiefTargetParams, BudgetChiefSolveResult, BudgetChiefRejectReason>(*this, 
            wave,
            diag,
            cfg,
            chief,
            abcd_seed_unusable,
            iterations,
            budget_chief_ignore_clip,
            limit,
            fixed_budget_solver,
            secant_budget_solver,
            has_abcd_inverse,
            stop_radius,
            convergence_limit,
            solve_budget_chief_fixed_or_secant,
            solve_field,
            solve_target,
            seeds,
            begin,
            end,
            reason,
            solved_anchor
        );
    };
    auto try_trial_seed_range = [&](std::size_t begin,
                                    std::size_t end,
                                    BudgetChiefRejectReason* reason) {
        return try_trial_seed_range_for_field(
            field,
            target_params_for(field),
            trial_seeds,
            begin,
            end,
            reason,
            &anchor);
    };
    auto append_angle_bridge_seeds = [&](bool allow_virtual_bridges) {
        return ::optiland::raytrace_detail::BudgetChiefAppendAngleBridgeSeeds::run<BudgetChiefRejectReason>(*this, 
            field_index,
            wave_index,
            field,
            wave,
            should_skip_abcd_seed_for_angle,
            seed,
            target_params_for,
            inward_seed,
            center_radius,
            trial_seeds,
            append_clipped_seed,
            append_field_seed,
            append_boundary_scan_to,
            scale_seed_radial,
            field_rho,
            same_field_direction,
            interpolate_field_by_rho,
            remember_bridge_anchor,
            try_trial_seed_range_for_field,
            allow_virtual_bridges
        );
    };

    // Initial fast-bridge candidate generation does not solve virtual fields or
    // write chief anchors. Consume each completed prefix before preparing the
    // next family, preserving candidate order, dedup history and failure reason.
    // Retain eager preparation where virtual bridge generation has side effects.
    const bool consume_initial_prefixes = !use_angle_bridge || use_fast_angle_bridge;
    std::size_t initial_tried = 0;
    BudgetChiefRejectReason initial_reason = BudgetChiefRejectReason::EmptySeeds;
    auto rejection_rank = [](BudgetChiefRejectReason reason) {
        switch (reason) {
        case BudgetChiefRejectReason::EmptySeeds: return 0;
        case BudgetChiefRejectReason::TraceFail: return 1;
        case BudgetChiefRejectReason::SeedExtractFail: return 2;
        case BudgetChiefRejectReason::ResidualReject: return 3;
        case BudgetChiefRejectReason::StopOutOfBounds: return 4;
        }
        return 0;
    };
    auto try_initial_prefix = [&]() {
        if (initial_tried == trial_seeds.size()) return false;
        BudgetChiefRejectReason reason = BudgetChiefRejectReason::EmptySeeds;
        const bool success = try_trial_seed_range(initial_tried, trial_seeds.size(), &reason);
        initial_tried = trial_seeds.size();
        if (rejection_rank(reason) > rejection_rank(initial_reason)) initial_reason = reason;
        return success;
    };
    bool angle_bridge_appended = false;
    bool field_continuation_appended = false;
    if (abcd_seed_unusable || abcd_seed_oob || prefer_angle_bridge || prefer_field_continuation) {
        append_same_field_cached_chief_seeds();
    }
    if (consume_initial_prefixes && try_initial_prefix()) {
        chief_anchors_[FieldWaveKey{field_index, wave_index}] = anchor;
        return anchor;
    }
    if (prefer_angle_bridge) {
        const std::size_t before = trial_seeds.size();
        append_angle_bridge_seeds(use_fast_angle_bridge ? false : true);
        angle_bridge_appended = trial_seeds.size() > before;
    }
    if (consume_initial_prefixes && try_initial_prefix()) {
        chief_anchors_[FieldWaveKey{field_index, wave_index}] = anchor;
        return anchor;
    }
    if (prefer_field_continuation || (prefer_angle_bridge && !angle_bridge_appended)) {
        const std::size_t before = trial_seeds.size();
        append_field_continuation_seeds();
        field_continuation_appended = trial_seeds.size() > before;
    }
    if (consume_initial_prefixes && try_initial_prefix()) {
        chief_anchors_[FieldWaveKey{field_index, wave_index}] = anchor;
        return anchor;
    }
    if (!abcd_seed_unusable && !abcd_seed_oob) {
        for (std::size_t factor_index = 0; factor_index < inward_factor_count; ++factor_index) {
            const double factor = inward_factors[factor_index];
            append_seed_with_bounds(trial_seeds, inward_seed(seed, factor * center_radius()), hard_radius, soft_radius);
        }
    }
    if (use_angle_bridge && !prefer_angle_bridge && trial_seeds.empty()) {
        const std::size_t before = trial_seeds.size();
        append_angle_bridge_seeds(use_fast_angle_bridge ? false : true);
        angle_bridge_appended = trial_seeds.size() > before;
    }
    if (use_field_continuation && !prefer_field_continuation && trial_seeds.empty()) {
        const std::size_t before = trial_seeds.size();
        append_field_continuation_seeds();
        field_continuation_appended = trial_seeds.size() > before;
    }

    if (try_initial_prefix()) {
        chief_anchors_[FieldWaveKey{field_index, wave_index}] = anchor;
        return anchor;
    }
    BudgetChiefRejectReason reject_reason = initial_reason;

    if (use_angle_bridge) {
        const std::size_t retry_begin = trial_seeds.size();
        if (use_fast_angle_bridge) {
            append_angle_bridge_seeds(true);
        } else {
            append_field_continuation_seeds();
        }
        if (try_trial_seed_range(retry_begin, trial_seeds.size(), &reject_reason)) {
            chief_anchors_[FieldWaveKey{field_index, wave_index}] = anchor;
            return anchor;
        }
    }

    if (use_field_continuation &&
        !field_continuation_appended &&
        (reject_reason == BudgetChiefRejectReason::TraceFail ||
         reject_reason == BudgetChiefRejectReason::EmptySeeds)) {
        const std::size_t retry_begin = trial_seeds.size();
        append_field_continuation_seeds();
        field_continuation_appended = true;
        if (try_trial_seed_range(retry_begin, trial_seeds.size(), &reject_reason)) {
            chief_anchors_[FieldWaveKey{field_index, wave_index}] = anchor;
            return anchor;
        }
    }

    if (abcd_seed_invalid && !skip_abcd_seed && (use_angle_bridge || use_field_continuation)) {
        add_diag(diag.calibrated_chief_anchor_requests);
        const bool chief_anchor_cached =
            chief_anchors_.find(FieldWaveKey{field_index, wave_index}) != chief_anchors_.end();
        add_diag(chief_anchor_cached ? diag.calibrated_chief_anchor_cache_hits
                                     : diag.calibrated_chief_anchor_builds);
        ChiefAnchor fallback_anchor;
        {
            ScopedDiagTimer chief_timer(diag.calibrated_chief_anchor_ns);
            fallback_anchor = ensure_chief_anchor(field_index, wave_index, field, wave);
        }
        add_diag(fallback_anchor.valid ? diag.calibrated_chief_anchor_valid
                                       : diag.calibrated_chief_anchor_invalid);
        if (fallback_anchor.valid) {
            double sx = 0.0;
            double sy = 0.0;
            if (sample_stop_from_ray(
                    fallback_anchor.ray,
                    &sx,
                    &sy,
                    budget_chief_ignore_clip,
                    budget_chief_ignore_clip)) {
                const double residual = std::hypot(sx - target_x, sy - target_y);
                if (std::isfinite(residual) && residual <= convergence_limit) {
                    fallback_anchor.stop_valid = true;
                    fallback_anchor.stop_x = sx;
                    fallback_anchor.stop_y = sy;
                    fallback_anchor.stop_residual = residual;
                    add_diag_residual(
                        diag.calibrated_budget_chief_residual_count,
                        diag.calibrated_budget_chief_residual_sum_scaled,
                        diag.calibrated_budget_chief_residual_max_scaled,
                        residual);
                    add_diag(diag.calibrated_budget_chief_accepted);
                    chief_anchors_[FieldWaveKey{field_index, wave_index}] = fallback_anchor;
                    return fallback_anchor;
                }
                reject_reason = BudgetChiefRejectReason::ResidualReject;
            } else {
                reject_reason = BudgetChiefRejectReason::TraceFail;
            }
        }
    }

    if (reject_reason == BudgetChiefRejectReason::TraceFail) {
        add_diag(diag.calibrated_budget_chief_trace_fail);
    } else if (reject_reason == BudgetChiefRejectReason::StopOutOfBounds) {
        add_diag(diag.calibrated_budget_chief_stop_oob_reject);
    } else if (reject_reason == BudgetChiefRejectReason::ResidualReject) {
        add_diag(diag.calibrated_budget_chief_residual_reject);
    } else if (reject_reason == BudgetChiefRejectReason::SeedExtractFail) {
        add_diag(diag.calibrated_budget_chief_seed_extract_fail);
    }
    add_diag(diag.calibrated_budget_chief_rejected);
    return anchor;
}

} // namespace optiland
