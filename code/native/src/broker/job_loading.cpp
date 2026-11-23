// Load one island optical system, variables, merit, glass context, and physical seed population.
// Application preparation turns tabular Job records into the validated LoadedJob inputs used by workers.
// Keep dimension checks, fixed-material setup, and archived population row order before execution.

#include "interfaces.hpp"
#include "post_asphere_scaling.hpp"

namespace lensde::broker {

bool load_population_csv(
    const std::string& path,
    int dim,
    std::vector<std::vector<double>>* rows,
    std::string* error) {
    rows->clear();
    std::ifstream in(path);
    if (!in) {
        *error = "cannot open " + path;
        return false;
    }
    std::string line;
    int line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        for (char& ch : line) {
            if (ch == ',' || ch == ';' || ch == '\t') {
                ch = ' ';
            }
        }
        std::istringstream stream(line);
        std::vector<double> row;
        double value = 0.0;
        while (stream >> value) {
            row.push_back(value);
        }
        if (row.empty()) {
            continue;
        }
        if (static_cast<int>(row.size()) != dim) {
            *error = "population line " + std::to_string(line_no) +
                     " has wrong dimension";
            return false;
        }
        rows->push_back(std::move(row));
    }
    return true;
}

LoadedJob load_job(Job job) {
    LoadedJob loaded;
    loaded.job = std::move(job);
    std::string error;
    if (!optiland::load_system_json(
            loaded.job.system_path, &loaded.system, &error)) {
        throw std::runtime_error("system load failed: " + error);
    }
    if (!optiland::load_variables_json(
            loaded.job.variables_path, &loaded.variables, &error)) {
        throw std::runtime_error("variables load failed: " + error);
    }
    if (!optiland::load_merit_json(
            loaded.job.merit_path, &loaded.merit, &error)) {
        throw std::runtime_error("merit load failed: " + error);
    }
    optiland::configure_variable_thickness_constraints(
        &loaded.merit,
        loaded.variables);
    if (!optiland::validate_fixed_material_wavelength_coverage(
            loaded.system,
            loaded.variables,
            &error)) {
        throw std::runtime_error("material wavelength coverage failed: " + error);
    }
    std::vector<double> required_material_wavelengths_um;
    for (const auto& wavelength : loaded.system.wavelengths) {
        if (std::isfinite(wavelength.value_um) && wavelength.value_um > 0.0) {
            required_material_wavelengths_um.push_back(wavelength.value_um);
        }
    }
    if (!loaded.job.glass_search_path.empty() &&
        !optiland::load_glass_search_config(
            loaded.job.glass_search_path,
            "",
            &loaded.glass,
            &error,
            required_material_wavelengths_um)) {
        throw std::runtime_error("glass-search load failed: " + error);
    }
    const bool post_asphere_profile =
        loaded.job.profile.rfind("post_asphere_", 0) == 0;
    // Every post-asphere system carries the GUI-selected aiming policy in its
    // serialized ray_aiming block.  Do not replace any part of that policy in
    // the multi-island route; otherwise batched CUDA scoring evaluates a
    // different optical contract than the CPU and single-process routes.
    optiland::sync_stop_aperture_to_aiming_radius(loaded.system);
    if (post_asphere_profile) {
        rebase_post_asphere_edge_sag_variables(
            loaded.system,
            loaded.variables);
    }
    loaded.merit.dynamic_apertures = true;
    const int aperture_rings =
        loaded.merit.vignetting_merit_aperture_envelope_experimental
            ? std::max(1, loaded.merit.num_rays)
            : (loaded.merit.clear_aperture_rings > 0
                   ? loaded.merit.clear_aperture_rings
                   : loaded.merit.num_rays);
    optiland::update_clear_apertures_from_trace(
        loaded.system,
        aperture_rings,
        "gaussian_ring_arm",
        loaded.merit.sampling_seed,
        loaded.system.ray_aiming.mode,
        loaded.merit.effective_clear_aperture_margin(),
        true,
        loaded.merit.clear_aperture_arms,
        false,
        false,
        loaded.merit.vignetting_boundary_support_tolerance,
        loaded.merit.vignetting_boundary_support_selectivity_slope,
        loaded.merit.vignetting_boundary_support_contract ==
            optiland::VignettingBoundarySupportContract::GlobalTrimRealizability,
        loaded.merit.vignetting_physical_cut_match_enabled,
        loaded.merit.vignetting_optimization_enabled());
    if (!load_population_csv(
            loaded.job.population_path,
            static_cast<int>(loaded.variables.size()),
            &loaded.initial_population,
            &error)) {
        throw std::runtime_error(error);
    }
    if (static_cast<int>(loaded.initial_population.size()) != loaded.job.population) {
        throw std::runtime_error("initial population row count does not match job population");
    }
    return loaded;
}

} // namespace lensde::broker
