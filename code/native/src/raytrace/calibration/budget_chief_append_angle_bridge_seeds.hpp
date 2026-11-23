#pragma once
#include "../detail.hpp"

namespace optiland::raytrace_detail {

struct BudgetChiefAppendAngleBridgeSeeds {
    using ChiefAnchor = AimingSession::ChiefAnchor;
    using FieldWaveKey = AimingSession::FieldWaveKey;
    using FirstSurfaceSeed = AimingSession::FirstSurfaceSeed;

    template <
        class Type_BudgetChiefRejectReason,
        class Capture_should_skip_abcd_seed_for_angle,
        class Capture_seed,
        class Capture_target_params_for,
        class Capture_inward_seed,
        class Capture_center_radius,
        class Capture_trial_seeds,
        class Capture_append_clipped_seed,
        class Capture_append_field_seed,
        class Capture_append_boundary_scan_to,
        class Capture_scale_seed_radial,
        class Capture_field_rho,
        class Capture_same_field_direction,
        class Capture_interpolate_field_by_rho,
        class Capture_remember_bridge_anchor,
        class Capture_try_trial_seed_range_for_field
    >
    static auto run(
        AimingSession& session,
        std::size_t& field_index,
        std::size_t& wave_index,
        const Field& field,
        const Wavelength& wave,
        Capture_should_skip_abcd_seed_for_angle& should_skip_abcd_seed_for_angle,
        Capture_seed& seed,
        Capture_target_params_for& target_params_for,
        Capture_inward_seed& inward_seed,
        Capture_center_radius& center_radius,
        Capture_trial_seeds& trial_seeds,
        Capture_append_clipped_seed& append_clipped_seed,
        Capture_append_field_seed& append_field_seed,
        Capture_append_boundary_scan_to& append_boundary_scan_to,
        Capture_scale_seed_radial& scale_seed_radial,
        Capture_field_rho& field_rho,
        Capture_same_field_direction& same_field_direction,
        Capture_interpolate_field_by_rho& interpolate_field_by_rho,
        Capture_remember_bridge_anchor& remember_bridge_anchor,
        Capture_try_trial_seed_range_for_field& try_trial_seed_range_for_field,
        bool allow_virtual_bridges
    ) {
        const double max_field_deg = std::max(0.0, session.system_.max_field_deg);
        const double target_rho = field_rho(field);
        const double target_angle_deg = target_rho * max_field_deg;
        if (!(max_field_deg > 0.0) || !(target_rho > 1.0e-9) || !(target_angle_deg > 35.0 + 1.0e-9)) {
            return;
        }

        struct BracketAnchor {
            bool valid = false;
            double rho = 0.0;
            ChiefAnchor anchor;
            Field field;
        };
        BracketAnchor inner;
        BracketAnchor outer;
        auto consider_candidate = [&](const Field& candidate_field,
                                      double candidate_rho,
                                      const ChiefAnchor& candidate_anchor) {
            if (!candidate_anchor.valid || !same_field_direction(candidate_field, field)) {
                return;
            }
            if (candidate_rho < target_rho - 1.0e-9 && (!inner.valid || candidate_rho > inner.rho)) {
                inner.valid = true;
                inner.rho = candidate_rho;
                inner.anchor = candidate_anchor;
                inner.field = candidate_field;
            }
            if (candidate_rho > target_rho + 1.0e-9 && (!outer.valid || candidate_rho < outer.rho)) {
                outer.valid = true;
                outer.rho = candidate_rho;
                outer.anchor = candidate_anchor;
                outer.field = candidate_field;
            }
        };
        for (std::size_t i = 0; i < session.system_.fields.size(); ++i) {
            if (i == field_index) {
                continue;
            }
            const auto it = session.chief_anchors_.find(FieldWaveKey{i, wave_index});
            if (it == session.chief_anchors_.end() || !it->second.valid) {
                continue;
            }
            consider_candidate(session.system_.fields[i], field_rho(session.system_.fields[i]), it->second);
        }
        for (const auto& bridge_entry : session.chief_bridge_anchors_) {
            if (bridge_entry.wave_index != wave_index || !bridge_entry.anchor.valid) {
                continue;
            }
            consider_candidate(bridge_entry.field, bridge_entry.rho, bridge_entry.anchor);
        }
        if (inner.valid && outer.valid) {
            const double t = std::clamp(
                (target_rho - inner.rho) / std::max(1.0e-12, outer.rho - inner.rho),
                0.0,
                1.0);
            append_field_seed(
                {inner.anchor.seed.x + t * (outer.anchor.seed.x - inner.anchor.seed.x),
                 inner.anchor.seed.y + t * (outer.anchor.seed.y - inner.anchor.seed.y),
                 true});
            append_field_seed(inner.anchor.seed);
            append_field_seed(outer.anchor.seed);
            return;
        }

        std::size_t center_field_index = 0;
        double center_rho = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < session.system_.fields.size(); ++i) {
            const double rho = field_rho(session.system_.fields[i]);
            if (rho < center_rho) {
                center_rho = rho;
                center_field_index = i;
            }
        }
        const auto center_it = session.chief_anchors_.find(FieldWaveKey{center_field_index, wave_index});
        if (center_it == session.chief_anchors_.end() || !center_it->second.valid) {
            return;
        }

