#pragma once
#include "support_types.hpp"

namespace optiland { namespace optimizer_detail {

double surface_curvature_value(const Surface& surface);

double topology_power_normalizer_length(const LensSystem& system, double target_efl);

char signed_power_bucket(
    double power,
    double normalizer_length,
    double normalized_weak = kTopologyWeakElementPower);

char surface_shape_bucket(
    double curvature,
    double surface_power,
    double normalizer_length);

char ratio_bucket(double value, double a, double b, double c);

char glass_index_bucket(double nd);

char glass_abbe_bucket(double vd);

bool topology_descriptor_enabled(TopologyDiversityMode mode);

bool topology_descriptor_selects(TopologyDiversityMode mode);

bool topology_descriptor_full(TopologyDiversityMode mode);

bool topology_similarity_mode(TopologyDiversityMode mode);

bool topology_family_similarity_mode(TopologyDiversityMode mode);

const char* topology_diversity_mode_name_internal(TopologyDiversityMode mode);

std::uint64_t hash_combine_u64(std::uint64_t seed, std::uint64_t value);

std::uint64_t hash_char_token(std::uint64_t seed, char value);

std::uint64_t hash_indexed_token(std::uint64_t seed, int index, char value);

std::string hex_hash(std::uint64_t value);

char descriptor_surface_sign(double surface_power, double ttl, double tau_low, double tau_high);

char descriptor_power_sign(double power, double abs_power_sum);

char descriptor_share_bucket(double share);

char descriptor_power_mag(double abs_share);

char descriptor_bending_bucket(double phi1, double phi2);

char descriptor_centroid_bucket(double centroid01, bool flat);

char descriptor_alternation_bucket(int alternation);

OpticalTopologyDescriptor build_optical_topology_descriptor(
    const LensSystem& system,
    const TopologyDescriptorOptions& options);

std::string optical_topology_key_impl(
    const LensSystem& system,
    bool structural_only,
    double target_efl = 0.0);

std::string optical_topology_key(const LensSystem& system);

std::string optical_topology_key(const LensSystem& system, double target_efl);

std::string optical_semantic_key(const LensSystem& system);

std::string optical_semantic_key(const LensSystem& system, double target_efl);

} }  // namespace optiland::optimizer_detail
