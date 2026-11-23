// Read the explicit asphere-oracle fixture and emit its native intersection evidence.
// The broker application selects this diagnostic route instead of launching island optimization.
// Preserve fixture field order, vector parsing, root-policy decisions, and reported failures.

#include "interfaces.hpp"

namespace lensde::broker {

std::vector<std::string> split_fixture_field(
    const std::string& value,
    char delimiter) {
    std::vector<std::string> result;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, delimiter)) {
        result.push_back(item);
    }
    return result;
}

std::vector<double> parse_fixture_vector(const std::string& value) {
    if (value.empty() || value == "-") {
        return {};
    }
    std::vector<double> result;
    for (const std::string& item : split_fixture_field(value, ',')) {
        result.push_back(std::stod(item));
    }
    return result;
}

int run_asphere_oracle_fixture(
    const std::string& fixture_path,
    const std::string& output_path) {
    std::ifstream input(fixture_path);
    if (!input) {
        throw std::runtime_error(
            "cannot open asphere oracle fixture: " + fixture_path);
    }
    std::ofstream output;
    if (!output_path.empty()) {
        output.open(output_path, std::ios::trunc);
        if (!output) {
            throw std::runtime_error(
                "cannot create asphere oracle output: " + output_path);
        }
    }
    std::ostream& out = output_path.empty() ? std::cout : output;
    out << std::setprecision(17);
    optiland::set_asphere_root_policy(
        optiland::AsphereRootPolicy::UnifiedCertifiedExperimental);
    int total = 0;
    int passed = 0;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::vector<std::string> fields =
            split_fixture_field(line, '\t');
        if (fields.size() != 8) {
            throw std::runtime_error(
                "invalid asphere oracle fixture row " +
                std::to_string(total + 1));
        }
        optiland::Surface surface;
        surface.kind = optiland::SurfaceKind::EvenAsphere;
        surface.radius = fields[1] == "+inf"
                             ? std::numeric_limits<double>::infinity()
                             : std::stod(fields[1]);
        surface.conic = std::stod(fields[2]);
        surface.asphere_coefficients = parse_fixture_vector(fields[3]);
        const std::vector<double> p = parse_fixture_vector(fields[4]);
        const std::vector<double> d = parse_fixture_vector(fields[5]);
        if (p.size() != 3 || d.size() != 3) {
            throw std::runtime_error(
                "invalid ray vector in oracle fixture row " +
                std::to_string(total + 1));
        }
        optiland::Ray ray;
        ray.valid = true;
        ray.p = {p[0], p[1], p[2]};
        ray.d = {d[0], d[1], d[2]};
        const bool expected_hit = fields[6] == "hit";
        const double expected_t = expected_hit
                                      ? std::stod(fields[7])
                                      : std::numeric_limits<double>::quiet_NaN();
        double actual_t = std::numeric_limits<double>::quiet_NaN();
        const bool actual_hit = optiland::intersect_surface(
            surface, ray, &actual_t, true);
        const double tolerance = expected_hit
                                     ? std::max(
                                           1.0e-9,
                                           1.0e-9 * std::abs(expected_t))
                                     : 0.0;
        const bool row_pass =
            actual_hit == expected_hit &&
            (!expected_hit ||
             (std::isfinite(actual_t) &&
              std::abs(actual_t - expected_t) <= tolerance));
        ++total;
        passed += row_pass ? 1 : 0;
        out << "{\"ray_case_id\":\"" << fields[0]
            << "\",\"expected_status\":\"" << fields[6]
            << "\",\"actual_status\":\""
            << (actual_hit ? "hit" : "miss") << "\",\"expected_t\":";
        if (expected_hit) {
            out << expected_t;
        } else {
            out << "null";
        }
        out << ",\"actual_t\":";
        if (actual_hit && std::isfinite(actual_t)) {
            out << actual_t;
        } else {
            out << "null";
        }
        out << ",\"pass\":" << (row_pass ? "true" : "false")
            << "}\n";
    }
    out << "{\"summary\":true,\"policy\":\"unified_certified_experimental\""
        << ",\"total\":" << total << ",\"passed\":" << passed
        << ",\"failed\":" << (total - passed) << "}\n";
    return passed == total ? 0 : 4;
}

} // namespace lensde::broker
