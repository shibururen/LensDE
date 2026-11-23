// Write the seed-selection summary, source-row identities, and selection diagnostics as JSON.
// The selector supplies its completed decisions; this writer does not launch or rerun optimization.
// Keep disclosed coordinates, counts, and numeric diagnostics aligned with the selected population CSV.

#include "cli_internal.hpp"

namespace lensde { namespace cli { namespace detail {

void write_selection_summary_json(
    const std::string& path,
    int input_count,
    int deduped_count,
    int target,
    const std::vector<std::vector<double>>& selected,
    const std::vector<VariableSpec>& variables,
    double min_active_fraction,
    double min_curvature_active_fraction,
    double selection_seconds,
    double write_seconds) {
    std::vector<int> active_counts;
    active_counts.reserve(selected.size());
    std::vector<int> active_curvature_counts;
    active_curvature_counts.reserve(selected.size());
    std::unordered_set<std::string> masks;
    std::unordered_set<std::string> row_keys;
    int duplicate_rows = 0;
    int range_ge_001 = 0;
    int range_ge_01 = 0;
    int active_ge_min = 0;
    int curvature_active_ge_min = 0;
    int undercovered_variables = 0;
    int undercovered_curvature_variables = 0;
    double min_variable_active_frac = selected.empty() || variables.empty() ? 0.0 : 1.0;
    double min_curvature_active_frac = 1.0;
    int curvature_variable_count = 0;
    for (const auto& row : selected) {
        active_counts.push_back(active_count_cpp(row, variables));
        active_curvature_counts.push_back(active_curvature_count_cpp(row, variables));
        masks.insert(row_curvature_mask_cpp(row, variables));
        if (!row_keys.insert(seed_row_key_cpp(row, variables)).second) {
            ++duplicate_rows;
        }
    }
    for (std::size_t j = 0; j < variables.size(); ++j) {
        const VariableSpec& variable = variables[j];
        double min_value = std::numeric_limits<double>::infinity();
        double max_value = -std::numeric_limits<double>::infinity();
        for (const auto& row : selected) {
            if (j >= row.size()) {
                continue;
            }
            const double value = seed_axis_coordinate_cpp(row[j], variable);
            min_value = std::min(min_value, value);
            max_value = std::max(max_value, value);
        }
        if (!std::isfinite(min_value) || !std::isfinite(max_value)) {
            continue;
        }
        const double span = seed_axis_span_cpp(variable);
        const double range_frac = (max_value - min_value) / span;
        if (range_frac >= 0.001) {
            ++range_ge_001;
        }
        if (range_frac >= 0.01) {
            ++range_ge_01;
        }
        const double active_tolerance = seed_activity_tolerance_cpp(variable);
        const double initial = seed_axis_coordinate_cpp(variable.initial, variable);
        int active_count = 0;
        for (const auto& row : selected) {
            if (j >= row.size()) {
                continue;
            }
            const double value = seed_axis_coordinate_cpp(row[j], variable);
            if (std::abs(value - initial) > active_tolerance) {
                ++active_count;
            }
        }
        const double active_frac =
            static_cast<double>(active_count) / std::max<std::size_t>(1, selected.size());
        const bool is_curvature = variable.kind == VariableKind::Curvature;
        const double required_frac = is_curvature ? min_curvature_active_fraction : min_active_fraction;
        if (active_frac + 1.0e-15 >= required_frac) {
            ++active_ge_min;
            if (is_curvature) {
                ++curvature_active_ge_min;
            }
        } else {
            ++undercovered_variables;
            if (is_curvature) {
                ++undercovered_curvature_variables;
            }
        }
        min_variable_active_frac = std::min(min_variable_active_frac, active_frac);
        if (is_curvature) {
            ++curvature_variable_count;
            min_curvature_active_frac = std::min(min_curvature_active_frac, active_frac);
        }
    }
    if (curvature_variable_count == 0) {
        min_curvature_active_frac = 0.0;
    }
    std::sort(active_counts.begin(), active_counts.end());
    std::sort(active_curvature_counts.begin(), active_curvature_counts.end());
    const int median_active = active_counts.empty()
                                  ? 0
                                  : active_counts[active_counts.size() / 2];
    const int median_active_curvature = active_curvature_counts.empty()
                                            ? 0
                                            : active_curvature_counts[active_curvature_counts.size() / 2];

    std::ofstream out(path);
    if (!out) {
        return;
    }
    out << std::setprecision(12);
    out << "{\n";
    out << "  \"engine\": \"cpp\",\n";
    out << "  \"input_count\": " << input_count << ",\n";
    out << "  \"deduped_count\": " << deduped_count << ",\n";
    out << "  \"target\": " << target << ",\n";
    out << "  \"selected_count\": " << selected.size() << ",\n";
    out << "  \"elapsed_seconds\": " << selection_seconds << ",\n";
    out << "  \"selected_seed_population_write_seconds\": " << write_seconds << ",\n";
    out << "  \"variables\": " << variables.size() << ",\n";
    out << "  \"variables_with_range_frac_ge_0_001\": " << range_ge_001 << ",\n";
    out << "  \"variables_with_range_frac_ge_0_01\": " << range_ge_01 << ",\n";
    out << "  \"variables_with_active_frac_ge_min\": " << active_ge_min << ",\n";
    out << "  \"curvature_variables_with_active_frac_ge_min\": " << curvature_active_ge_min << ",\n";
    out << "  \"curvature_variables\": " << curvature_variable_count << ",\n";
    out << "  \"min_variable_active_frac\": " << min_variable_active_frac << ",\n";
    out << "  \"min_curvature_active_frac\": " << min_curvature_active_frac << ",\n";
    out << "  \"diversity_min_active_fraction\": " << min_active_fraction << ",\n";
    out << "  \"diversity_min_curvature_active_fraction\": " << min_curvature_active_fraction << ",\n";
    out << "  \"undercovered_variables\": " << undercovered_variables << ",\n";
    out << "  \"undercovered_curvature_variables\": " << undercovered_curvature_variables << ",\n";
    out << "  \"median_active_vars\": " << median_active << ",\n";
    out << "  \"median_active_curvature_vars\": " << median_active_curvature << ",\n";
    out << "  \"unique_curvature_masks\": " << masks.size() << ",\n";
    out << "  \"duplicate_rows\": " << duplicate_rows << "\n";
    out << "}\n";
}

} } }
