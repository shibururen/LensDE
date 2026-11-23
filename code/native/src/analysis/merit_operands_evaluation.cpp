/*
 * Expands configured operands into system, field and wavelength merit contributions.
 * The merit pipeline uses this path when MeritConfig supplies an operand list.
 * Coverage-based image eligibility, operand order and field/wavelength indices must
 * stay aligned with the previously measured spot and wavefront arrays.
 */

#include "merit_evaluation.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
bool MeritEvaluation::field_spot_usable_for_merit(std::size_t field_index) {

        if (field_index >= field_spots.size()) {
            return false;
        }
        const auto& stats = field_spots[field_index].stats;
        return std::isfinite(stats.rms_radius) &&
               (!soft_trace || stats.coverage >= config.min_coverage_for_image_merit);
    
}

bool MeritEvaluation::field_wavefront_usable_for_merit(std::size_t entry_index) {

        if (entry_index >= field_wavefronts.size()) {
            return false;
        }
        const auto& stats = field_wavefronts[entry_index].stats;
        return stats.chief_ok &&
               std::isfinite(stats.rms_waves) &&
               (!soft_trace || stats.coverage >= config.min_coverage_for_image_merit);
    
}

void MeritEvaluation::append_explicit_operands() {

        for (const auto& operand : config.operands) {
            if (is_interval_thickness_constraint_kind(operand.name) &&
                !thickness_constraint_is_active(config, operand.index)) {
                continue;
            }
            if (is_worst_wavelength_spot_operand_name(operand.name) ||
                is_spectral_spot_spread_operand_name(operand.name)) {
                const bool worst_operand =
                    is_worst_wavelength_spot_operand_name(operand.name);
                for (std::size_t field_index = 0; field_index < field_spots.size(); ++field_index) {
                    if (!field_spot_usable_for_merit(field_index)) {
                        continue;
                    }
                    const FieldSpot& field_spot = field_spots[field_index];
                    const double value = worst_operand
                                             ? field_spot.worst_wavelength_rms_radius
                                             : field_spot.spectral_rms_spread;
                    const double penalty = operand_penalty(value, operand);
                    add_contribution(make_contribution(
                        "operand_field",
                        operand.name,
                        operand.mode,
                        value,
                        operand.target,
                        operand.lower,
                        operand.upper,
                        operand.weight,
                        penalty,
                        field_index,
                        0));
                }
                continue;
            }
            if (is_rms_spot_operand_name(operand.name)) {
                for (std::size_t field_index = 0; field_index < field_spots.size(); ++field_index) {
                    if (!field_spot_usable_for_merit(field_index)) {
                        continue;
                    }
                    const double value = field_spots[field_index].stats.rms_radius;
                    const double penalty = operand_penalty(value, operand);
                    add_contribution(make_contribution(
                        "operand_field",
                        operand.name,
                        operand.mode,
                        value,
                        operand.target,
                        operand.lower,
                        operand.upper,
                        operand.weight,
                        penalty,
                        field_index,
                        0));
                }
                continue;
            }
            if (is_single_ray_spot_tail_operand_name(operand.name)) {
                for (std::size_t field_index = 0; field_index < field_spots.size(); ++field_index) {
                    if (!field_spot_usable_for_merit(field_index)) {
                        continue;
                    }
                    const double value =
                        field_spots[field_index].stats.single_ray_max_radius;
                    const double penalty = operand_penalty(value, operand);
                    add_contribution(make_contribution(
                        "operand_field",
                        operand.name,
                        operand.mode,
                        value,
                        operand.target,
                        operand.lower,
                        operand.upper,
                        operand.weight,
                        penalty,
                        field_index,
                        0));
                }
                continue;
            }
            if (is_single_ray_spot_guard_operand_name(operand.name)) {
                for (std::size_t field_index = 0; field_index < field_spots.size(); ++field_index) {
                    if (!field_spot_usable_for_merit(field_index)) {
                        continue;
                    }
                    const auto& stats = field_spots[field_index].stats;
                    const double denom = std::max(1.0e-12, std::abs(stats.rms_radius));
                    double value =
                        std::isfinite(stats.single_ray_max_radius)
                            ? stats.single_ray_max_radius / denom
                            : std::numeric_limits<double>::quiet_NaN();
                    const std::string mode = normalized_name(operand.mode);
                    const bool upper_bound_mode =
                        mode == "less" || mode == "less_than" ||
                        mode == "upper" || mode == "max" ||
                        mode == "le" || mode == "lte" ||
                        mode == "range" || mode == "between" ||
                        mode == "interval";
                    double penalty = 0.0;
                    if (stats.single_ray_spot_guard_failure_squared > 0.0 &&
                        upper_bound_mode) {
                        const double upper =
                            single_ray_guard_upper_from_operand(operand);
                        const double measured_excess = std::isfinite(value)
                                                           ? std::max(0.0, value - upper)
                                                           : 0.0;
                        const double combined_excess = std::hypot(
                            measured_excess,
                            std::sqrt(stats.single_ray_spot_guard_failure_squared));
                        value = upper + combined_excess;
                        penalty = operand_penalty(value, operand);
                    } else {
                        penalty = operand_penalty(value, operand);
                        if (stats.single_ray_spot_guard_failure_squared > 0.0) {
                            penalty += std::abs(operand.weight) *
                                       stats.single_ray_spot_guard_failure_squared;
                        }
                    }
                    add_contribution(make_contribution(
                        "operand_field",
                        operand.name,
                        operand.mode,
                        value,
                        operand.target,
                        operand.lower,
                        operand.upper,
                        operand.weight,
                        penalty,
                        field_index,
                        0));
                }
                continue;
            }
            if (is_rms_wavefront_operand_name(operand.name)) {
                bool added_wavefront_operand = false;
                for (std::size_t entry_index = 0; entry_index < field_wavefronts.size(); ++entry_index) {
                    if (!field_wavefront_usable_for_merit(entry_index)) {
                        continue;
                    }
                    const FieldWavefront& field_wavefront = field_wavefronts[entry_index];
                    const double value = field_wavefront.stats.rms_waves;
                    const double penalty = operand_penalty(value, operand);
                    add_contribution(make_contribution(
                        "operand_field",
                        operand.name,
                        operand.mode,
                        value,
                        operand.target,
                        operand.lower,
                        operand.upper,
                        operand.weight,
                        penalty,
                        field_wavefront.field_index,
                        field_wavefront.wavelength_index));
                    added_wavefront_operand = true;
                }
                if (!added_wavefront_operand) {
                    add_contribution(make_contribution(
                        "operand_field",
                        operand.name,
                        operand.mode,
                        std::numeric_limits<double>::quiet_NaN(),
                        operand.target,
                        operand.lower,
                        operand.upper,
                        operand.weight,
                        scoring_weight(operand.weight) * config.fail_merit,
                        0,
                        0));
                }
                continue;
            }
            if (is_single_ray_wavefront_guard_operand_name(operand.name)) {
                bool added_wavefront_operand = false;
                for (std::size_t entry_index = 0; entry_index < field_wavefronts.size(); ++entry_index) {
                    if (!field_wavefront_usable_for_merit(entry_index)) {
                        continue;
                    }
                    const FieldWavefront& field_wavefront = field_wavefronts[entry_index];
                    const double denom = std::max(
                        1.0e-12,
                        std::abs(field_wavefront.stats.rms_waves));
                    const double value =
                        std::isfinite(field_wavefront.stats.single_ray_max_abs_waves)
                            ? field_wavefront.stats.single_ray_max_abs_waves / denom
                            : std::numeric_limits<double>::quiet_NaN();
                    const double penalty = operand_penalty(value, operand);
                    add_contribution(make_contribution(
                        "operand_field",
                        operand.name,
                        operand.mode,
                        value,
                        operand.target,
                        operand.lower,
                        operand.upper,
                        operand.weight,
                        penalty,
                        field_wavefront.field_index,
                        field_wavefront.wavelength_index));
                    added_wavefront_operand = true;
                }
                if (!added_wavefront_operand) {
                    add_contribution(make_contribution(
                        "operand_field",
                        operand.name,
                        operand.mode,
                        std::numeric_limits<double>::quiet_NaN(),
                        operand.target,
                        operand.lower,
                        operand.upper,
                        operand.weight,
                        scoring_weight(operand.weight) * config.fail_merit,
                        0,
                        0));
                }
                continue;
            }
            const double value = operand_value(system, score, operand);
            const double penalty = operand_penalty(value, operand);
            add_contribution(make_contribution(
                "operand",
                operand.name,
                operand.mode,
                value,
                operand.target,
                operand.lower,
                operand.upper,
                operand.weight,
                penalty,
                operand.index,
                operand.term));
        }
    
}
}  // namespace analysis_detail

}  // namespace optiland
