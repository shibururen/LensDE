/*
 * Loads material YAML/JSON models and catalog_nk entries for native optical inputs.
 * System and glass-search readers use these helpers to resolve exact glass dispersion.
 * Formula/tabulated coefficients, wavelength coverage and source/reference identity
 * remain intact when a material is found through the supplied database root.
 */

#include "detail.hpp"

namespace optiland {
using namespace io_detail;

namespace io_detail {
bool load_material_yaml(const std::string& path, const std::string& name, const std::string& reference, Material* material) {
    const std::string text = read_text_file(path);
    Material out;
    out.name = name;
    out.reference = reference;
    out.source_path = path;

    std::istringstream lines(text);
    std::string line;
    bool in_data_block = false;
    std::string data_type;
    std::ostringstream table;

    auto flush_table = [&]() {
        if ((data_type == "tabulated n" || data_type == "tabulated nk") && table.tellp() > 0) {
            std::istringstream rows(table.str());
            std::string row;
            while (std::getline(rows, row)) {
                auto vals = parse_doubles(row);
                if (data_type == "tabulated n" && vals.size() >= 2) {
                    out.table_wavelengths.push_back(vals[0]);
                    out.table_n.push_back(vals[1]);
                } else if (data_type == "tabulated nk" && vals.size() >= 2) {
                    out.table_wavelengths.push_back(vals[0]);
                    out.table_n.push_back(vals[1]);
                }
            }
            if (!out.table_wavelengths.empty()) {
                out.model = Material::Model::Tabulated;
                if (!out.wavelength_range_declared) {
                    const auto bounds = std::minmax_element(
                        out.table_wavelengths.begin(), out.table_wavelengths.end());
                    out.wavelength_min_um = *bounds.first;
                    out.wavelength_max_um = *bounds.second;
                    out.wavelength_range_declared = true;
                }
            }
        }
        table.str("");
        table.clear();
    };

    while (std::getline(lines, line)) {
        const auto type_pos = line.find("type:");
        if (type_pos != std::string::npos) {
            flush_table();
            data_type = line.substr(type_pos + 5);
            while (!data_type.empty() && std::isspace(static_cast<unsigned char>(data_type.front()))) {
                data_type.erase(data_type.begin());
            }
            while (!data_type.empty() && std::isspace(static_cast<unsigned char>(data_type.back()))) {
                data_type.pop_back();
            }
            in_data_block = true;
        }

        const auto coeff_pos = line.find("coefficients:");
        if (in_data_block && coeff_pos != std::string::npos && data_type.rfind("formula ", 0) == 0) {
            out.coefficients = parse_doubles(line.substr(coeff_pos + 13));
            out.formula_type = std::atoi(data_type.substr(8).c_str());
            out.model = Material::Model::Formula;
        }

        const auto range_pos = line.find("wavelength_range:");
        if (in_data_block && range_pos != std::string::npos &&
            (data_type.rfind("formula ", 0) == 0 ||
             data_type == "tabulated n" || data_type == "tabulated nk")) {
            const auto range = parse_doubles(line.substr(range_pos + 17));
            if (range.size() >= 2 && std::isfinite(range[0]) &&
                std::isfinite(range[1]) && range[0] <= range[1]) {
                out.wavelength_min_um = range[0];
                out.wavelength_max_um = range[1];
                out.wavelength_range_declared = true;
            }
        }

        const auto nd_pos = line.find("nd:");
        if (nd_pos != std::string::npos) {
            out.nd = std::atof(line.substr(nd_pos + 3).c_str());
        }
        const auto vd_pos = line.find("Vd:");
        if (vd_pos != std::string::npos) {
            out.vd = std::atof(line.substr(vd_pos + 3).c_str());
        }

        if ((data_type == "tabulated n" || data_type == "tabulated nk") &&
            line.find_first_of("0123456789") != std::string::npos &&
            line.find("type:") == std::string::npos &&
            line.find("data:") == std::string::npos) {
            table << line << "\n";
        }
    }
    flush_table();

    if (out.model == Material::Model::Ideal && out.vd > 0.0) {
        out.model = Material::Model::Abbe;
    }
    *material = out;
    return true;
}
}  // namespace io_detail

namespace io_detail {
Material material_from_json(const Json& json) {
    const std::string type = json.at("type").as_string();
    if (type == "IdealMaterial") {
        return Material::ideal("ideal", json.at("index").as_number(1.0));
    }
    if (type == "AbbeMaterial") {
        return Material::abbe("abbe", json.at("index").as_number(1.0), json.at("abbe").as_number(0.0));
    }
    if (type == "Material") {
        Material material;
        const std::string name = json.at("name").as_string();
        const std::string reference = json.at("reference").as_string();
        std::string error;
        if (load_glass_from_catalog(
                "D:\\PycharmProjects\\Optiland\\optiland\\optiland\\database",
                name,
                reference,
                &material,
                &error)) {
            return material;
        }
        return Material::ideal(name.empty() ? "unknown" : name, json.at("index").as_number(1.0));
    }
    return Material::air();
}
}  // namespace io_detail

bool load_glass_from_catalog(
    const std::string& database_root,
    const std::string& name,
    const std::string& reference,
    Material* material,
    std::string* error) {
    try {
        std::ifstream in(database_root + "\\catalog_nk.csv");
        if (!in) {
            throw std::runtime_error("catalog_nk.csv not found");
        }
        std::string header;
        std::getline(in, header);
        std::vector<std::string> rows;
        std::string line;
        while (std::getline(in, line)) {
            rows.push_back(line);
        }

        auto try_match = [&](bool exact_only) -> bool {
            for (const auto& row : rows) {
                auto cols = split_csv_line(row);
                if (cols.size() < 9) {
                    continue;
                }
                const std::string row_ref = cols[3];
                const std::string row_name = cols[4];
                const std::string filename = cols[5];
                const std::string no_ext = cols[8];
                const bool exact_name_match =
                    name.empty() || row_name == name || no_ext == name || row_ref == name;
                const bool exact_ref_match =
                    reference.empty() || row_ref == reference || row_name == reference || no_ext == reference;
                const bool fuzzy_name_match =
                    exact_name_match || row_name.find(name) != std::string::npos || row_ref.find(name) != std::string::npos;
                const bool fuzzy_ref_match =
                    exact_ref_match || row_ref.find(reference) != std::string::npos ||
                    row_name.find(reference) != std::string::npos || no_ext.find(reference) != std::string::npos;
                if ((exact_only && exact_name_match && exact_ref_match) ||
                    (!exact_only && fuzzy_name_match && fuzzy_ref_match)) {
                    return load_material_yaml(
                        database_root + "\\data-nk\\" + filename,
                        no_ext.empty() ? name : no_ext,
                        row_ref,
                        material);
                }
            }
            return false;
        };

        if (try_match(true) || try_match(false)) {
            return true;
        }
        if (error) {
            *error = "glass not found: " + name + " / " + reference;
        }
        return false;
    } catch (const std::exception& e) {
        if (error) {
            *error = e.what();
        }
        return false;
    }
}

}  // namespace optiland