        Field base_field = session.system_.fields[center_field_index];
        FirstSurfaceSeed base_seed = center_it->second.seed;
        double base_rho = field_rho(base_field);
        if (inner.valid) {
            base_field = inner.field;
            base_seed = inner.anchor.seed;
            base_rho = inner.rho;
        }

        if (!allow_virtual_bridges) {
            append_field_seed(base_seed);
            if (base_rho > 1.0e-12 && target_rho > base_rho + 1.0e-9) {
                const FirstSurfaceSeed scaled_target_seed =
                    scale_seed_radial(base_seed, target_rho / base_rho);
                append_field_seed(scaled_target_seed);
                if (scaled_target_seed.valid) {
                    append_field_seed(
                        {(base_seed.x + scaled_target_seed.x) * 0.5,
                         (base_seed.y + scaled_target_seed.y) * 0.5,
                         true});
                }
            } else if (seed.valid) {
                append_field_seed(seed);
                append_field_seed(
                    {(base_seed.x + seed.x) * 0.5,
                     (base_seed.y + seed.y) * 0.5,
                     true});
            }
            return;
        }

        auto append_local_seed_family = [&](std::vector<FirstSurfaceSeed>& seeds,
                                            const FirstSurfaceSeed& origin,
                                            const FirstSurfaceSeed* toward) {
            if (!origin.valid) {
                return;
            }
            append_clipped_seed(seeds, origin);
            for (const double factor : std::array<double, 3>{{0.5, 1.0, 1.5}}) {
                append_clipped_seed(seeds, inward_seed(origin, factor * center_radius()));
            }
            double dx = 0.0;
            double dy = 0.0;
            if (toward && toward->valid) {
                dx = toward->x - origin.x;
                dy = toward->y - origin.y;
            }
            double norm = std::hypot(dx, dy);
            if ((!std::isfinite(norm) || norm <= 1.0e-12) && origin.valid) {
                dx = origin.x;
                dy = origin.y;
                norm = std::hypot(dx, dy);
            }
            if (!std::isfinite(norm) || norm <= 1.0e-12) {
                return;
            }
            dx /= norm;
            dy /= norm;
            const double scan_step = std::max(1.0, center_radius());
            for (const double offset : std::array<double, 6>{{0.25, -0.25, 0.5, -0.5, 1.0, -1.0}}) {
                append_clipped_seed(
                    seeds,
                    {origin.x + offset * scan_step * dx,
                     origin.y + offset * scan_step * dy,
                     true});
            }
            const double px = -dy;
            const double py = dx;
            for (const double offset : std::array<double, 4>{{0.25, -0.25, 0.5, -0.5}}) {
                append_clipped_seed(
                    seeds,
                    {origin.x + offset * scan_step * px,
                     origin.y + offset * scan_step * py,
                     true});
            }
            append_boundary_scan_to(seeds, origin, dx, dy);
        };
        auto find_cached_bridge_anchor = [&](double bridge_rho,
                                             ChiefAnchor* cached_anchor,
                                             Field* cached_field) {
            for (const auto& bridge_entry : session.chief_bridge_anchors_) {
                if (bridge_entry.wave_index != wave_index || !bridge_entry.anchor.valid) {
                    continue;
                }
                if (!same_field_direction(bridge_entry.field, field)) {
                    continue;
                }
                if (std::abs(bridge_entry.rho - bridge_rho) > 1.0e-9) {
                    continue;
                }
                if (cached_anchor) {
                    *cached_anchor = bridge_entry.anchor;
                }
                if (cached_field) {
                    *cached_field = bridge_entry.field;
                }
                return true;
            }
            return false;
        };

