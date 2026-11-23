#pragma once

#include "optiland/core.hpp"

#include <string>
#include <vector>

namespace optiland {

bool save_system_json(const LensSystem& system, const std::string& path);
bool save_score_json(const Score& score, const std::string& path);
bool load_system_json(const std::string& path, LensSystem* system, std::string* error = nullptr);
bool load_optiland_json(const std::string& path, LensSystem* system, std::string* error = nullptr);
bool load_variables_json(const std::string& path, std::vector<VariableSpec>* variables, std::string* error = nullptr);
bool load_merit_json(const std::string& path, MeritConfig* config, std::string* error = nullptr);
bool load_glass_search_config(
    const std::string& path,
    const std::string& database_root,
    GlassSearchContext* context,
    std::string* error = nullptr,
    const std::vector<double>& required_wavelengths_um = {});
bool validate_fixed_material_wavelength_coverage(
    const LensSystem& system,
    const std::vector<VariableSpec>& variables,
    std::string* error = nullptr);
void build_glass_neighbor_indices(GlassSearchContext& context);
bool load_glass_from_catalog(
    const std::string& database_root,
    const std::string& name,
    const std::string& reference,
    Material* material,
    std::string* error = nullptr);

}  // namespace optiland
