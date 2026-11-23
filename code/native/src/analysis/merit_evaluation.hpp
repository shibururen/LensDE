#pragma once
#include "detail.hpp"

namespace optiland { namespace analysis_detail {
// Per-evaluation state. Phases append contributions in their original order.
class MeritEvaluation {
public:
    MeritEvaluation(const LensSystem& system, const MeritConfig& config)
        : system(system), config(config) {}
    Score evaluate();
private:
    const LensSystem& system;
    const MeritConfig& config;
    Score score;
    bool soft_trace = false;
    bool need_wavefront = false;
    bool spot_failed = false;
    bool wavefront_failed = false;
    std::vector<FieldSpot> field_spots;
    std::vector<FieldWavefront> field_wavefronts;
    std::vector<std::size_t> illumination_fields;
    double merit_sum = 0.0;
    double merit_weight_sum = 0.0;
    void measure_image_quality();
    bool accept_trace_results();
    bool append_trace_contributions();
    void append_illumination_floor();
    void append_vignetting_priors();
    void append_ray_geometry_priors();
    void append_thickness_constraints();
    void append_triplet_prior();
    void append_surface_constraints();
    void append_material_constraints();
    void finalize_score();
    void add_contribution(MeritContribution contribution);
    bool field_spot_usable_for_merit(std::size_t field_index);
    bool field_wavefront_usable_for_merit(std::size_t entry_index);
    void append_explicit_operands();
    void append_legacy_operands();
};
} }  // namespace optiland::analysis_detail
