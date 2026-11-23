/*
 * Loads ordered explicit bound, operand and glass-proximity records from merit JSON.
 * This is the final settings-family reader called by load_merit_json.
 * Preserve bounds/variable_bounds aliases, surface/term indices and array order so
 * the analysis pipeline constrains the same physical quantities with the same weights.
 */

#include "detail.hpp"

namespace optiland {
using namespace io_detail;

namespace io_detail {
void read_merit_constraints(const Json& root, MeritConfig* config) {
const Json& bounds = root.at("bounds").is_array() ? root.at("bounds") : root.at("variable_bounds");
        if (bounds.is_array()) {
            config->bounds.clear();
            for (const auto& item : bounds.array) {
                BoundConstraint bound;
                bound.kind = item.at("kind").as_string();
                bound.index = static_cast<std::size_t>(std::max(0.0, item.at("index").as_number(0.0)));
                bound.term = static_cast<std::size_t>(std::max(0.0, item.at("term").as_number(0.0)));
                bound.lower = item.at("lower").as_number(-kInf);
                bound.upper = item.at("upper").as_number(kInf);
                bound.weight = item.at("weight").as_number(1000.0);
                if (!bound.kind.empty()) {
                    config->bounds.push_back(bound);
                }
            }
        }
        const Json& operands = root.at("operands").is_array() ? root.at("operands") : root.at("constraints");
        if (operands.is_array()) {
            config->operands.clear();
            for (const auto& item : operands.array) {
                MeritOperand operand;
                operand.name = item.at("name").as_string(item.at("kind").as_string(item.at("operand").as_string()));
                operand.mode = item.at("mode").as_string("target");
                operand.index = static_cast<std::size_t>(std::max(0.0, item.at("index").as_number(0.0)));
                operand.term = static_cast<std::size_t>(std::max(0.0, item.at("term").as_number(0.0)));
                operand.target = item.at("target").as_number(0.0);
                operand.lower = item.at("lower").as_number(-kInf);
                operand.upper = item.at("upper").as_number(kInf);
                operand.weight = item.at("weight").as_number(1.0);
                if (!operand.name.empty()) {
                    config->operands.push_back(operand);
                }
            }
        }
        const Json& glass_proximity = root.at("glass_proximity");
        if (glass_proximity.is_array()) {
            config->glass_proximity.clear();
            for (const auto& item : glass_proximity.array) {
                GlassProximityConstraint constraint;
                constraint.index =
                    static_cast<std::size_t>(std::max(0.0, item.at("index").as_number(0.0)));
                constraint.weight = item.at("weight").as_number(1.0);
                constraint.tolerance = item.at("tolerance").as_number(0.0);
                constraint.region_weight = item.at("region_weight").as_number(0.0);
                constraint.region_tolerance =
                    item.at("region_tolerance").as_number(0.0);
                constraint.nd_scale = std::max(1.0e-12, item.at("nd_scale").as_number(0.01));
                constraint.vd_scale = std::max(1.0e-12, item.at("vd_scale").as_number(5.0));
                constraint.vd_metric = item.at("vd_metric").as_string("absolute");
                constraint.vd_relative_scale = std::max(
                    1.0e-12,
                    item.at("vd_relative_scale").as_number(0.05));
                constraint.smooth_temperature = std::max(
                    0.0,
                    item.at("smooth_temperature").as_number(
                        item.at("smoothness").as_number(0.0)));
                const Json& points = item.at("points").is_array() ? item.at("points") : item.at("candidates");
                if (points.is_array()) {
                    for (const auto& point : points.array) {
                        GlassProximityPoint glass_point;
                        glass_point.nd = point.at("nd").as_number(std::numeric_limits<double>::quiet_NaN());
                        glass_point.vd = point.at("vd").as_number(std::numeric_limits<double>::quiet_NaN());
                        if (std::isfinite(glass_point.nd) &&
                            std::isfinite(glass_point.vd) &&
                            glass_point.nd > 1.0 &&
                            glass_point.vd > 0.0) {
                            constraint.points.push_back(glass_point);
                        }
                    }
                }
                const Json& region_points = item.at("region_points");
                if (region_points.is_array()) {
                    for (const auto& point : region_points.array) {
                        GlassProximityPoint glass_point;
                        glass_point.nd = point.at("nd").as_number(
                            std::numeric_limits<double>::quiet_NaN());
                        glass_point.vd = point.at("vd").as_number(
                            std::numeric_limits<double>::quiet_NaN());
                        if (std::isfinite(glass_point.nd) &&
                            std::isfinite(glass_point.vd) &&
                            glass_point.nd > 1.0 &&
                            glass_point.vd > 0.0) {
                            constraint.region_points.push_back(glass_point);
                        }
                    }
                }
                if (!constraint.points.empty() || !constraint.region_points.empty()) {
                    config->glass_proximity.push_back(std::move(constraint));
                }
            }
        }
}
}  // namespace io_detail

}  // namespace optiland
