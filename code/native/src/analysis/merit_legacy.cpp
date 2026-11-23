/*
 * Constructs target-based merit contributions when no explicit operand list is supplied.
 * The merit pipeline selects this compatibility path after accepting trace results.
 * Keep the established target weights, field/wavelength traversal and contribution
 * identities so saved configurations retain their original scoring behavior.
 */

#include "merit_evaluation.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
void MeritEvaluation::append_legacy_operands() {

        if (std::abs(config.target_efl) > kEps) {
            const double penalty = scoring_weight(config.weight_efl) * square_error(score.efl, config.target_efl);
            add_contribution(make_contribution(
                "legacy",
                "efl",
                "target",
                score.efl,
                config.target_efl,
                -kInf,
                kInf,
                config.weight_efl,
                penalty));
        }
        if (config.target_ttl > 0.0) {
            const double penalty = scoring_weight(config.weight_ttl) * square_error(score.ttl, config.target_ttl);
            add_contribution(make_contribution(
                "legacy",
                "ttl",
                "target",
                score.ttl,
                config.target_ttl,
                -kInf,
                kInf,
                config.weight_ttl,
                penalty));
        }
        if (config.target_bfl > 0.0) {
            const double penalty = scoring_weight(config.weight_bfl) * square_error(score.bfl, config.target_bfl);
            add_contribution(make_contribution(
                "legacy",
                "bfl",
                "target",
                score.bfl,
                config.target_bfl,
                -kInf,
                kInf,
                config.weight_bfl,
                penalty));
        }
        if (config.spot_target > 0.0) {
            for (std::size_t field_index = 0; field_index < field_spots.size(); ++field_index) {
                if (!field_spot_usable_for_merit(field_index)) {
                    continue;
                }
                const double value = field_spots[field_index].stats.rms_radius;
                const double penalty = scoring_weight(config.weight_spot) * square_error(value, config.spot_target);
                add_contribution(make_contribution(
                    "legacy_field",
                    "rms_spot",
                    "target",
                    value,
                    config.spot_target,
                    -kInf,
                    kInf,
                    config.weight_spot,
                    penalty,
                    field_index,
                    0));
            }
        } else {
            for (std::size_t field_index = 0; field_index < field_spots.size(); ++field_index) {
                if (!field_spot_usable_for_merit(field_index)) {
                    continue;
                }
                const double value = field_spots[field_index].stats.rms_radius;
                const double penalty = scoring_weight(config.weight_spot) * value * value;
                add_contribution(make_contribution(
                    "legacy_field",
                    "rms_spot",
                    "minimize",
                    value,
                    0.0,
                    -kInf,
                    kInf,
                    config.weight_spot,
                    penalty,
                    field_index,
                    0));
            }
        }
        if (scoring_weight(config.unified_image_single_ray_spot_tail_weight) > 0.0) {
            for (std::size_t field_index = 0; field_index < field_spots.size(); ++field_index) {
                if (!field_spot_usable_for_merit(field_index)) {
                    continue;
                }
                const double value =
                    field_spots[field_index].stats.single_ray_max_radius;
                const double weight =
                    config.unified_image_single_ray_spot_tail_weight;
                const double penalty =
                    std::isfinite(value)
                        ? scoring_weight(weight) * value * value
                        : scoring_weight(weight) * 1.0e12;
                add_contribution(make_contribution(
                    "legacy_field",
                    "single_ray_spot_tail",
                    "minimize",
                    value,
                    0.0,
                    -kInf,
                    kInf,
                    weight,
                    penalty,
                    field_index,
                    0));
            }
        }
    
}
}  // namespace analysis_detail

}  // namespace optiland
