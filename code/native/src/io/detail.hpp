#pragma once
#include "optiland/fileio.hpp"

#include <fstream>
#include <iomanip>
#include <cmath>
#include <cctype>
#include <algorithm>
#include <map>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>


namespace optiland {
namespace io_detail {
struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Json> array;
    std::map<std::string, Json> object;

    const Json& at(const std::string& key) const {
        static const Json null_json;
        auto it = object.find(key);
        return it == object.end() ? null_json : it->second;
    }
    bool is_null() const { return type == Type::Null; }
    bool is_object() const { return type == Type::Object; }
    bool is_array() const { return type == Type::Array; }
    std::string as_string(const std::string& fallback = "") const {
        return type == Type::String ? string : fallback;
    }
    double as_number(double fallback = 0.0) const {
        return type == Type::Number ? number : fallback;
    }
    bool as_bool(bool fallback = false) const {
        return type == Type::Bool ? boolean : fallback;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string text) : text_(std::move(text)) {}

    Json parse() {
        skip_ws();
        Json value = parse_value();
        skip_ws();
        return value;
    }

private:
    Json parse_value() {
        skip_ws();
        if (pos_ >= text_.size()) {
            throw std::runtime_error("unexpected end of json");
        }
        const char ch = text_[pos_];
        if (ch == '{') {
            return parse_object();
        }
        if (ch == '[') {
            return parse_array();
        }
        if (ch == '"') {
            Json v;
            v.type = Json::Type::String;
            v.string = parse_string();
            return v;
        }
        if (ch == 't' || ch == 'f') {
            return parse_bool();
        }
        if (ch == 'n') {
            expect("null");
            return Json{};
        }
        return parse_number();
    }

    Json parse_object() {
        Json v;
        v.type = Json::Type::Object;
        ++pos_;
        skip_ws();
        if (peek('}')) {
            ++pos_;
            return v;
        }
        while (true) {
            skip_ws();
            std::string key = parse_string();
            skip_ws();
            if (!peek(':')) {
                throw std::runtime_error("expected ':'");
            }
            ++pos_;
            v.object[key] = parse_value();
            skip_ws();
            if (peek('}')) {
                ++pos_;
                break;
            }
            if (!peek(',')) {
                throw std::runtime_error("expected ','");
            }
            ++pos_;
        }
        return v;
    }

    Json parse_array() {
        Json v;
        v.type = Json::Type::Array;
        ++pos_;
        skip_ws();
        if (peek(']')) {
            ++pos_;
            return v;
        }
        while (true) {
            v.array.push_back(parse_value());
            skip_ws();
            if (peek(']')) {
                ++pos_;
                break;
            }
            if (!peek(',')) {
                throw std::runtime_error("expected ',' in array");
            }
            ++pos_;
        }
        return v;
    }

    Json parse_bool() {
        Json v;
        v.type = Json::Type::Bool;
        if (text_.compare(pos_, 4, "true") == 0) {
            v.boolean = true;
            pos_ += 4;
        } else {
            expect("false");
            v.boolean = false;
        }
        return v;
    }

    Json parse_number() {
        Json v;
        v.type = Json::Type::Number;
        if (text_.compare(pos_, 8, "Infinity") == 0) {
            v.number = std::numeric_limits<double>::infinity();
            pos_ += 8;
            return v;
        }
        if (text_.compare(pos_, 9, "-Infinity") == 0) {
            v.number = -std::numeric_limits<double>::infinity();
            pos_ += 9;
            return v;
        }
        if (text_.compare(pos_, 3, "NaN") == 0) {
            v.number = std::numeric_limits<double>::quiet_NaN();
            pos_ += 3;
            return v;
        }
        const char* start = text_.c_str() + pos_;
        char* end = nullptr;
        v.number = std::strtod(start, &end);
        if (end == start) {
            throw std::runtime_error("invalid number");
        }
        pos_ = static_cast<std::size_t>(end - text_.c_str());
        return v;
    }

