/*
 * Build optical element/window descriptors and their stable topology/semantic keys.
 * Adaptive archive diversity and avoidance derive these descriptors from the scored lens system.
 * Preserve surface traversal, bucket thresholds and hash-token order because they define candidate identity.
 */
#include "support.hpp"

namespace optiland {
using namespace optimizer_detail;

namespace optimizer_detail {

double surface_curvature_value(const Surface& surface) {
    if (!std::isfinite(surface.radius) || std::abs(surface.radius) < kEps) {
        return 0.0;
    }
    return 1.0 / surface.radius;
}

double topology_power_normalizer_length(const LensSystem& system, double target_efl) {
    if (std::isfinite(target_efl) && std::abs(target_efl) > kEps) {
        return std::abs(target_efl);
    }
    return std::max(1.0e-9, std::abs(system.total_track()));
}

char signed_power_bucket(
    double power,
    double normalizer_length,
    double normalized_weak ) {
    if (!std::isfinite(power)) {
        return 'w';
    }
    const double normalized_power =
        std::abs(power) * std::max(1.0e-9, std::abs(normalizer_length));
    if (normalized_power <= normalized_weak) {
        return 'w';
    }
    return power > 0.0 ? 'p' : 'n';
}

char surface_shape_bucket(
    double curvature,
    double surface_power,
    double normalizer_length) {
    if (!std::isfinite(surface_power)) {
        return 'w';
    }
    const double normalized_power =
        std::abs(surface_power) * std::max(1.0e-9, std::abs(normalizer_length));
    if (normalized_power <= kTopologyWeakSurfacePower ||
        !std::isfinite(curvature) ||
        std::abs(curvature) <= kEps) {
        return 'w';
    }
    return curvature > 0.0 ? 'p' : 'n';
}

char ratio_bucket(double value, double a, double b, double c) {
    if (!std::isfinite(value)) {
        return 'x';
    }
    if (value < a) {
        return 'a';
    }
    if (value < b) {
        return 'b';
    }
    if (value < c) {
        return 'c';
    }
    return 'd';
}

char glass_index_bucket(double nd) {
    if (!std::isfinite(nd)) {
        return 'x';
    }
    if (nd < 1.55) {
        return 'l';
    }
    if (nd < 1.70) {
        return 'm';
    }
    return 'h';
}

char glass_abbe_bucket(double vd) {
    if (!std::isfinite(vd) || vd <= 0.0) {
        return 'x';
    }
    if (vd < 40.0) {
        return 'f';
    }
    if (vd < 55.0) {
        return 'm';
    }
    return 'c';
}

bool topology_descriptor_enabled(TopologyDiversityMode mode) {
    return mode != TopologyDiversityMode::Legacy;
}

bool topology_descriptor_selects(TopologyDiversityMode mode) {
    return mode == TopologyDiversityMode::DescriptorSelect ||
           mode == TopologyDiversityMode::DescriptorFull ||
           mode == TopologyDiversityMode::TopologySimilarity ||
           mode == TopologyDiversityMode::TopologyFamilySimilarity;
}

bool topology_descriptor_full(TopologyDiversityMode mode) {
    return mode == TopologyDiversityMode::DescriptorFull;
}

bool topology_similarity_mode(TopologyDiversityMode mode) {
    return mode == TopologyDiversityMode::TopologySimilarity ||
           mode == TopologyDiversityMode::TopologyFamilySimilarity;
}

bool topology_family_similarity_mode(TopologyDiversityMode mode) {
    return mode == TopologyDiversityMode::TopologyFamilySimilarity;
}

const char* topology_diversity_mode_name_internal(TopologyDiversityMode mode) {
    switch (mode) {
        case TopologyDiversityMode::DescriptorShadow:
            return "descriptor_shadow";
        case TopologyDiversityMode::DescriptorAudit:
            return "descriptor_audit";
        case TopologyDiversityMode::DescriptorSelect:
            return "descriptor_select";
        case TopologyDiversityMode::DescriptorFull:
            return "descriptor_full";
        case TopologyDiversityMode::TopologySimilarity:
            return "topology_similarity";
        case TopologyDiversityMode::TopologyFamilySimilarity:
            return "topology_family_similarity";
        case TopologyDiversityMode::Legacy:
            return "legacy";
    }
    return "legacy";
}

std::uint64_t hash_combine_u64(std::uint64_t seed, std::uint64_t value) {
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
}

std::uint64_t hash_char_token(std::uint64_t seed, char value) {
    return hash_combine_u64(seed, static_cast<unsigned char>(value));
}

std::uint64_t hash_indexed_token(std::uint64_t seed, int index, char value) {
    seed = hash_combine_u64(seed, static_cast<std::uint64_t>(std::max(0, index)));
    return hash_char_token(seed, value);
}

std::string hex_hash(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << value;
    return out.str();
}

char descriptor_surface_sign(double surface_power, double ttl, double tau_low, double tau_high) {
    if (!std::isfinite(surface_power)) {
        return 'z';
    }
    const double normalized = surface_power * std::max(1.0e-9, std::abs(ttl));
    const double magnitude = std::abs(normalized);
    const double low = std::max(0.0, tau_low);
    const double high = std::max(low, tau_high);
    if (magnitude <= low) {
        return 'z';
    }
    if (magnitude <= high) {
        return normalized >= 0.0 ? 'u' : 'v';
    }
    return normalized > 0.0 ? 'p' : 'n';
}

char descriptor_power_sign(double power, double abs_power_sum) {
    if (!std::isfinite(power) || abs_power_sum <= 0.0) {
        return 'w';
    }
    const double share = std::abs(power) / std::max(1.0e-12, abs_power_sum);
    if (share < 0.03) {
        return 'w';
    }
    return power > 0.0 ? 'p' : 'n';
}

char descriptor_share_bucket(double share) {
    if (!std::isfinite(share) || share < 0.0) {
        return 'x';
    }
    if (share < 0.03) {
        return '0';
    }
    if (share < 0.10) {
        return '1';
    }
    if (share < 0.25) {
        return '2';
    }
    if (share < 0.50) {
        return '3';
    }
    return '4';
}

char descriptor_power_mag(double abs_share) {
    if (!std::isfinite(abs_share) || abs_share < 0.0) {
        return 'x';
    }
    if (abs_share < 0.03) {
        return 'w';
    }
    if (abs_share < 0.10) {
        return 'l';
    }
    if (abs_share < 0.25) {
        return 'm';
    }
    return 'h';
}

char descriptor_bending_bucket(double phi1, double phi2) {
    const double total = std::abs(phi1) + std::abs(phi2);
    if (!std::isfinite(total) || total <= 1.0e-12) {
        return 'x';
    }
    const double front = std::abs(phi1) / total;
    if (front < 0.35) {
        return 'r';
    }
    if (front > 0.65) {
        return 'f';
    }
    return 'e';
}

char descriptor_centroid_bucket(double centroid01, bool flat) {
    if (flat || !std::isfinite(centroid01)) {
        return 'f';
    }
    if (centroid01 < 0.35) {
        return 'l';
    }
    if (centroid01 > 0.65) {
        return 'r';
    }
    return 'c';
}

char descriptor_alternation_bucket(int alternation) {
    if (alternation <= 0) {
        return '0';
    }
    if (alternation == 1) {
        return '1';
    }
    if (alternation == 2) {
        return '2';
    }
    return 'm';
}

OpticalTopologyDescriptor build_optical_topology_descriptor(
    const LensSystem& system,
    const TopologyDescriptorOptions& options) {
    const double wavelength = system.primary_wavelength();
    const double ttl = std::max(1.0e-9, std::abs(system.total_track()));
    const std::size_t first = system.first_optical_surface();
    const int lmin = std::max(1, options.window_lmin);
    const int lmax = std::max(lmin, options.window_lmax);
    OpticalTopologyDescriptor desc;
    std::vector<double> powers;
    std::vector<double> air_gaps;
    const std::size_t surface_capacity = system.surfaces.size() -
                                         std::min(first, system.surfaces.size());
    desc.elements.reserve(surface_capacity);
    powers.reserve(surface_capacity);
    air_gaps.reserve(surface_capacity);
    double bfl_gap = 0.0;
    int glass_spaces_seen = 0;

    for (std::size_t i = first; i < system.surfaces.size(); ++i) {
        const Surface& surface = system.surfaces[i];
        if (surface.kind == SurfaceKind::Image) {
            break;
        }
        if (surface.is_stop && desc.stop_after_element < 0) {
            desc.stop_after_element = glass_spaces_seen;
        }
        const double n_glass = surface.material_after.refractive_index(wavelength);
        const bool glass_after = n_glass > 1.0005;
        if (glass_after && i + 1 < system.surfaces.size()) {
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
            const double power = phi1 + phi2 - thickness_in_glass / n_glass * phi1 * phi2;

            OpticalElementDescriptor element;
            element.s1 = descriptor_surface_sign(
                phi1,
                ttl,
                options.weak_tau_low,
                options.weak_tau_high);
            element.s2 = descriptor_surface_sign(
                phi2,
                ttl,
                options.weak_tau_low,
                options.weak_tau_high);
            element.bending = descriptor_bending_bucket(phi1, phi2);
            element.material_nd =
                glass_index_bucket(surface.material_after.nd > 0.0 ? surface.material_after.nd : n_glass);
            element.material_vd = glass_abbe_bucket(surface.material_after.vd);
            element.thickness = ratio_bucket(thickness_in_glass / ttl, 0.03, 0.10, 0.20);
            element.power = power;
            element.abs_power = std::abs(power);
            desc.elements.push_back(element);
            powers.push_back(power);
            ++glass_spaces_seen;
        } else if (std::isfinite(surface.thickness) && surface.thickness > 0.0) {
            const double gap = std::max(0.0, surface.thickness);
            const bool next_is_image =
                i + 1 < system.surfaces.size() && system.surfaces[i + 1].kind == SurfaceKind::Image;
            if (next_is_image) {
                bfl_gap = gap;
            } else {
                air_gaps.push_back(gap);
            }
        }
    }

    desc.element_count = static_cast<int>(desc.elements.size());
    double abs_power_sum = 0.0;
    double positive_abs = 0.0;
    double negative_abs = 0.0;
    for (double power : powers) {
        const double abs_power = std::abs(power);
        abs_power_sum += abs_power;
        if (power > 0.0) {
            positive_abs += abs_power;
        } else if (power < 0.0) {
            negative_abs += abs_power;
        }
    }
    abs_power_sum = std::max(1.0e-12, abs_power_sum);

    std::uint64_t global_sign_hash = 0x84222325cbf29ce4ULL;
    std::uint64_t global_mag_hash = 0x9e3779b97f4a7c15ULL;
    double global_centroid_sum = 0.0;
    int global_alternation = 0;
    char previous_power_sign = '\0';
    for (int i = 0; i < desc.element_count; ++i) {
        OpticalElementDescriptor& element = desc.elements[static_cast<std::size_t>(i)];
        const double share = element.abs_power / abs_power_sum;
        element.power_sign = descriptor_power_sign(element.power, abs_power_sum);
        element.power_mag = descriptor_power_mag(share);
        element.abs_share = descriptor_share_bucket(share);

        std::uint64_t shape_hash = 0x100000001b3ULL;
        shape_hash = hash_char_token(shape_hash, element.s1);
        shape_hash = hash_char_token(shape_hash, element.s2);
        shape_hash = hash_char_token(shape_hash, element.bending);
        element.shape_hash = shape_hash;

        std::uint64_t power_hash = 0x517cc1b727220a95ULL;
        power_hash = hash_char_token(power_hash, element.power_sign);
        power_hash = hash_char_token(power_hash, element.power_mag);
        power_hash = hash_char_token(power_hash, element.abs_share);
        element.power_hash = power_hash;

        element.shape_power_hash = hash_combine_u64(element.shape_hash, element.power_hash);
        std::uint64_t semantic_hash = element.shape_power_hash;
        semantic_hash = hash_char_token(semantic_hash, element.material_nd);
        semantic_hash = hash_char_token(semantic_hash, element.material_vd);
        semantic_hash = hash_char_token(semantic_hash, element.thickness);
        element.semantic_hash = semantic_hash;

        global_sign_hash = hash_indexed_token(global_sign_hash, i, element.power_sign);
        global_mag_hash = hash_indexed_token(global_mag_hash, i, element.power_mag);
        global_centroid_sum += static_cast<double>(i) * element.abs_power;
        if (previous_power_sign != '\0' &&
            previous_power_sign != 'w' &&
            element.power_sign != 'w' &&
            previous_power_sign != element.power_sign) {
            ++global_alternation;
        }
        if (element.power_sign != 'w') {
            previous_power_sign = element.power_sign;
        }
    }

    const double global_centroid01 =
        desc.element_count > 1 ? global_centroid_sum / abs_power_sum /
                                     static_cast<double>(desc.element_count - 1)
                               : 0.5;
    desc.global_power_hash = global_sign_hash;
    desc.global_power_hash = hash_combine_u64(desc.global_power_hash, global_mag_hash);
    desc.global_power_hash =
        hash_char_token(desc.global_power_hash,
                        descriptor_share_bucket(positive_abs / abs_power_sum));
    desc.global_power_hash =
        hash_char_token(desc.global_power_hash,
                        descriptor_share_bucket(negative_abs / abs_power_sum));
    desc.global_power_hash =
        hash_char_token(desc.global_power_hash,
                        descriptor_centroid_bucket(global_centroid01, desc.element_count <= 1));
    desc.global_power_hash =
        hash_char_token(desc.global_power_hash, descriptor_alternation_bucket(global_alternation));

    double air_sum = 0.0;
    double max_air = 0.0;
    for (double gap : air_gaps) {
        air_sum += gap;
        max_air = std::max(max_air, gap);
    }
    const double max_air_share = air_sum > 1.0e-12 ? max_air / air_sum : 0.0;
    std::uint64_t spacing_hash = 0xd6e8feb86659fd93ULL;
    spacing_hash = hash_combine_u64(spacing_hash, static_cast<std::uint64_t>(air_gaps.size()));
    spacing_hash = hash_char_token(spacing_hash, ratio_bucket(air_sum / ttl, 0.10, 0.35, 0.65));
    spacing_hash = hash_char_token(spacing_hash, ratio_bucket(max_air / ttl, 0.04, 0.12, 0.28));
    spacing_hash = hash_char_token(spacing_hash, ratio_bucket(max_air_share, 0.25, 0.45, 0.70));
    spacing_hash = hash_char_token(spacing_hash, ratio_bucket(bfl_gap / ttl, 0.08, 0.20, 0.40));
    for (std::size_t i = 0; i < air_gaps.size(); ++i) {
        spacing_hash = hash_indexed_token(
            spacing_hash,
            static_cast<int>(i),
            ratio_bucket(air_gaps[i] / ttl, 0.04, 0.12, 0.28));
    }
    desc.spacing_hash = spacing_hash;

    std::size_t window_count = 0;
    for (int length = lmin; length <= lmax && length <= desc.element_count; ++length) {
        window_count += static_cast<std::size_t>(desc.element_count - length + 1);
    }
    desc.windows.reserve(window_count);
    for (int length = lmin; length <= lmax && length <= desc.element_count; ++length) {
        for (int start = 0; start + length <= desc.element_count; ++start) {
            OpticalWindowDescriptor window;
            window.start = start;
            window.length = length;
            std::uint64_t shape_hash = hash_combine_u64(0x9ae16a3b2f90404fULL, length);
            std::uint64_t power_hash = hash_combine_u64(0xc3a5c85c97cb3127ULL, length);
            double window_power = 0.0;
            double window_abs_power = 0.0;
            double centroid_sum = 0.0;
            int alternation = 0;
            char previous = '\0';
            for (int offset = 0; offset < length; ++offset) {
                const int index = start + offset;
                const OpticalElementDescriptor& element =
                    desc.elements[static_cast<std::size_t>(index)];
                shape_hash = hash_indexed_token(shape_hash, offset, static_cast<char>(
                    element.shape_hash & 0xffU));
                shape_hash = hash_combine_u64(shape_hash, element.shape_hash);
                power_hash = hash_indexed_token(power_hash, offset, static_cast<char>(
                    element.power_hash & 0xffU));
                power_hash = hash_combine_u64(power_hash, element.power_hash);
                window_power += element.power;
                window_abs_power += element.abs_power;
                centroid_sum += static_cast<double>(offset) * element.abs_power;
                if (previous != '\0' &&
                    previous != 'w' &&
                    element.power_sign != 'w' &&
                    previous != element.power_sign) {
                    ++alternation;
                }
                if (element.power_sign != 'w') {
                    previous = element.power_sign;
                }
            }
            window.shape_seq_hash = shape_hash;
            window.power_seq_hash = power_hash;
            window.shape_power_hash = hash_combine_u64(shape_hash, power_hash);
            window.net_power_sign = descriptor_power_sign(window_power, abs_power_sum);
            window.abs_share = descriptor_share_bucket(window_abs_power / abs_power_sum);
            const bool flat = window_abs_power <= 1.0e-12 || length <= 1;
            const double centroid01 = flat ? 0.5 : centroid_sum / window_abs_power /
                                                 static_cast<double>(length - 1);
            window.centroid = descriptor_centroid_bucket(centroid01, flat);
            window.alternation = descriptor_alternation_bucket(alternation);
            window.shape_power_hash = hash_char_token(window.shape_power_hash, window.net_power_sign);
            window.shape_power_hash = hash_char_token(window.shape_power_hash, window.abs_share);
            window.shape_power_hash = hash_char_token(window.shape_power_hash, window.centroid);
            window.shape_power_hash = hash_char_token(window.shape_power_hash, window.alternation);
            desc.windows.push_back(window);
        }
    }

    desc.axis_values.reserve(
        2U + static_cast<std::size_t>(desc.element_count) + desc.windows.size());
    desc.axis_values.push_back(desc.global_power_hash);
    desc.axis_values.push_back(desc.spacing_hash);
    for (const OpticalElementDescriptor& element : desc.elements) {
        desc.axis_values.push_back(element.shape_power_hash);
    }
    for (const OpticalWindowDescriptor& window : desc.windows) {
        desc.axis_values.push_back(window.shape_power_hash);
    }

    std::uint64_t diagnostic_hash = 0x27d4eb2f165667c5ULL;
    diagnostic_hash = hash_combine_u64(diagnostic_hash, static_cast<std::uint64_t>(desc.element_count));
    diagnostic_hash = hash_combine_u64(
        diagnostic_hash,
        static_cast<std::uint64_t>(std::max(0, desc.stop_after_element + 1)));
    diagnostic_hash = hash_combine_u64(diagnostic_hash, desc.global_power_hash);
    diagnostic_hash = hash_combine_u64(diagnostic_hash, desc.spacing_hash);
    for (const OpticalElementDescriptor& element : desc.elements) {
        diagnostic_hash = hash_combine_u64(diagnostic_hash, element.semantic_hash);
    }
    for (const OpticalWindowDescriptor& window : desc.windows) {
        diagnostic_hash = hash_combine_u64(diagnostic_hash, window.shape_power_hash);
    }
    desc.diagnostic_hash = diagnostic_hash;
    desc.key = "d" + std::to_string(desc.element_count) + "|h" + hex_hash(desc.diagnostic_hash);

    if (!options.lazy_diagnostic_strings) {
        std::ostringstream diag;
        diag << "e" << desc.element_count
             << "|s" << desc.stop_after_element
             << "|g" << hex_hash(desc.global_power_hash)
             << "|a" << hex_hash(desc.spacing_hash)
             << "|l";
        for (const OpticalElementDescriptor& element : desc.elements) {
            diag << element.s1 << element.s2 << element.power_sign
                 << element.power_mag << element.abs_share << element.bending << '.';
        }
        diag << "|w";
        for (const OpticalWindowDescriptor& window : desc.windows) {
            diag << window.start << "_" << window.length << ":"
                 << hex_hash(window.shape_power_hash) << '.';
        }
        desc.diagnostic = diag.str();
    }
    return desc;
}

std::string optical_topology_key_impl(
    const LensSystem& system,
    bool structural_only,
    double target_efl ) {
    const double wavelength = system.primary_wavelength();
    const double ttl = std::max(1.0e-9, std::abs(system.total_track()));
    const double power_normalizer =
        topology_power_normalizer_length(system, target_efl);
    const std::size_t first = system.first_optical_surface();
    std::vector<std::string> element_tokens;
    std::vector<char> air_gap_tokens;
    std::vector<double> element_powers;
    const std::size_t surface_capacity = system.surfaces.size() -
                                         std::min(first, system.surfaces.size());
    element_tokens.reserve(surface_capacity);
    air_gap_tokens.reserve(surface_capacity);
    element_powers.reserve(surface_capacity);
    int stop_after_elements = -1;
    int glass_spaces_seen = 0;

    for (std::size_t i = first; i < system.surfaces.size(); ++i) {
        const Surface& surface = system.surfaces[i];
        if (surface.kind == SurfaceKind::Image) {
            break;
        }
        if (surface.is_stop && stop_after_elements < 0) {
            stop_after_elements = glass_spaces_seen;
        }
        const double n_glass = surface.material_after.refractive_index(wavelength);
        const bool glass_after = n_glass > 1.0005;
        if (glass_after && i + 1 < system.surfaces.size()) {
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
            const double power = phi1 + phi2 - thickness_in_glass / n_glass * phi1 * phi2;
            const char s1 = surface_shape_bucket(c1, phi1, power_normalizer);
            const char s2 = surface_shape_bucket(c2, phi2, power_normalizer);
            const char power_sign =
                signed_power_bucket(power, power_normalizer, kTopologyWeakElementPower);
            std::string shape = "x";
            if (s1 == 'w' || s2 == 'w') {
                shape = "pl";
            } else if (s1 == 'p' && s2 == 'n') {
                shape = "bv";
            } else if (s1 == 'n' && s2 == 'p') {
                shape = "bc";
            } else if (power_sign == 'p') {
                shape = "mp";
            } else if (power_sign == 'n') {
                shape = "mn";
            } else {
                shape = "mw";
            }
            const char power_mag =
                ratio_bucket(std::abs(power) * power_normalizer, 0.15, 0.6, 1.5);
            const char thickness = ratio_bucket(std::max(0.0, surface.thickness) / ttl, 0.03, 0.10, 0.20);
            const char nd_bucket = glass_index_bucket(surface.material_after.nd > 0.0 ? surface.material_after.nd : n_glass);
            const char vd_bucket = glass_abbe_bucket(surface.material_after.vd);

            std::ostringstream token;
            if (structural_only) {
                token << shape << power_sign << s1 << s2;
            } else {
                token << shape << power_sign << power_mag << s1 << s2 << thickness
                      << nd_bucket << vd_bucket;
            }
            element_tokens.push_back(token.str());
            element_powers.push_back(power);
            ++glass_spaces_seen;
        } else if (!structural_only &&
                   std::isfinite(surface.thickness) &&
                   surface.thickness > 0.0) {
            air_gap_tokens.push_back(
                ratio_bucket(std::max(0.0, surface.thickness) / ttl, 0.04, 0.12, 0.28));
        }
    }

    double front_power = 0.0;
    double rear_power = 0.0;
    const std::size_t split = (element_powers.size() + 1) / 2;
    for (std::size_t i = 0; i < element_powers.size(); ++i) {
        if (i < split) {
            front_power += element_powers[i];
        } else {
            rear_power += element_powers[i];
        }
    }
    const double power_scale = std::abs(front_power) + std::abs(rear_power) + 1.0e-12;
    const char front_share = ratio_bucket(std::abs(front_power) / power_scale, 0.35, 0.55, 0.75);

    std::ostringstream key;
    key << "e" << element_tokens.size()
        << "|s" << stop_after_elements
        << "|fp" << signed_power_bucket(front_power, power_normalizer, kTopologyWeakGroupPower)
        << "|rp" << signed_power_bucket(rear_power, power_normalizer, kTopologyWeakGroupPower);
    if (!structural_only) {
        key << "|fs" << front_share;
    }
    key << "|l";
    for (const auto& token : element_tokens) {
        key << token << '.';
    }
    if (!structural_only) {
        key << "|g";
        for (char gap : air_gap_tokens) {
            key << gap;
        }
    }
    return key.str();
}

std::string optical_topology_key(const LensSystem& system) {
    return optical_topology_key_impl(system, true);
}

std::string optical_topology_key(const LensSystem& system, double target_efl) {
    return optical_topology_key_impl(system, true, target_efl);
}

std::string optical_semantic_key(const LensSystem& system) {
    return optical_topology_key_impl(system, false);
}

std::string optical_semantic_key(const LensSystem& system, double target_efl) {
    return optical_topology_key_impl(system, false, target_efl);
}

}  // namespace optimizer_detail

}  // namespace optiland
