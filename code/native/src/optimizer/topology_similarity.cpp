/*
 * Parse structural topology keys and compare shape/family similarity with the configured costs.
 * Adaptive family matching, basin selection and avoidance reuse these ordered token representations.
 * Preserve parsing, alignment cost and structural-difference rules for existing archived keys.
 */
#include "support.hpp"

namespace optiland {
using namespace optimizer_detail;

namespace optimizer_detail {

ParsedStructuralTopologyKey parse_structural_topology_key(const std::string& key) {
    ParsedStructuralTopologyKey parsed;
    std::size_t start = 0;
    while (start <= key.size()) {
        const std::size_t end = key.find('|', start);
        const std::string part =
            key.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!part.empty()) {
            if (part[0] == 'e') {
                parsed.element_count = part.substr(1);
            } else if (part[0] == 's') {
                parsed.stop_after_elements = part.substr(1);
            } else if (part.rfind("fp", 0) == 0) {
                parsed.front_power_sign = part.substr(2);
            } else if (part.rfind("rp", 0) == 0) {
                parsed.rear_power_sign = part.substr(2);
            } else if (part[0] == 'l') {
                std::size_t token_start = 1;
                while (token_start <= part.size()) {
                    const std::size_t token_end = part.find('.', token_start);
                    const std::string token =
                        part.substr(
                            token_start,
                            token_end == std::string::npos
                                ? std::string::npos
                                : token_end - token_start);
                    if (!token.empty()) {
                        parsed.element_tokens.push_back(token);
                    }
                    if (token_end == std::string::npos) {
                        break;
                    }
                    token_start = token_end + 1;
                }
            }
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return parsed;
}

double topology_similarity_char_cost(char a, char b, double weak_cost) {
    if (a == b) {
        return 0.0;
    }
    if (a == 'w' || b == 'w') {
        return weak_cost;
    }
    return 1.0;
}

int structural_topology_shape_diff_count_parsed(
    const ParsedStructuralTopologyKey& a,
    const ParsedStructuralTopologyKey& b) {
    const std::size_t count =
        std::max(a.element_tokens.size(), b.element_tokens.size());
    if (count == 0U) {
        return 0;
    }
    int diff_count = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (i >= a.element_tokens.size() || i >= b.element_tokens.size()) {
            ++diff_count;
            continue;
        }
        const std::string& left = a.element_tokens[i];
        const std::string& right = b.element_tokens[i];
        const std::string left_shape = left.size() >= 2 ? left.substr(0, 2) : left;
        const std::string right_shape = right.size() >= 2 ? right.substr(0, 2) : right;
        if (left_shape != right_shape) {
            ++diff_count;
        }
    }
    return diff_count;
}

int structural_topology_shape_diff_count(const std::string& a_key,
                                         const std::string& b_key) {
    if (a_key == b_key) {
        return 0;
    }
    if (a_key.empty() || b_key.empty()) {
        return std::numeric_limits<int>::max();
    }
    return structural_topology_shape_diff_count_parsed(
        parse_structural_topology_key(a_key),
        parse_structural_topology_key(b_key));
}

double weighted_structural_topology_similarity(const std::string& a_key,
                                               const std::string& b_key) {
    if (a_key == b_key) {
        return 1.0;
    }
    if (a_key.empty() || b_key.empty()) {
        return 0.0;
    }
    const ParsedStructuralTopologyKey a = parse_structural_topology_key(a_key);
    const ParsedStructuralTopologyKey b = parse_structural_topology_key(b_key);

    constexpr double element_count_weight = 4.0;
    constexpr double stop_weight = 3.0;
    constexpr double front_rear_power_weight = 0.5;
    constexpr double shape_weight = 1.0;
    constexpr double element_power_weight = 0.6;
    constexpr double surface_weight = 3.0;
    constexpr double missing_element_weight = 8.0;
    constexpr double weak_surface_cost = 0.7;

    double cost = 0.0;
    double total = 0.0;
    auto add_string_match = [&](const std::string& left,
                                const std::string& right,
                                double weight) {
        total += weight;
        if (left != right) {
            cost += weight;
        }
    };
    auto add_char_match = [&](char left, char right, double weight) {
        total += weight;
        cost += weight * topology_similarity_char_cost(left, right, weak_surface_cost);
    };

    add_string_match(a.element_count, b.element_count, element_count_weight);
    add_string_match(a.stop_after_elements, b.stop_after_elements, stop_weight);
    add_char_match(
        a.front_power_sign.empty() ? 'w' : a.front_power_sign[0],
        b.front_power_sign.empty() ? 'w' : b.front_power_sign[0],
        front_rear_power_weight);
    add_char_match(
        a.rear_power_sign.empty() ? 'w' : a.rear_power_sign[0],
        b.rear_power_sign.empty() ? 'w' : b.rear_power_sign[0],
        front_rear_power_weight);

    const std::size_t count =
        std::max(a.element_tokens.size(), b.element_tokens.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (i >= a.element_tokens.size() || i >= b.element_tokens.size()) {
            total += missing_element_weight;
            cost += missing_element_weight;
            continue;
        }
        const std::string& left = a.element_tokens[i];
        const std::string& right = b.element_tokens[i];
        const std::string left_shape = left.size() >= 2 ? left.substr(0, 2) : left;
        const std::string right_shape = right.size() >= 2 ? right.substr(0, 2) : right;
        add_string_match(left_shape, right_shape, shape_weight);
        add_char_match(
            left.size() > 2 ? left[2] : 'w',
            right.size() > 2 ? right[2] : 'w',
            element_power_weight);
        add_char_match(
            left.size() > 3 ? left[3] : 'w',
            right.size() > 3 ? right[3] : 'w',
            surface_weight);
        add_char_match(
            left.size() > 4 ? left[4] : 'w',
            right.size() > 4 ? right[4] : 'w',
            surface_weight);
    }
    if (total <= 0.0 || !std::isfinite(total)) {
        return 0.0;
    }
    return std::clamp(1.0 - cost / total, 0.0, 1.0);
}

double weighted_structural_family_similarity_parsed(
    const ParsedStructuralTopologyKey& a,
    const ParsedStructuralTopologyKey& b) {
    constexpr double element_count_weight = 4.0;
    constexpr double stop_weight = 3.0;
    constexpr double front_rear_power_weight = 0.4;
    constexpr double shape_weight = 0.4;
    constexpr double shape_mismatch_cost = 0.5;
    constexpr double element_power_weight = 0.6;
    constexpr double surface_weight = 3.0;
    constexpr double missing_element_weight = 8.0;
    constexpr double weak_surface_cost = 0.7;

    double cost = 0.0;
    double total = 0.0;
    auto add_string_match = [&](const std::string& left,
                                const std::string& right,
                                double weight,
                                double mismatch_cost = 1.0) {
        total += weight;
        if (left != right) {
            cost += weight * mismatch_cost;
        }
    };
    auto add_char_match = [&](char left, char right, double weight) {
        total += weight;
        cost += weight * topology_similarity_char_cost(left, right, weak_surface_cost);
    };

    add_string_match(a.element_count, b.element_count, element_count_weight);
    add_string_match(a.stop_after_elements, b.stop_after_elements, stop_weight);
    add_char_match(
        a.front_power_sign.empty() ? 'w' : a.front_power_sign[0],
        b.front_power_sign.empty() ? 'w' : b.front_power_sign[0],
        front_rear_power_weight);
    add_char_match(
        a.rear_power_sign.empty() ? 'w' : a.rear_power_sign[0],
        b.rear_power_sign.empty() ? 'w' : b.rear_power_sign[0],
        front_rear_power_weight);

    const std::size_t count =
        std::max(a.element_tokens.size(), b.element_tokens.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (i >= a.element_tokens.size() || i >= b.element_tokens.size()) {
            total += missing_element_weight;
            cost += missing_element_weight;
            continue;
        }
        const std::string& left = a.element_tokens[i];
        const std::string& right = b.element_tokens[i];
        const std::string left_shape = left.size() >= 2 ? left.substr(0, 2) : left;
        const std::string right_shape = right.size() >= 2 ? right.substr(0, 2) : right;
        add_string_match(left_shape, right_shape, shape_weight, shape_mismatch_cost);
        add_char_match(
            left.size() > 2 ? left[2] : 'w',
            right.size() > 2 ? right[2] : 'w',
            element_power_weight);
        add_char_match(
            left.size() > 3 ? left[3] : 'w',
            right.size() > 3 ? right[3] : 'w',
            surface_weight);
        add_char_match(
            left.size() > 4 ? left[4] : 'w',
            right.size() > 4 ? right[4] : 'w',
            surface_weight);
    }
    if (total <= 0.0 || !std::isfinite(total)) {
        return 0.0;
    }
    return std::clamp(1.0 - cost / total, 0.0, 1.0);
}

double weighted_structural_family_similarity(const std::string& a_key,
                                             const std::string& b_key) {
    if (a_key == b_key) {
        return 1.0;
    }
    if (a_key.empty() || b_key.empty()) {
        return 0.0;
    }
    return weighted_structural_family_similarity_parsed(
        parse_structural_topology_key(a_key),
        parse_structural_topology_key(b_key));
}

}  // namespace optimizer_detail

}  // namespace optiland
