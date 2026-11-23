/*
 * Provide optimizer timing and research-snapshot path/JSON formatting helpers.
 * Adaptive snapshot/trace writers share these utilities without owning optimizer population or RNG state.
 * Preserve path joining, escaping and nonfinite-number formatting in persisted observation files.
 */
#include "support.hpp"

namespace optiland {
using namespace optimizer_detail;

namespace optimizer_detail {

double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::string research_snapshot_path(
    const std::string& directory,
    const std::string& filename) {
    if (directory.empty()) {
        return filename;
    }
    const char last = directory.back();
    if (last == '/' || last == '\\') {
        return directory + filename;
    }
#ifdef _WIN32
    return directory + "\\" + filename;
#else
    return directory + "/" + filename;
#endif
}

std::string research_json_escape(const std::string& value) {
    std::ostringstream out;
    for (const unsigned char ch : value) {
        switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20) {
                    out << "\\u" << std::hex << std::setw(4)
                        << std::setfill('0') << static_cast<int>(ch)
                        << std::dec << std::setfill(' ');
                } else {
                    out << static_cast<char>(ch);
                }
                break;
        }
    }
    return out.str();
}

void research_write_json_number(std::ostream& out, double value) {
    if (std::isfinite(value)) {
        out << value;
    } else {
        out << "null";
    }
}

}  // namespace optimizer_detail

}  // namespace optiland
