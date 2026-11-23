/*
 * Provides shared text/CSV helpers, JSON escaping and finite-number output for file I/O.
 * System, score and catalog readers/writers use the value/parser types in detail.hpp.
 * Non-finite JSON numbers serialize as null; stream parsing and caller-selected
 * precision retain the existing representation of saved native inputs and diagnostics.
 */

#include "detail.hpp"

namespace optiland {
using namespace io_detail;

namespace io_detail {
void write_json_number(std::ofstream& out, double value) {
    if (std::isfinite(value)) {
        out << value;
    } else {
        out << "null";
    }
}
}  // namespace io_detail

namespace io_detail {
std::string json_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += ch;
                break;
        }
    }
    return out;
}
}  // namespace io_detail

namespace io_detail {
std::string read_text_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("could not open " + path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
}  // namespace io_detail

namespace io_detail {
std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> cols;
    std::string cur;
    bool quoted = false;
    for (char ch : line) {
        if (ch == '"') {
            quoted = !quoted;
        } else if (ch == ',' && !quoted) {
            cols.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    cols.push_back(cur);
    return cols;
}
}  // namespace io_detail

namespace io_detail {
std::vector<double> parse_doubles(const std::string& text) {
    std::vector<double> values;
    std::istringstream in(text);
    double value = 0.0;
    while (in >> value) {
        values.push_back(value);
    }
    return values;
}
}  // namespace io_detail

}  // namespace optiland