        bool built_bridge = false;
        const std::array<double, 3> bridge_angles_deg{{35.0, 60.0, 80.0}};
        for (const double bridge_angle_deg : bridge_angles_deg) {
            if (!(bridge_angle_deg > base_rho * max_field_deg + 1.0e-9) ||
                !(bridge_angle_deg < target_angle_deg - 1.0e-9)) {
                continue;
            }
            const double bridge_rho = std::clamp(bridge_angle_deg / max_field_deg, 0.0, target_rho);
            ChiefAnchor cached_bridge_anchor;
            Field cached_bridge_field;
            if (find_cached_bridge_anchor(bridge_rho, &cached_bridge_anchor, &cached_bridge_field)) {
                built_bridge = true;
                base_field = cached_bridge_field;
                base_seed = cached_bridge_anchor.seed;
                base_rho = bridge_rho;
                append_field_seed(base_seed);
                continue;
            }
            Field bridge_field = interpolate_field_by_rho(
                session.system_.fields[center_field_index],
                field_rho(session.system_.fields[center_field_index]),
                field,
                target_rho,
                bridge_rho);
            std::vector<FirstSurfaceSeed> bridge_seeds;
            bridge_seeds.reserve(32);
            bool bridge_seed_singular = false;
            FirstSurfaceSeed bridge_abcd_guess;
            if (!should_skip_abcd_seed_for_angle(bridge_angle_deg)) {
                bridge_abcd_guess =
                    session.estimate_abcd_chief_seed(bridge_field, wave, &bridge_seed_singular);
            }
            const bool first_explicit_bridge = !built_bridge && !inner.valid;
            if (first_explicit_bridge) {
                if (bridge_abcd_guess.valid) {
                    append_clipped_seed(bridge_seeds, bridge_abcd_guess);
                } else {
                    append_clipped_seed(bridge_seeds, base_seed);
                }
            } else {
                const FirstSurfaceSeed scaled_guess =
                    target_rho > 1.0e-12
                        ? scale_seed_radial(seed, bridge_rho / target_rho)
                        : FirstSurfaceSeed{};
                append_local_seed_family(
                    bridge_seeds,
                    bridge_abcd_guess.valid ? bridge_abcd_guess : base_seed,
                    bridge_abcd_guess.valid
                        ? &base_seed
                        : (scaled_guess.valid ? &scaled_guess : nullptr));
                if (bridge_abcd_guess.valid) {
                    append_local_seed_family(bridge_seeds, base_seed, &bridge_abcd_guess);
                    append_clipped_seed(
                        bridge_seeds,
                        {(base_seed.x + bridge_abcd_guess.x) * 0.5,
                         (base_seed.y + bridge_abcd_guess.y) * 0.5,
                         true});
                } else {
                    append_local_seed_family(
                        bridge_seeds,
                        base_seed,
                        scaled_guess.valid ? &scaled_guess : nullptr);
                }
                if (scaled_guess.valid) {
                    append_local_seed_family(bridge_seeds, scaled_guess, &base_seed);
                    append_clipped_seed(
                        bridge_seeds,
                        {(base_seed.x + scaled_guess.x) * 0.5,
                         (base_seed.y + scaled_guess.y) * 0.5,
                         true});
                }
            }
            ChiefAnchor bridge_anchor;
            Type_BudgetChiefRejectReason bridge_reason = Type_BudgetChiefRejectReason::EmptySeeds;
            if (!try_trial_seed_range_for_field(
                    bridge_field,
                    target_params_for(bridge_field),
                    bridge_seeds,
                    0,
                    bridge_seeds.size(),
                    &bridge_reason,
                    &bridge_anchor)) {
                break;
            }
            built_bridge = true;
            base_field = bridge_field;
            base_seed = bridge_anchor.seed;
            base_rho = bridge_rho;
            remember_bridge_anchor(bridge_field, bridge_rho, bridge_anchor);
            append_field_seed(base_seed);
        }

        if (built_bridge || inner.valid) {
            append_field_seed(base_seed);
            if (base_rho > 1.0e-12 && target_rho > base_rho + 1.0e-9) {
                const FirstSurfaceSeed scaled_target_seed =
                    scale_seed_radial(base_seed, target_rho / base_rho);
                append_field_seed(scaled_target_seed);
                if (scaled_target_seed.valid) {
                    append_local_seed_family(trial_seeds, scaled_target_seed, &base_seed);
                    append_clipped_seed(
                        trial_seeds,
                        {(base_seed.x + scaled_target_seed.x) * 0.5,
                         (base_seed.y + scaled_target_seed.y) * 0.5,
                         true});
                }
            }
        }
    }
};

} // namespace optiland::raytrace_detail