    std::string parse_string() {
        if (!peek('"')) {
            throw std::runtime_error("expected string");
        }
        ++pos_;
        std::string out;
        while (pos_ < text_.size()) {
            char ch = text_[pos_++];
            if (ch == '"') {
                return out;
            }
            if (ch == '\\' && pos_ < text_.size()) {
                char esc = text_[pos_++];
                switch (esc) {
                    case '"':
                    case '\\':
                    case '/':
                        out.push_back(esc);
                        break;
                    case 'b':
                        out.push_back('\b');
                        break;
                    case 'f':
                        out.push_back('\f');
                        break;
                    case 'n':
                        out.push_back('\n');
                        break;
                    case 'r':
                        out.push_back('\r');
                        break;
                    case 't':
                        out.push_back('\t');
                        break;
                    default:
                        out.push_back(esc);
                        break;
                }
            } else {
                out.push_back(ch);
            }
        }
        throw std::runtime_error("unterminated string");
    }

    void expect(const char* literal) {
        const std::size_t n = std::char_traits<char>::length(literal);
        if (text_.compare(pos_, n, literal) != 0) {
            throw std::runtime_error(std::string("expected ") + literal);
        }
        pos_ += n;
    }

    void skip_ws() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
    }

    bool peek(char ch) const {
        return pos_ < text_.size() && text_[pos_] == ch;
    }

    std::string text_;
    std::size_t pos_ = 0;
};

const char* surface_kind_name(SurfaceKind kind);
const char* ray_aiming_mode_name(RayAimingMode mode);
const char* ray_aiming_stop_radius_source_name(RayAimingStopRadiusSource source);
std::string normalized_name(std::string value);
std::string canonical_distortion_model(
    const std::string& model,
    const std::string& fallback = "auto");
std::string canonical_wavefront_reference(
    const std::string& reference,
    const std::string& fallback = "centroid_sphere");
RayAimingMode ray_aiming_mode_from_string(const std::string& mode);
RayAimingStopRadiusSource ray_aiming_stop_radius_source_from_string(
    const std::string& source,
    RayAimingStopRadiusSource fallback = RayAimingStopRadiusSource::Paraxial);
const char* trace_failure_policy_name(TraceFailurePolicy policy);
TraceFailurePolicy trace_failure_policy_from_string(const std::string& raw);
const char* merit_category_name(MeritCategory category);
void write_json_number(std::ofstream& out, double value);
std::string json_escape(const std::string& value);
std::string read_text_file(const std::string& path);
std::vector<std::string> split_csv_line(const std::string& line);
std::vector<double> parse_doubles(const std::string& text);
bool load_material_yaml(const std::string& path, const std::string& name, const std::string& reference, Material* material);
Material material_from_json(const Json& json);
void read_merit_targets(const Json& root, MeritConfig* config);
void read_merit_manufacturing(const Json& root, MeritConfig* config);
void read_merit_quality(const Json& root, MeritConfig* config);
void read_merit_vignetting(const Json& root, MeritConfig* config);
void read_merit_chromatic_quality(const Json& root, MeritConfig* config);
void read_merit_sampling(const Json& root, MeritConfig* config);
void read_merit_trace(const Json& root, MeritConfig* config);
void read_merit_image_weights(const Json& root, MeritConfig* config);
void read_merit_constraints(const Json& root, MeritConfig* config);
}  // namespace io_detail
bool save_system_json(const LensSystem& system, const std::string& path);
bool save_score_json(const Score& score, const std::string& path);
bool load_glass_from_catalog(
    const std::string& database_root,
    const std::string& name,
    const std::string& reference,
    Material* material,
    std::string* error);
bool load_optiland_json(const std::string& path, LensSystem* system, std::string* error);
SurfaceKind surface_kind_from_string(const std::string& kind);
Material simple_material_from_json(const io_detail::Json& json);
VariableKind variable_kind_from_string(const std::string& kind);
bool load_system_json(const std::string& path, LensSystem* system, std::string* error);
bool load_variables_json(const std::string& path, std::vector<VariableSpec>* variables, std::string* error);
void build_glass_neighbor_indices(GlassSearchContext& context);
bool load_glass_search_config(
    const std::string& path,
    const std::string& database_root,
    GlassSearchContext* context,
    std::string* error,
    const std::vector<double>& required_wavelengths_um);
bool validate_fixed_material_wavelength_coverage(
    const LensSystem& system,
    const std::vector<VariableSpec>& variables,
    std::string* error);
}  // namespace optiland
