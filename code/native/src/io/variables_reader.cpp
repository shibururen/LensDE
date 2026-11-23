/*
 * Loads optimizer variable records from a JSON array or a variables wrapper object.
 * Native CLI preparation uses these physical bounds, initial values and surface indices.
 * Record order, invalid-bound filtering, glass-group IDs and asphere reference metadata
 * must preserve the column meaning of initial populations and optimizer result vectors.
 */

#include "detail.hpp"

namespace optiland {
using namespace io_detail;

bool load_variables_json(const std::string& path, std::vector<VariableSpec>* variables, std::string* error) {
    if (!variables) {
        return false;
    }
    try {
        Json root = JsonParser(read_text_file(path)).parse();
        const Json& items = root.is_array() ? root : root.at("variables");
        variables->clear();
        if (!items.is_array()) {
            return true;
        }
        for (const auto& item : items.array) {
            VariableSpec var;
            var.kind = variable_kind_from_string(item.at("kind").as_string("radius"));
            var.index = static_cast<std::size_t>(std::max(0.0, item.at("index").as_number(0.0)));
            var.lower = item.at("lower").as_number(0.0);
            var.upper = item.at("upper").as_number(0.0);
            var.initial = item.at("initial").as_number(item.at("value").as_number(0.0));
            var.role = item.at("role").as_string(item.at("kind").as_string(""));
            var.term = static_cast<std::size_t>(std::max(0.0, item.at("term").as_number(0.0)));
            var.glass_group =
                static_cast<int>(item.at("glass_group").as_number(item.at("group").as_number(-1.0)));
            var.asphere_reference_radius = item.at("asphere_reference_radius").as_number(0.0);
            var.edge_sag_lower = item.at("edge_sag_lower").as_number(
                std::numeric_limits<double>::quiet_NaN());
            var.edge_sag_upper = item.at("edge_sag_upper").as_number(
                std::numeric_limits<double>::quiet_NaN());
            if (var.upper >= var.lower) {
                variables->push_back(var);
            }
        }
        return true;
    } catch (const std::exception& e) {
        if (error) {
            *error = e.what();
        }
        return false;
    }
}

}  // namespace optiland
