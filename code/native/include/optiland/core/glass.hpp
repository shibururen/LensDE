#pragma once
#include "optiland/core/surfaces.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace optiland {
struct GlassCandidate {
    std::string name;
    std::string reference;
    std::string library;
    Material material;
};

struct GlassChoiceGroup {
    int id = -1;
    std::size_t surface_index = 0;
    std::vector<GlassCandidate> candidates;
    std::vector<std::vector<int>> neighbor_indices;
};

struct GlassSearchContext {
    bool enabled = false;
    std::string database_root;
    double mutation_rate = 0.35;
    double random_rate = 0.05;
    double pbest_inherit_rate = 0.35;
    int neighbor_count = 12;
    bool local_search = true;
    double neighbor_nd_scale = 0.035;
    double neighbor_vd_scale = 7.5;
    std::vector<double> required_wavelengths_um;
    std::size_t wavelength_rejected_candidates = 0;
    std::vector<std::string> wavelength_rejections;
    std::vector<GlassChoiceGroup> groups;
};


}  // namespace optiland
