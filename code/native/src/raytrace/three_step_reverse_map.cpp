// Prepare the STOP-to-first-surface reverse mapping used by three-step aiming.
// The session caches the mapping before central/edge model construction and ray generation.
// Keep reverse refraction, surface traversal, and mapping validity tied to the original geometry.

#include "diagnostics_inline.hpp"
#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

AimingSession::ThreeStepReverseMap& AimingSession::ensure_three_step_reverse_map(
    std::size_t wave_index,
    const Wavelength& wave) {
    ThreeStepReverseMap& map = three_step_reverse_maps_[wave_index];
    if (map.built && std::abs(map.wavelength_um - wave.value_um) <= 1.0e-12) {
        return map;
    }
    map = ThreeStepReverseMap{};
    map.built = true;
    map.wavelength_um = wave.value_um;

    const auto& cfg = system_.ray_aiming.three_step;
    const int trial_rays = std::max(2, cfg.chief_reverse_trial_rays);
    const double max_angle = std::clamp(cfg.chief_reverse_max_angle_deg, 0.0, 89.999);
    constexpr double map_mer_x = 0.0;
    constexpr double map_mer_y = 1.0;
    struct ReverseSample {
        double stop_angle = 0.0;
        double object_angle = 0.0;
        double first_radius = 0.0;
    };
    std::vector<ReverseSample> reverse_samples;
    reverse_samples.reserve(static_cast<std::size_t>(trial_rays + 16));
    auto& diag = diagnostics_state();

    auto trace_reverse_sample = [&](double angle, ReverseSample* sample) {
        if (!sample || !std::isfinite(angle)) {
            return false;
        }
        Vec3 first_point;
        Vec3 forward_dir;
        add_diag(diag.three_step_reverse_trial_calls);
        bool reverse_ok = false;
        {
            ScopedDiagTimer reverse_timer(diag.three_step_reverse_trial_ns);
            reverse_ok = reverse_trace_stop_center_to_first(
                system_,
                wave.value_um,
                map_mer_x,
                map_mer_y,
                angle,
                &first_point,
                &forward_dir);
        }
        if (!reverse_ok) {
            return false;
        }
        const double out_angle = rad_to_deg(std::atan2(
            std::hypot(forward_dir.x, forward_dir.y),
            forward_dir.z));
        const double first_radius = first_point.x * map_mer_x + first_point.y * map_mer_y;
        if (!std::isfinite(out_angle) ||
            !std::isfinite(first_radius)) {
            return false;
        }
        add_diag(diag.three_step_reverse_trial_successes);
        *sample = {angle, out_angle, first_radius};
        return true;
    };

    auto append_reverse_sample = [&](const ReverseSample& sample) {
        if (!std::isfinite(sample.stop_angle) ||
            !std::isfinite(sample.object_angle) ||
            !std::isfinite(sample.first_radius)) {
            return false;
        }
        for (const auto& existing : reverse_samples) {
            if (std::abs(existing.stop_angle - sample.stop_angle) <=
                1.0e-10 * std::max({1.0, std::abs(existing.stop_angle), std::abs(sample.stop_angle)})) {
                return false;
            }
        }
        reverse_samples.push_back(sample);
        return true;
    };

    const double coarse_step =
        trial_rays == 1 ? max_angle : max_angle / static_cast<double>(trial_rays - 1);
    for (int i = 0; i < trial_rays; ++i) {
        const double fraction =
            trial_rays == 1 ? 0.0 : static_cast<double>(i) / static_cast<double>(trial_rays - 1);
        const double angle = max_angle * fraction;
        ReverseSample sample;
        if (trace_reverse_sample(angle, &sample)) {
            append_reverse_sample(sample);
        }
    }

    auto target_is_bracketed = [&](double target_angle) {
        if (!std::isfinite(target_angle) || reverse_samples.empty()) {
            return false;
        }
        bool has_low = false;
        bool has_high = false;
        for (const auto& sample : reverse_samples) {
            has_low = has_low || sample.object_angle <= target_angle + 1.0e-9;
            has_high = has_high || sample.object_angle >= target_angle - 1.0e-9;
        }
        return has_low && has_high;
    };

    std::vector<double> target_angles;
    for (const Field& field : system_.fields) {
        const double field_radius = std::hypot(field.hx, field.hy);
        const double target_angle =
            std::max(0.0, system_.max_field_deg) * field_radius;
        if (!std::isfinite(target_angle) || target_angle <= 1.0e-9) {
            continue;
        }
        bool duplicate = false;
        for (double existing : target_angles) {
            if (std::abs(existing - target_angle) <=
                1.0e-9 * std::max({1.0, std::abs(existing), std::abs(target_angle)})) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            target_angles.push_back(target_angle);
        }
    }
    std::sort(target_angles.begin(), target_angles.end());

    constexpr int kLocalRefineIterations = 12;
    for (double target_angle : target_angles) {
        if (target_is_bracketed(target_angle)) {
            continue;
        }
        auto best_low = reverse_samples.end();
        for (auto it = reverse_samples.begin(); it != reverse_samples.end(); ++it) {
            if (it->object_angle > target_angle) {
                continue;
            }
            if (best_low == reverse_samples.end() ||
                it->object_angle > best_low->object_angle) {
                best_low = it;
            }
        }
        if (best_low == reverse_samples.end()) {
            continue;
        }
        double lo_stop = best_low->stop_angle;
        double hi_stop = std::min(max_angle, lo_stop + std::max(1.0e-9, coarse_step));
        if (!(hi_stop > lo_stop)) {
            lo_stop = std::max(0.0, best_low->stop_angle - std::max(1.0e-9, coarse_step));
            hi_stop = best_low->stop_angle;
        }
        if (!(hi_stop > lo_stop)) {
            continue;
        }
        double lo_object = best_low->object_angle;
        for (int refine = 0; refine < kLocalRefineIterations; ++refine) {
            const double mid_stop = 0.5 * (lo_stop + hi_stop);
            if (!(mid_stop > lo_stop && mid_stop < hi_stop)) {
                break;
            }
            ReverseSample mid_sample;
            if (!trace_reverse_sample(mid_stop, &mid_sample)) {
                hi_stop = mid_stop;
                continue;
            }
            append_reverse_sample(mid_sample);
            if (mid_sample.object_angle >= target_angle) {
                if (target_is_bracketed(target_angle)) {
                    break;
                }
                hi_stop = mid_stop;
            } else {
                lo_stop = mid_stop;
                lo_object = mid_sample.object_angle;
            }
            (void)lo_object;
        }
    }

    std::sort(reverse_samples.begin(), reverse_samples.end(), [](const auto& a, const auto& b) {
        return a.object_angle < b.object_angle;
    });
    for (const auto& sample : reverse_samples) {
        if (!map.samples.empty() &&
            std::abs(sample.object_angle - map.samples.back().first) <= 1.0e-9) {
            continue;
        }
        map.samples.push_back({sample.object_angle, sample.first_radius});
    }
    map.valid = !map.samples.empty();
    return map;
}

} // namespace optiland
