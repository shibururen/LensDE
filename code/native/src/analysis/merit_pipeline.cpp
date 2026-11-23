/*
 * Coordinates the complete MeritEvaluation phase sequence and contribution bookkeeping.
 * The public evaluate_merit wrapper enters here after selecting its aperture contract.
 * Early-return gates, field-weight application and contribution/reduction order are
 * part of the numerical interface consumed by DE, DLS and checkpoint rescoring.
 */

#include "merit_evaluation.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
void MeritEvaluation::add_contribution(MeritContribution contribution) {

        if ((contribution.source == "operand_field" ||
             contribution.source == "legacy_field") &&
            (config.image_field_weight_mode != "center_double" ||
             classify_unified_contribution(contribution) == MeritCategory::ImageQuality)) {
            contribution.field_weight = configured_image_field_weight(
                system, config, contribution.index);
            contribution.penalty *= contribution.field_weight;
            contribution.weight *= contribution.field_weight;
        }
        merit_sum += contribution.penalty;
        merit_weight_sum += scoring_weight(contribution.weight);
        score.contributions.push_back(std::move(contribution));
    
}

Score MeritEvaluation::evaluate() {
    measure_image_quality();
    if (!accept_trace_results()) return score;
    merit_sum = 0.0;
    merit_weight_sum = 0.0;
    if (!append_trace_contributions()) return score;
    if (!config.operands.empty()) append_explicit_operands();
    else append_legacy_operands();
    append_illumination_floor();
    append_vignetting_priors();
    append_ray_geometry_priors();
    append_thickness_constraints();
    append_triplet_prior();
    append_surface_constraints();
    append_material_constraints();
    finalize_score();
    return score;
}
}  // namespace analysis_detail

}  // namespace optiland
