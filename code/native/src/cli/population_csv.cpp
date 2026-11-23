// Read physical population rows and write the optimizer final population with 17-digit values.
// Seed preparation and score-output code share the CSV cell and recorded-row parsing helpers.
// Preserve row/column order, dimensions, and serialization precision rather than treating CSV as an RNG checkpoint.

#include "cli_internal.hpp"

namespace lensde { namespace cli { namespace detail {

bool load_population_csv(
    const std::string& path,
    int dim,
    std::vector<std::vector<double>>* rows,
    std::string* error) {
    if (!rows) {
        return false;
    }
    rows->clear();
    std::ifstream in(path);
    if (!in) {
        if (error) {
            *error = "cannot open " + path;
        }
        return false;
    }
    std::string line;
    int line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.resize(comment);
        }
        for (char& ch : line) {
            if (ch == ',' || ch == ';' || ch == '\t') {
                ch = ' ';
            }
        }
        std::istringstream ss(line);
        std::vector<double> values;
        double value = 0.0;
        while (ss >> value) {
            values.push_back(value);
        }
        if (values.empty()) {
            continue;
        }
        if (static_cast<int>(values.size()) != dim) {
            if (error) {
                *error = "line " + std::to_string(line_no) +
                         " has " + std::to_string(values.size()) +
                         " values, expected " + std::to_string(dim);
            }
            return false;
        }
        rows->push_back(std::move(values));
    }
    return true;
}

bool save_population_csv(const OptimizationResult& result, const std::string& path) {
    if (result.final_population_size <= 0 ||
        result.final_population_dim <= 0 ||
        result.final_population_values.empty()) {
        return false;
    }
    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out.precision(17);
    const int rows = result.final_population_size;
    const int dim = result.final_population_dim;
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < dim; ++j) {
            if (j > 0) {
                out << ',';
            }
            out << result.final_population_values[static_cast<std::size_t>(i) * dim + j];
        }
        out << '\n';
    }
    return true;
}

std::vector<std::string> split_csv_line_simple(const std::string& line) {
    std::vector<std::string> cells;
    std::string cell;
    bool in_quotes = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '"') {
            if (in_quotes && i + 1 < line.size() && line[i + 1] == '"') {
                cell.push_back('"');
                ++i;
            } else {
                in_quotes = !in_quotes;
            }
            continue;
        }
        if (ch == ',' && !in_quotes) {
            cells.push_back(cell);
            cell.clear();
            continue;
        }
        cell.push_back(ch);
    }
    cells.push_back(cell);
    return cells;
}

bool parse_double_cell(const std::string& cell, double* out) {
    if (!out) {
        return false;
    }
    const char* begin = cell.c_str();
    char* end = nullptr;
    const double value = std::strtod(begin, &end);
    if (end == begin) {
        return false;
    }
    while (*end != '\0') {
        if (!std::isspace(static_cast<unsigned char>(*end))) {
            return false;
        }
        ++end;
    }
    *out = std::isfinite(value) ? value : 0.0;
    return true;
}

bool parse_bool_cell(const std::string& cell, bool fallback ) {
    std::string value;
    value.reserve(cell.size());
    for (char ch : cell) {
        if (!std::isspace(static_cast<unsigned char>(ch))) {
            value.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }
    if (value.empty()) {
        return fallback;
    }
    if (value == "1" || value == "true" || value == "yes") {
        return true;
    }
    if (value == "0" || value == "false" || value == "no") {
        return false;
    }
    double numeric = 0.0;
    if (parse_double_cell(cell, &numeric)) {
        return std::abs(numeric) > 0.5;
    }
    return fallback;
}

bool parse_numeric_seed_row_line(
    const std::string& line,
    int dim,
    std::vector<double>* row) {
    if (!row) {
        return false;
    }
    row->clear();
    row->reserve(static_cast<std::size_t>(dim));
    const char* cursor = line.c_str();
    for (int j = 0; j < dim; ++j) {
        while (*cursor != '\0' &&
               (std::isspace(static_cast<unsigned char>(*cursor)) ||
                *cursor == ',' || *cursor == ';' || *cursor == '\t')) {
            ++cursor;
        }
        char* end = nullptr;
        const double value = std::strtod(cursor, &end);
        if (end == cursor) {
            return false;
        }
        row->push_back(std::isfinite(value) ? value : 0.0);
        cursor = end;
    }
    return static_cast<int>(row->size()) == dim;
}

} } }
