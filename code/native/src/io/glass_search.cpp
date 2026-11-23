/*
 * Builds glass-choice groups, wavelength filters and nd/Vd-scaled neighbor tables.
 * Native CLI optimizer setup loads these candidate groups before applying glass variables.
 * Candidate order, group IDs, resource resolution and required wavelength coverage
 * must preserve the material indices and mutation neighborhoods seen by DE/DLS.
 */

#include "detail.hpp"

namespace optiland {
using namespace io_detail;

void build_glass_neighbor_indices(GlassSearchContext& context) {
    const double nd_scale = std::max(1.0e-9, context.neighbor_nd_scale);
    const double vd_scale = std::max(1.0e-9, context.neighbor_vd_scale);

    for (auto& group : context.groups) {
        const int n = static_cast<int>(group.candidates.size());
        group.neighbor_indices.assign(static_cast<std::size_t>(n), {});
        for (int i = 0; i < n; ++i) {
            std::vector<std::pair<double, int>> ranked;
            ranked.reserve(static_cast<std::size_t>(std::max(0, n - 1)));
            const double ndi = group.candidates[static_cast<std::size_t>(i)].material.nd;
            const double vdi = group.candidates[static_cast<std::size_t>(i)].material.vd;
            for (int j = 0; j < n; ++j) {
                if (i == j) {
                    continue;
                }
                const double ndj = group.candidates[static_cast<std::size_t>(j)].material.nd;
                const double vdj = group.candidates[static_cast<std::size_t>(j)].material.vd;
                const double dnd = (ndj - ndi) / nd_scale;
                const double dvd = (vdj - vdi) / vd_scale;
                ranked.emplace_back(dnd * dnd + dvd * dvd, j);
            }
            std::sort(
                ranked.begin(),
                ranked.end(),
                [](const auto& a, const auto& b) {
                    if (a.first != b.first) {
                        return a.first < b.first;
                    }
                    return a.second < b.second;
                });
            auto& out = group.neighbor_indices[static_cast<std::size_t>(i)];
            out.reserve(ranked.size());
            for (const auto& item : ranked) {
                out.push_back(item.second);
            }
        }
    }
}

bool load_glass_search_config(
    const std::string& path,
    const std::string& database_root,
    GlassSearchContext* context,
    std::string* error,
    const std::vector<double>& required_wavelengths_um) {
    if (!context) {
        return false;
    }
    try {
        Json root = JsonParser(read_text_file(path)).parse();
        GlassSearchContext out;
        out.enabled = root.at("enabled").as_bool(true);
        out.database_root = root.at("database_root").as_string(database_root);
        if (out.database_root.empty()) {
            out.database_root = database_root;
        }
        out.mutation_rate = root.at("mutation_rate").as_number(out.mutation_rate);
        out.random_rate = root.at("random_rate").as_number(out.random_rate);
        out.pbest_inherit_rate = root.at("pbest_inherit_rate").as_number(out.pbest_inherit_rate);
        out.neighbor_count = std::max(
            0,
            static_cast<int>(root.at("neighbor_count").as_number(out.neighbor_count)));
        out.local_search = root.at("local_search").as_bool(out.local_search);
        out.neighbor_nd_scale = std::max(
            1.0e-9,
            root.at("neighbor_nd_scale").as_number(out.neighbor_nd_scale));
        out.neighbor_vd_scale = std::max(
            1.0e-9,
            root.at("neighbor_vd_scale").as_number(out.neighbor_vd_scale));
        out.required_wavelengths_um = required_wavelengths_um;
        if (out.required_wavelengths_um.empty()) {
            const Json& configured_wavelengths = root.at("required_wavelengths_um");
            if (configured_wavelengths.is_array()) {
                for (const auto& value : configured_wavelengths.array) {
                    const double wavelength = value.as_number(
                        std::numeric_limits<double>::quiet_NaN());
                    if (std::isfinite(wavelength) && wavelength > 0.0) {
                        out.required_wavelengths_um.push_back(wavelength);
                    }
                }
            }
        }
        std::sort(out.required_wavelengths_um.begin(), out.required_wavelengths_um.end());
        out.required_wavelengths_um.erase(
            std::unique(
                out.required_wavelengths_um.begin(),
                out.required_wavelengths_um.end()),
            out.required_wavelengths_um.end());

        const Json& groups = root.at("groups").is_array() ? root.at("groups") : root.at("surface_configs");
        int fallback_group_id = 0;
        if (groups.is_array()) {
            for (const auto& group_json : groups.array) {
                if (!group_json.at("enabled").as_bool(true)) {
                    continue;
                }
                GlassChoiceGroup group;
                group.id = static_cast<int>(group_json.at("id").as_number(
                    group_json.at("group").as_number(static_cast<double>(fallback_group_id))));
                group.surface_index = static_cast<std::size_t>(
                    std::max(0.0, group_json.at("surface_index").as_number(
                                      group_json.at("index").as_number(0.0))));
                const Json& candidate_items = group_json.at("candidates").is_array()
                                                  ? group_json.at("candidates")
                                                  : group_json.at("glasses");
                if (!candidate_items.is_array()) {
                    ++fallback_group_id;
                    continue;
                }
                const std::size_t rejected_before_group = out.wavelength_rejected_candidates;
                for (const auto& candidate_json : candidate_items.array) {
                    const std::string name = candidate_json.at("name").as_string(
                        candidate_json.at("glass").as_string());
                    const std::string reference = candidate_json.at("reference").as_string(
                        candidate_json.at("library").as_string());
                    const std::string library = candidate_json.at("library").as_string(reference);
                    if (name.empty()) {
                        continue;
                    }
                    Material material;
                    bool loaded = false;
                    const std::string source_path = candidate_json.at("source_path").as_string();
                    const std::string filename = candidate_json.at("filename").as_string();
                    if (!source_path.empty()) {
                        try {
                            loaded = load_material_yaml(
                                source_path,
                                name,
                                reference,
                                &material);
                        } catch (...) {
                            loaded = false;
                        }
                    }
                    if (!loaded && !filename.empty() && !out.database_root.empty()) {
                        try {
                            loaded = load_material_yaml(
                                out.database_root + "\\data-nk\\" + filename,
                                name,
                                reference,
                                &material);
                        } catch (...) {
                            loaded = false;
                        }
                    }
                    if (!loaded && !out.database_root.empty()) {
                        std::string catalog_error;
                        loaded = load_glass_from_catalog(
                            out.database_root,
                            name,
                            reference,
                            &material,
                            &catalog_error);
                    }
                    if (!loaded) {
                        const Json& material_json = candidate_json.at("material");
                        const double nd = candidate_json.at("nd").as_number(
                            material_json.at("nd").as_number(1.0));
                        const double vd = candidate_json.at("vd").as_number(
                            material_json.at("vd").as_number(0.0));
                        material = vd > 0.0 ? Material::abbe(name, nd, vd)
                                            : Material::ideal(name, nd);
                        material.reference = reference;
                        const double wavelength_min = candidate_json.at("min_wavelength").as_number(
                            candidate_json.at("wavelength_min").as_number(
                                std::numeric_limits<double>::quiet_NaN()));
                        const double wavelength_max = candidate_json.at("max_wavelength").as_number(
                            candidate_json.at("wavelength_max").as_number(
                                std::numeric_limits<double>::quiet_NaN()));
                        if (std::isfinite(wavelength_min) && std::isfinite(wavelength_max) &&
                            wavelength_min <= wavelength_max) {
                            material.wavelength_range_declared = true;
                            material.wavelength_min_um = wavelength_min;
                            material.wavelength_max_um = wavelength_max;
                        }
                    }
                    bool covers_required_wavelengths = true;
                    for (const double wavelength_um : out.required_wavelengths_um) {
                        if (!material.covers_wavelength(wavelength_um)) {
                            covers_required_wavelengths = false;
                            break;
                        }
                    }
                    if (!covers_required_wavelengths) {
                        ++out.wavelength_rejected_candidates;
                        std::ostringstream rejection;
                        rejection << name << " / " << reference << " declared ";
                        if (material.wavelength_range_declared) {
                            rejection << material.wavelength_min_um << "-"
                                      << material.wavelength_max_um << " um";
                        } else {
                            rejection << "no wavelength range";
                        }
                        out.wavelength_rejections.push_back(rejection.str());
                        continue;
                    }
                    GlassCandidate candidate;
                    candidate.name = name;
                    candidate.reference = reference;
                    candidate.library = library;
                    candidate.material = std::move(material);
                    group.candidates.push_back(std::move(candidate));
                }
                if (group.candidates.empty() &&
                    out.wavelength_rejected_candidates > rejected_before_group) {
                    std::ostringstream message;
                    message << "glass candidate group for surface " << group.surface_index
                            << " is empty after wavelength filtering; required";
                    for (const double wavelength_um : out.required_wavelengths_um) {
                        message << " " << wavelength_um;
                    }
                    message << " um";
                    throw std::runtime_error(message.str());
                }
                if (!group.candidates.empty()) {
                    out.groups.push_back(std::move(group));
                }
                ++fallback_group_id;
            }
        }
        out.enabled = out.enabled && !out.groups.empty();
        build_glass_neighbor_indices(out);
        *context = std::move(out);
        return true;
    } catch (const std::exception& e) {
        if (error) {
            *error = e.what();
        }
        return false;
    }
}

bool validate_fixed_material_wavelength_coverage(
    const LensSystem& system,
    const std::vector<VariableSpec>& variables,
    std::string* error) {
    std::vector<bool> material_is_variable(system.surfaces.size(), false);
    for (const auto& variable : variables) {
        if ((variable.kind == VariableKind::MaterialNd ||
             variable.kind == VariableKind::MaterialVd ||
             variable.kind == VariableKind::GlassChoice) &&
            variable.index < material_is_variable.size()) {
            material_is_variable[variable.index] = true;
        }
    }

    std::vector<double> required_wavelengths;
    for (const auto& wavelength : system.wavelengths) {
        if (std::isfinite(wavelength.value_um) && wavelength.value_um > 0.0) {
            required_wavelengths.push_back(wavelength.value_um);
        }
    }
    if (required_wavelengths.empty()) {
        required_wavelengths.push_back(0.5875618);
    }

    std::vector<std::string> violations;
    for (std::size_t index = 0; index < system.surfaces.size(); ++index) {
        if (material_is_variable[index]) {
            continue;
        }
        const Material& material = system.surfaces[index].material_after;
        if (material.name == "air" ||
            (material.model == Material::Model::Ideal &&
             material.reference.empty() && material.source_path.empty())) {
            continue;
        }
        const bool exact_catalog_material =
            material.model == Material::Model::Formula ||
            material.model == Material::Model::Tabulated ||
            !material.reference.empty() || !material.source_path.empty();
        if (!exact_catalog_material) {
            continue;
        }
        bool covers = true;
        for (const double wavelength_um : required_wavelengths) {
            if (!material.covers_wavelength(wavelength_um)) {
                covers = false;
                break;
            }
        }
        if (!covers) {
            std::ostringstream message;
            message << "surface " << index << " material " << material.name << " (";
            if (material.wavelength_range_declared) {
                message << material.wavelength_min_um << "-" << material.wavelength_max_um
                        << " um";
            } else {
                message << "no declared wavelength range";
            }
            message << ")";
            violations.push_back(message.str());
        }
    }
    if (violations.empty()) {
        return true;
    }
    if (error) {
        std::ostringstream message;
        message << "fixed catalog material wavelength coverage failed for system wavelengths";
        for (const double wavelength_um : required_wavelengths) {
            message << " " << wavelength_um;
        }
        message << " um: ";
        for (std::size_t index = 0; index < violations.size(); ++index) {
            if (index > 0) {
                message << "; ";
            }
            message << violations[index];
        }
        *error = message.str();
    }
    return false;
}

}  // namespace optiland
