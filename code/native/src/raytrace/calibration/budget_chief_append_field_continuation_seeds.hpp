#pragma once
#include "../detail.hpp"

namespace optiland::raytrace_detail {

struct BudgetChiefAppendFieldContinuationSeeds {
    using FieldWaveKey = AimingSession::FieldWaveKey;
    using FirstSurfaceSeed = AimingSession::FirstSurfaceSeed;

    template <
        class Capture_append_field_seed,
        class Capture_append_boundary_scan
    >
    static auto run(
        AimingSession& session,
        std::size_t& field_index,
        std::size_t& wave_index,
        const Wavelength& wave,
        Capture_append_field_seed& append_field_seed,
        Capture_append_boundary_scan& append_boundary_scan
    ) {
        if (field_index == 0) {
            return;
        }
        const auto prev = session.chief_anchors_.find(FieldWaveKey{field_index - 1, wave_index});
        if (prev != session.chief_anchors_.end() && prev->second.valid) {
            append_field_seed(prev->second.seed);
        }
        const auto f = session.fields_seen_.find(field_index);
        if (field_index > 1) {
            const auto prev0 = session.chief_anchors_.find(FieldWaveKey{field_index - 1, wave_index});
            const auto prev1 = session.chief_anchors_.find(FieldWaveKey{field_index - 2, wave_index});
            const auto f0 = session.fields_seen_.find(field_index - 1);
            const auto f1 = session.fields_seen_.find(field_index - 2);
            if (prev0 != session.chief_anchors_.end() && prev1 != session.chief_anchors_.end() &&
                prev0->second.valid && prev1->second.valid &&
                f != session.fields_seen_.end() && f0 != session.fields_seen_.end() && f1 != session.fields_seen_.end()) {
                const double rho = std::hypot(f->second.hx, f->second.hy);
                const double rho0 = std::hypot(f0->second.hx, f0->second.hy);
                const double rho1 = std::hypot(f1->second.hx, f1->second.hy);
                double alpha = (rho - rho0) / std::max(1.0e-12, rho0 - rho1);
                alpha = std::clamp(
                    alpha,
                    0.0,
                    std::max(0.0, session.system_.ray_aiming.adaptive_pilot_grid.max_field_extrapolation_factor));
                const FirstSurfaceSeed extrapolated{
                    prev0->second.seed.x + alpha * (prev0->second.seed.x - prev1->second.seed.x),
                    prev0->second.seed.y + alpha * (prev0->second.seed.y - prev1->second.seed.y),
                    true};
                append_field_seed(extrapolated);

                double dx = prev0->second.seed.x - prev1->second.seed.x;
                double dy = prev0->second.seed.y - prev1->second.seed.y;
                double norm = std::hypot(dx, dy);
                if ((!std::isfinite(norm) || norm <= 1.0e-12) && extrapolated.valid) {
                    dx = extrapolated.x;
                    dy = extrapolated.y;
                    norm = std::hypot(dx, dy);
                }
                if (std::isfinite(norm) && norm > 1.0e-12) {
                    dx /= norm;
                    dy /= norm;
                    const double scan_step = std::max(1.0, session.center_meridional_radius(wave));
                    static constexpr std::array<double, 17> offsets{
                        0.0, 0.5, -0.5, 1.0, -1.0,
                        1.25, 1.5, 1.75, -2.0,
                        2.0, 2.125, 2.25, 2.5, 2.75,
                        3.0, 3.5, 4.0};
                    for (const double offset : offsets) {
                        append_field_seed(
                            {extrapolated.x + offset * scan_step * dx,
                             extrapolated.y + offset * scan_step * dy,
                             true});
                    }
                    append_boundary_scan(prev0->second.seed, dx, dy);
                }
            }
        } else if (prev != session.chief_anchors_.end() && prev->second.valid) {
            double dx = prev->second.seed.x;
            double dy = prev->second.seed.y;
            double norm = std::hypot(dx, dy);
            if (std::isfinite(norm) && norm > 1.0e-12) {
                dx /= norm;
                dy /= norm;
                append_boundary_scan(prev->second.seed, dx, dy);
            }
        }
    }
};

} // namespace optiland::raytrace_detail
