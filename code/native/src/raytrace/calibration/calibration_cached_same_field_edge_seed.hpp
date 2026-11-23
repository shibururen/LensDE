#pragma once
#include "../detail.hpp"

namespace optiland::raytrace_detail {

struct CalibrationCachedSameFieldEdgeSeed {
    using CalibratedParaxialModel = AimingSession::CalibratedParaxialModel;
    using FieldWaveKey = AimingSession::FieldWaveKey;
    using FirstSurfaceSeed = AimingSession::FirstSurfaceSeed;

    template <
        class Capture_anchor
    >
    static auto run(
        AimingSession& session,
        std::size_t& field_index,
        std::size_t& wave_index,
        const Wavelength& wave,
        const int& continuation_mode,
        CalibratedParaxialModel& model,
        Capture_anchor& anchor,
        std::size_t edge_index
    ) {
        FirstSurfaceSeed cached_seed;
        if (continuation_mode&512) {
            // CUDA chooses one whole valid source model: primary
            // for secondaries, then immediate previous, then next.
            // Do not scan farther wavelengths or let map iteration
            // choose a different source when distances tie.
            const CalibratedParaxialModel* source=nullptr;
            auto select=[&](std::size_t wi) {
                if (source || wi==wave_index || wi>=session.system_.wavelengths.size()) return;
                auto found=session.calibrated_models_.find({field_index,wi});
                if (found!=session.calibrated_models_.end() && found->second.valid)
                    source=&found->second;
            };
            if (!wave.primary) {
                for (std::size_t wi=0;wi<session.system_.wavelengths.size();++wi)
                    if (session.system_.wavelengths[wi].primary) {select(wi);break;}
            }
            if (wave_index>0) select(wave_index-1);
            select(wave_index+1);
            if (source && source->chief_seed.valid && edge_index<source->edge_anchors.size()) {
                const auto& anchor=source->edge_anchors[edge_index];
                if (anchor.valid && anchor.seed.valid)
                    cached_seed={model.chief_seed.x+(anchor.seed.x-source->chief_seed.x),
                                 model.chief_seed.y+(anchor.seed.y-source->chief_seed.y),true};
            }
            return cached_seed;
        }
        std::size_t best_distance = std::numeric_limits<std::size_t>::max();
        for (const auto& entry : session.calibrated_models_) {
            const FieldWaveKey& other_key = entry.first;
            if (other_key.first != field_index || other_key.second == wave_index) {
                continue;
            }
            const CalibratedParaxialModel& other_model = entry.second;
            if (((continuation_mode&512) && !other_model.valid) || !other_model.built ||
                edge_index >= other_model.edge_anchors.size()) {
                continue;
            }
            const auto& other_anchor = other_model.edge_anchors[edge_index];
            if (!other_anchor.valid || !other_anchor.seed.valid) {
                continue;
            }
            const std::size_t distance =
                other_key.second > wave_index
                    ? other_key.second - wave_index
                    : wave_index - other_key.second;
            if (distance < best_distance) {
                cached_seed = {
                    model.chief_seed.x +
                        (other_anchor.seed.x - other_model.chief_seed.x),
                    model.chief_seed.y +
                        (other_anchor.seed.y - other_model.chief_seed.y),
                    true};
                best_distance = distance;
            }
        }
        return cached_seed;
    }
};

} // namespace optiland::raytrace_detail
