#pragma once
#include "../detail.hpp"

namespace optiland::raytrace_detail {

struct BudgetChiefAppendSameFieldCachedChiefSeeds {
    using ChiefAnchor = AimingSession::ChiefAnchor;
    using FieldWaveKey = AimingSession::FieldWaveKey;

    template <
        class Capture_append_field_seed
    >
    static auto run(
        AimingSession& session,
        std::size_t& field_index,
        std::size_t& wave_index,
        const Wavelength& wave,
        Capture_append_field_seed& append_field_seed
    ) {
        if (session.system_.wavelengths.empty()) {
            return;
        }
        auto append_cached_wave = [&](std::size_t candidate_wave_index) {
            if (candidate_wave_index == wave_index ||
                candidate_wave_index >= session.system_.wavelengths.size()) {
                return;
            }
            const auto it = session.chief_anchors_.find(FieldWaveKey{field_index, candidate_wave_index});
            if (it != session.chief_anchors_.end() && it->second.valid) {
                append_field_seed(it->second.seed);
            }
        };

        std::size_t primary_index = 0;
        bool have_primary = false;
        for (std::size_t i = 0; i < session.system_.wavelengths.size(); ++i) {
            if (session.system_.wavelengths[i].primary) {
                primary_index = i;
                have_primary = true;
                break;
            }
        }
        if (have_primary) {
            append_cached_wave(primary_index);
        }
        if (wave_index > 0) {
            append_cached_wave(wave_index - 1);
        }
        if (wave_index + 1 < session.system_.wavelengths.size()) {
            append_cached_wave(wave_index + 1);
        }

        struct SameFieldCachedSeed {
            double wavelength_distance = 0.0;
            std::size_t wave_index = 0;
        };
        std::vector<SameFieldCachedSeed> cached;
        cached.reserve(session.system_.wavelengths.size());
        for (const auto& entry : session.chief_anchors_) {
            const FieldWaveKey& other_key = entry.first;
            const ChiefAnchor& other_anchor = entry.second;
            if (other_key.first != field_index ||
                other_key.second == wave_index ||
                other_key.second >= session.system_.wavelengths.size() ||
                !other_anchor.valid) {
                continue;
            }
            const double distance =
                std::abs(session.system_.wavelengths[other_key.second].value_um - wave.value_um);
            if (!std::isfinite(distance)) {
                continue;
            }
            cached.push_back(SameFieldCachedSeed{distance, other_key.second});
        }
        std::sort(cached.begin(), cached.end(), [](const auto& a, const auto& b) {
            if (a.wavelength_distance == b.wavelength_distance) {
                return a.wave_index < b.wave_index;
            }
            return a.wavelength_distance < b.wavelength_distance;
        });
        const std::size_t max_extra_cached = std::min<std::size_t>(cached.size(), 3);
        for (std::size_t i = 0; i < max_extra_cached; ++i) {
            append_cached_wave(cached[i].wave_index);
        }
    }
};

} // namespace optiland::raytrace_detail
