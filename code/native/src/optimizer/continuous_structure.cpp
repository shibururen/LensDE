/*
 * Encode continuous optical structure signatures and compare their geometry/parameter components.
 * Topology avoidance and archive guidance share these signatures across optimizer sessions.
 * Preserve component order, normalization and serialized key syntax so stored identities remain comparable.
 */
#include "support.hpp"

namespace optiland {
using namespace optimizer_detail;

namespace optimizer_detail {

void append_continuous_values(
    std::ostringstream& out,
    char field,
    const std::vector<double>& values) {
    out << '|' << field;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ';';
        }
        out << values[i];
    }
}

std::string optical_continuous_structure_signature(
    const LensSystem& system,
    double target_efl) {
    const double wavelength = system.primary_wavelength();
    const double ttl = std::max(1.0e-9, std::abs(system.total_track()));
    const double power_normalizer =
        topology_power_normalizer_length(system, target_efl);
    const std::size_t first = system.first_optical_surface();
    ContinuousStructureSignature signature;
    int glass_spaces_seen = 0;
    for (std::size_t i = first; i < system.surfaces.size(); ++i) {
        const Surface& surface = system.surfaces[i];
        if (surface.kind == SurfaceKind::Image) {
            break;
        }
        if (surface.is_stop && signature.stop_after_elements < 0) {
            signature.stop_after_elements = glass_spaces_seen;
        }
        const double n_glass = surface.material_after.refractive_index(wavelength);
        const bool glass_after = n_glass > 1.0005;
        if (!glass_after || i + 1 >= system.surfaces.size()) {
            continue;
        }
        const Surface& next_surface = system.surfaces[i + 1];
        const double n_before =
            i == 0 ? 1.0
                   : system.surfaces[i - 1].material_after.refractive_index(wavelength);
        const double n_after = next_surface.material_after.refractive_index(wavelength);
        const double c1 = surface_curvature_value(surface);
        const double c2 = surface_curvature_value(next_surface);
        const double phi1 = (n_glass - n_before) * c1;
        const double phi2 = (n_after - n_glass) * c2;
        const double thickness_in_glass =
            std::isfinite(surface.thickness) ? std::max(0.0, surface.thickness) : 0.0;
        const double power =
            phi1 + phi2 - thickness_in_glass / n_glass * phi1 * phi2;
        signature.element_powers.push_back(power * power_normalizer);
        signature.surface_curvatures.push_back(c1 * power_normalizer);
        signature.surface_curvatures.push_back(c2 * power_normalizer);
        signature.glass_thicknesses.push_back(thickness_in_glass / ttl);
        const double gap_after =
            std::isfinite(next_surface.thickness)
                ? std::max(0.0, next_surface.thickness)
                : 0.0;
        signature.air_gaps.push_back(gap_after / ttl);
        ++glass_spaces_seen;
    }
    signature.element_count = glass_spaces_seen;
    if (signature.element_count <= 0) {
        return {};
    }
    std::ostringstream out;
    out << std::setprecision(10)
        << "c1|e" << signature.element_count
        << "|s" << signature.stop_after_elements;
    append_continuous_values(out, 'p', signature.element_powers);
    append_continuous_values(out, 'c', signature.surface_curvatures);
    append_continuous_values(out, 't', signature.glass_thicknesses);
    append_continuous_values(out, 'g', signature.air_gaps);
    return out.str();
}

