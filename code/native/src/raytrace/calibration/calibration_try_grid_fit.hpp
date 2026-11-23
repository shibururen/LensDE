#pragma once
#include "../diagnostics_inline.hpp"
#include "../detail.hpp"

namespace optiland::raytrace_detail {

struct CalibrationTryGridFit {
    using CalibratedParaxialModel = AimingSession::CalibratedParaxialModel;

    template <
        class Capture_finish_from_jacobian,
        class Capture_samples,
        class Capture_add_sample
    >
    static auto run(
        AimingSession& session,
        RayAimingDiagnosticsState& diag,
        CalibratedParaxialModel& model,
        const CalibratedParaxialConfig& cfg,
        Capture_finish_from_jacobian& finish_from_jacobian,
        Capture_samples& samples,
        Capture_add_sample& add_sample,
        double h
    ) {
        add_diag(diag.calibrated_grid_fit_attempts);
        ScopedDiagTimer grid_timer(diag.calibrated_grid_fit_ns);
        samples.clear();
        samples.push_back({model.chief_seed, model.chief_stop_x, model.chief_stop_y, true});
        const int n = std::max(3, cfg.pilot_grid_n);
        const double halfwidth = h * std::max(1, n / 2);
        for (int iy = 0; iy < n; ++iy) {
            const double dy = -halfwidth +
                              (n == 1 ? 0.0 : 2.0 * halfwidth * iy / static_cast<double>(n - 1));
            for (int ix = 0; ix < n; ++ix) {
                const double dx = -halfwidth +
                                  (n == 1 ? 0.0 : 2.0 * halfwidth * ix / static_cast<double>(n - 1));
                if (std::abs(dx) <= 1.0e-15 && std::abs(dy) <= 1.0e-15) {
                    continue;
                }
                add_sample({model.chief_seed.x + dx, model.chief_seed.y + dy, true});
            }
        }
        if (samples.size() < 4) {
            return false;
        }

        double mean_x = 0.0;
        double mean_y = 0.0;
        double mean_sx = 0.0;
        double mean_sy = 0.0;
        int count = 0;
        for (const auto& sample : samples) {
            if (!sample.valid) {
                continue;
            }
            mean_x += sample.seed.x;
            mean_y += sample.seed.y;
            mean_sx += sample.stop_x;
            mean_sy += sample.stop_y;
            ++count;
        }
        if (count < 3) {
            return false;
        }
        const double inv_count = 1.0 / static_cast<double>(count);
        mean_x *= inv_count;
        mean_y *= inv_count;
        mean_sx *= inv_count;
        mean_sy *= inv_count;
        double sxx = 0.0;
        double sxy = 0.0;
        double syy = 0.0;
        double tx_x = 0.0;
        double tx_y = 0.0;
        double ty_x = 0.0;
        double ty_y = 0.0;
        for (const auto& sample : samples) {
            if (!sample.valid) {
                continue;
            }
            const double qx = sample.seed.x - mean_x;
            const double qy = sample.seed.y - mean_y;
            const double sx = sample.stop_x - mean_sx;
            const double sy = sample.stop_y - mean_sy;
            sxx += qx * qx;
            sxy += qx * qy;
            syy += qy * qy;
            tx_x += sx * qx;
            tx_y += sx * qy;
            ty_x += sy * qx;
            ty_y += sy * qy;
        }
        const double normal_det = sxx * syy - sxy * sxy;
        if (!std::isfinite(normal_det) || std::abs(normal_det) <= 1.0e-14) {
            return false;
        }
        const double j11 = (tx_x * syy - tx_y * sxy) / normal_det;
        const double j12 = (-tx_x * sxy + tx_y * sxx) / normal_det;
        const double j21 = (ty_x * syy - ty_y * sxy) / normal_det;
        const double j22 = (-ty_x * sxy + ty_y * sxx) / normal_det;
        const bool ok = finish_from_jacobian(j11, j12, j21, j22, samples);
        if (ok) {
            add_diag(diag.calibrated_grid_fit_successes);
        }
        return ok;
    }
};

} // namespace optiland::raytrace_detail