bool parse_continuous_values(
    const std::string& text,
    std::vector<double>* values) {
    if (values == nullptr) {
        return false;
    }
    values->clear();
    if (text.empty()) {
        return true;
    }
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(';', start);
        const std::string token =
            end == std::string::npos ? text.substr(start) : text.substr(start, end - start);
        if (token.empty()) {
            return false;
        }
        try {
            std::size_t consumed = 0;
            const double value = std::stod(token, &consumed);
            if (consumed != token.size() || !std::isfinite(value)) {
                return false;
            }
            values->push_back(value);
        } catch (const std::exception&) {
            return false;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return true;
}

bool parse_continuous_structure_signature(
    const std::string& key,
    ContinuousStructureSignature* signature) {
    if (signature == nullptr || key.rfind("c1|", 0) != 0) {
        return false;
    }
    *signature = ContinuousStructureSignature{};
    bool have_elements = false;
    bool have_stop = false;
    bool have_powers = false;
    bool have_curvatures = false;
    bool have_thicknesses = false;
    bool have_gaps = false;
    std::size_t start = 3;
    while (start <= key.size()) {
        const std::size_t end = key.find('|', start);
        const std::string part =
            end == std::string::npos ? key.substr(start) : key.substr(start, end - start);
        if (part.size() < 2) {
            return false;
        }
        try {
            switch (part[0]) {
                case 'e':
                    signature->element_count = std::stoi(part.substr(1));
                    have_elements = true;
                    break;
                case 's':
                    signature->stop_after_elements = std::stoi(part.substr(1));
                    have_stop = true;
                    break;
                case 'p':
                    have_powers = parse_continuous_values(
                        part.substr(1), &signature->element_powers);
                    break;
                case 'c':
                    have_curvatures = parse_continuous_values(
                        part.substr(1), &signature->surface_curvatures);
                    break;
                case 't':
                    have_thicknesses = parse_continuous_values(
                        part.substr(1), &signature->glass_thicknesses);
                    break;
                case 'g':
                    have_gaps = parse_continuous_values(
                        part.substr(1), &signature->air_gaps);
                    break;
                default:
                    return false;
            }
        } catch (const std::exception&) {
            return false;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    const std::size_t count = static_cast<std::size_t>(
        std::max(0, signature->element_count));
    return have_elements && have_stop && have_powers && have_curvatures &&
           have_thicknesses && have_gaps && count > 0 &&
           signature->element_powers.size() == count &&
           signature->surface_curvatures.size() == count * 2U &&
           signature->glass_thicknesses.size() == count &&
           signature->air_gaps.size() == count;
}

double continuous_vector_norm(const std::vector<double>& values) {
    double sum = 0.0;
    for (const double value : values) {
        sum += value * value;
    }
    return std::sqrt(std::max(0.0, sum));
}

double continuous_vector_cosine(
    const std::vector<double>& left,
    const std::vector<double>& right) {
    if (left.size() != right.size() || left.empty()) {
        return 0.0;
    }
    double dot = 0.0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        dot += left[i] * right[i];
    }
    const double left_norm = continuous_vector_norm(left);
    const double right_norm = continuous_vector_norm(right);
    if (left_norm <= 1.0e-12 || right_norm <= 1.0e-12) {
        return left_norm <= 1.0e-12 && right_norm <= 1.0e-12 ? 1.0 : 0.0;
    }
    return std::clamp(dot / (left_norm * right_norm), -1.0, 1.0);
}

double continuous_vector_normalized_distance(
    const std::vector<double>& left,
    const std::vector<double>& right) {
    if (left.size() != right.size() || left.empty()) {
        return std::numeric_limits<double>::infinity();
    }
    double squared = 0.0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        const double delta = left[i] - right[i];
        squared += delta * delta;
    }
    const double scale = std::max({
        continuous_vector_norm(left), continuous_vector_norm(right), 1.0e-12});
    return std::sqrt(std::max(0.0, squared)) / scale;
}

bool continuous_sign_skeleton_compatible(
    const std::vector<double>& left,
    const std::vector<double>& right,
    double significance_fraction) {
    if (left.size() != right.size() || left.empty()) {
        return false;
    }
    const double sample_count = static_cast<double>(left.size());
    const double rms_scale = std::max(
        continuous_vector_norm(left), continuous_vector_norm(right)) /
        std::sqrt(sample_count);
    const double significance_floor =
        std::max(1.0e-10, std::max(0.0, significance_fraction) * rms_scale);
    for (std::size_t i = 0; i < left.size(); ++i) {
        // Weak surfaces/groups do not veto a continuous match merely because
        // an almost-zero value crossed a sign/bucket boundary.
        if (std::abs(left[i]) <= significance_floor ||
            std::abs(right[i]) <= significance_floor) {
            continue;
        }
        if (std::signbit(left[i]) != std::signbit(right[i])) {
            return false;
        }
    }
    return true;
}

double continuous_distance_similarity(
    const std::vector<double>& left,
    const std::vector<double>& right) {
    return std::clamp(
        1.0 - continuous_vector_normalized_distance(left, right),
        0.0,
        1.0);
}

ContinuousAvoidMatch continuous_parameter_auxiliary_match(
    const ContinuousStructureSignature& left,
    const ContinuousStructureSignature& right,
    const AdaptiveHybridDeDlsOptions& options) {
    if (left.element_count != right.element_count ||
        left.stop_after_elements != right.stop_after_elements) {
        return {};
    }

    // A strong curvature/power sign reversal is an explicit structural
    // difference. Weak near-zero sign changes are ignored by the significance
    // rule, but sign agreement is only strong evidence, never a direct match.
    if (!continuous_sign_skeleton_compatible(
            left.surface_curvatures,
            right.surface_curvatures,
            options.avoided_topology_continuous_significance_fraction) ||
        !continuous_sign_skeleton_compatible(
            left.element_powers,
            right.element_powers,
            options.avoided_topology_continuous_significance_fraction)) {
        return {};
    }

    // One auditable score replaces the former six-way hard conjunction.
    // Curvature-sign compatibility carries half of the evidence, so it is a
    // strong warning but cannot trigger avoidance without parameter support.
    const double curvature_similarity = std::clamp(
        continuous_vector_cosine(
            left.surface_curvatures, right.surface_curvatures),
        0.0,
        1.0);
    const double power_similarity = std::clamp(
        continuous_vector_cosine(left.element_powers, right.element_powers),
        0.0,
        1.0);
    const double thickness_similarity = continuous_distance_similarity(
        left.glass_thicknesses, right.glass_thicknesses);
    const double gap_similarity =
        continuous_distance_similarity(left.air_gaps, right.air_gaps);
    const double score =
        0.50 +
        0.20 * curvature_similarity +
        0.15 * power_similarity +
        0.075 * thickness_similarity +
        0.075 * gap_similarity;
    return {
        score >=
            options.avoided_topology_parameter_auxiliary_similarity_min,
        score,
    };
}

}  // namespace optimizer_detail

}  // namespace optiland
