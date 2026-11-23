/*
 * Implements compact LensSystem and imported Optiland JSON readers for native CLI inputs.
 * Reconstructs fields, wavelengths, apertures, aiming, aspheres and material models.
 * Source/catalog/nd-Vd fallbacks, field-factor aliases and infinite-object conventions
 * preserve the prescription and resource identity supplied to subsequent ray tracing.
 */

#include "detail.hpp"

namespace optiland {
using namespace io_detail;

bool load_optiland_json(const std::string& path, LensSystem* system, std::string* error) {
    if (!system) {
        return false;
    }
    try {
        Json root = JsonParser(read_text_file(path)).parse();
        LensSystem lens;
        lens.name = path;

        const Json& aperture = root.at("aperture");
        const std::string ap_type = aperture.at("type").as_string("EPD");
        if (ap_type == "imageFNO") {
            lens.aperture.type = ApertureType::ImageFNumber;
        } else if (ap_type == "objectNA") {
            lens.aperture.type = ApertureType::ObjectNumericalAperture;
        } else if (ap_type == "float_by_stop_size") {
            lens.aperture.type = ApertureType::FloatByStopSize;
        } else {
            lens.aperture.type = ApertureType::EntrancePupilDiameter;
        }
        lens.aperture.value = aperture.at("value").as_number(1.0);

        const Json& field_items = root.at("fields").at("fields");
        double max_field = 0.0;
        for (const auto& item : field_items.array) {
            const double x = item.at("x").as_number(0.0);
            const double y = item.at("y").as_number(0.0);
            max_field = std::max(max_field, std::sqrt(x * x + y * y));
        }
        lens.max_field_deg = max_field;
        if (max_field <= kEps) {
            lens.fields.push_back(Field{});
        } else {
            for (const auto& item : field_items.array) {
                Field field;
                field.hx = item.at("x").as_number(0.0) / max_field;
                field.hy = item.at("y").as_number(0.0) / max_field;
                field.fvdx = item.at("vx").as_number(0.0);
                field.fvdy = item.at("vy").as_number(0.0);
                field.vignette_x = field.fvdx;
                field.vignette_y = field.fvdy;
                lens.fields.push_back(field);
            }
        }

        const Json& waves = root.at("wavelengths").at("wavelengths");
        for (const auto& item : waves.array) {
            Wavelength wave;
            wave.value_um = item.at("value").as_number(0.5875618);
            wave.weight = item.at("weight").as_number(1.0);
            wave.primary = item.at("is_primary").as_bool(false);
            lens.wavelengths.push_back(wave);
        }

        const Json& surfaces = root.at("surface_group").at("surfaces");
        bool skipped_object = false;
        for (const auto& item : surfaces.array) {
            const std::string surface_type = item.at("type").as_string();
            if (surface_type == "ObjectSurface") {
                skipped_object = true;
                continue;
            }
            const Json& geometry = item.at("geometry");
            const std::string geom_type = geometry.at("type").as_string();
            Surface surface;
            surface.kind = SurfaceKind::Standard;
            if (geom_type == "Plane") {
                surface.kind = SurfaceKind::Plane;
            } else if (geom_type == "EvenAsphere") {
                surface.kind = SurfaceKind::EvenAsphere;
            }
            surface.radius = geometry.at("radius").as_number(kInf);
            surface.conic = geometry.at("conic").as_number(0.0);
            const Json& coeffs = geometry.at("coefficients");
            if (coeffs.is_array()) {
                for (const auto& c : coeffs.array) {
                    surface.asphere_coefficients.push_back(c.as_number(0.0));
                }
            }
            surface.thickness = item.at("thickness").as_number(0.0);
            surface.is_stop = item.at("is_stop").as_bool(false);
            const Json& aperture_obj = item.at("aperture");
            if (aperture_obj.is_object()) {
                const std::string aperture_type = aperture_obj.at("type").as_string();
                if (aperture_type == "RadialAperture" || aperture_obj.at("r_max").type == Json::Type::Number) {
                    surface.aperture_radius = aperture_obj.at("r_max").as_number(0.0);
                }
            }
            surface.semi_aperture = item.at("semi_aperture").as_number(surface.aperture_radius);
            surface.mechanical_aperture = item.at("mechanical_aperture").as_number(
                item.at("mechanical_aperture_radius").as_number(
                    std::max(surface.semi_aperture, surface.aperture_radius)));
            surface.clear_aperture_fixed = item.at("clear_aperture_fixed").as_bool(
                surface.aperture_radius > 0.0 && !surface.is_stop);
            surface.vignetting_aperture_excluded = !surface.is_stop &&
                item.at("vignetting_aperture_excluded").as_bool(false);
            if (surface.vignetting_aperture_excluded) {
                surface.clear_aperture_fixed = false;
                surface.aperture_radius = 0.0;
            }
            surface.mechanical_aperture_fixed = item.at("mechanical_aperture_fixed").as_bool(false);
            surface.material_after = material_from_json(item.at("material_post"));
            surface.reflective = item.at("interaction_model").at("is_reflective").as_bool(false);
            lens.surfaces.push_back(surface);
        }
        if (!lens.surfaces.empty()) {
            lens.surfaces.back().kind = SurfaceKind::Image;
        }
        if (!skipped_object && error) {
            *error = "warning: no ObjectSurface found";
        }

        *system = lens;
        return true;
    } catch (const std::exception& e) {
        if (error) {
            *error = e.what();
        }
        return false;
    }
}

Material simple_material_from_json(const Json& json) {
    const std::string name = json.at("name").as_string("air");
    const std::string mode = json.at("mode").as_string();
    const std::string reference = json.at("reference").as_string();
    const std::string source_path = json.at("source_path").as_string();
    if (mode == "air" || name.empty()) {
        return Material::air();
    }
    if ((mode.empty() || mode == "glass") && !reference.empty() && name != "air") {
        Material material;
        std::string error;
        if (!source_path.empty()) {
            try {
                if (load_material_yaml(source_path, name, reference, &material)) {
                    return material;
                }
            } catch (...) {
                // Fall through to the catalog lookup and finally nd/Vd.
            }
        }
        if (load_glass_from_catalog(
                "D:\\PycharmProjects\\Optiland\\optiland\\optiland\\database",
                name,
                reference,
                &material,
                &error)) {
            return material;
        }
    }
    const double nd = json.at("nd").as_number(json.at("index").as_number(1.0));
    const double vd = json.at("vd").as_number(json.at("abbe").as_number(0.0));
    if (name == "air" || std::abs(nd - 1.0) < 1.0e-12) {
        return Material::air();
    }
    if (vd > 0.0) {
        Material material = Material::abbe(name, nd, vd);
        if (mode == "glass") {
            material.reference = reference;
            material.source_path = source_path;
        }
        return material;
    }
    Material material = Material::ideal(name, nd);
    if (mode == "glass") {
        material.reference = reference;
        material.source_path = source_path;
    }
    return material;
}

bool load_system_json(const std::string& path, LensSystem* system, std::string* error) {
    if (!system) {
        return false;
    }
    try {
        Json root = JsonParser(read_text_file(path)).parse();
        LensSystem lens;
        lens.name = root.at("name").as_string("optiland-cpp");
        lens.glass_library = root.at("glass_library").as_string("SCHOTT");
        const Json& glass_libraries = root.at("glass_libraries");
        if (glass_libraries.is_array()) {
            for (const auto& item : glass_libraries.array) {
                const std::string library = item.as_string();
                if (!library.empty()) {
                    lens.glass_libraries.push_back(library);
                }
            }
        }
        if (lens.glass_libraries.empty()) {
            lens.glass_libraries.push_back(lens.glass_library);
        }
        lens.bfl_reference_surface = static_cast<int>(
            root.at("bfl_reference_surface").as_number(-1.0));
        lens.asphere_root_policy =
            root.at("asphere_root_policy").as_string(kDefaultAsphereRootPolicyToken);
        if (lens.asphere_root_policy.empty()) {
            lens.asphere_root_policy = kDefaultAsphereRootPolicyToken;
        }

        const Json& aperture = root.at("aperture");
        const std::string ap_type = aperture.at("type").as_string("EPD");
        if (ap_type == "imageFNO") {
            lens.aperture.type = ApertureType::ImageFNumber;
        } else if (ap_type == "objectNA") {
            lens.aperture.type = ApertureType::ObjectNumericalAperture;
        } else if (ap_type == "float_by_stop_size") {
            lens.aperture.type = ApertureType::FloatByStopSize;
        } else {
            lens.aperture.type = ApertureType::EntrancePupilDiameter;
        }
        lens.aperture.value = aperture.at("value").as_number(10.0);
        lens.max_field_deg = root.at("max_field_deg").as_number(0.0);

        const Json& ray_aiming = root.at("ray_aiming");
        if (ray_aiming.is_object()) {
            const std::string raw_aiming_mode =
                ray_aiming.at("mode").as_string(ray_aiming_mode_name(lens.ray_aiming.mode));
            lens.ray_aiming.mode = ray_aiming_mode_from_string(raw_aiming_mode);
            const bool corrected_calibrated_alias =
                normalized_name(raw_aiming_mode) == "calibrated_paraxial_corrected";
            lens.ray_aiming.max_iter =
                static_cast<int>(ray_aiming.at("max_iter").as_number(lens.ray_aiming.max_iter));
            lens.ray_aiming.tolerance =
                ray_aiming.at("tolerance").as_number(lens.ray_aiming.tolerance);
            lens.ray_aiming.start_offset =
                ray_aiming.at("start_offset").as_number(lens.ray_aiming.start_offset);
            lens.ray_aiming.require_convergence =
                ray_aiming.at("require_convergence").as_bool(lens.ray_aiming.require_convergence);
            lens.ray_aiming.continuation_steps = std::max(
                1,
                static_cast<int>(
                    ray_aiming.at("continuation_steps").as_number(lens.ray_aiming.continuation_steps)));
            const std::string raw_stop_radius_source =
                ray_aiming.at("stop_radius_source").as_string(
                    ray_aiming_stop_radius_source_name(lens.ray_aiming.stop_radius_source));
            lens.ray_aiming.stop_radius_source =
                ray_aiming_stop_radius_source_from_string(
                    raw_stop_radius_source,
                    lens.ray_aiming.stop_radius_source);
            const Json& cp_json = ray_aiming.at("calibrated_paraxial");
            if (cp_json.is_object()) {
                auto& cp = lens.ray_aiming.calibrated_paraxial;
                cp.enabled = cp_json.at("enabled").as_bool(cp.enabled);
                cp.sampling_mode =
                    normalized_name(cp_json.at("sampling_mode").as_string(cp.sampling_mode));
                cp.allow_grid_upgrade =
                    cp_json.at("allow_grid_upgrade").as_bool(cp.allow_grid_upgrade);
                cp.pilot_grid_n = std::max(
                    3,
                    static_cast<int>(cp_json.at("pilot_grid_n").as_number(cp.pilot_grid_n)));
                cp.jacobian_probe_fraction =
                    cp_json.at("jacobian_probe_fraction").as_number(cp.jacobian_probe_fraction);
                cp.min_probe_step_mm =
                    cp_json.at("min_probe_step_mm").as_number(cp.min_probe_step_mm);
                cp.max_probe_step_mm =
                    cp_json.at("max_probe_step_mm").as_number(cp.max_probe_step_mm);
                cp.max_condition_number =
                    cp_json.at("max_condition_number").as_number(cp.max_condition_number);
                cp.fit_rms_tolerance_stop_fraction =
                    cp_json.at("fit_rms_tolerance_stop_fraction").as_number(
                        cp.fit_rms_tolerance_stop_fraction);
                cp.validation_sample_count = std::max(
                    0,
                    static_cast<int>(cp_json.at("validation_sample_count").as_number(
                        cp.validation_sample_count)));
                cp.validation_residual_stop_fraction =
                    cp_json.at("validation_residual_stop_fraction").as_number(
                        cp.validation_residual_stop_fraction);
                cp.use_previous_field_model =
                    cp_json.at("use_previous_field_model").as_bool(cp.use_previous_field_model);
                cp.use_previous_wave_model =
                    cp_json.at("use_previous_wave_model").as_bool(cp.use_previous_wave_model);
                cp.chief_anchor_mode =
                    normalized_name(cp_json.at("chief_anchor_mode").as_string(cp.chief_anchor_mode));
                cp.budget_chief_iterations = std::max(
                    0,
                    static_cast<int>(cp_json.at("budget_chief_iterations").as_number(
                        cp.budget_chief_iterations)));
                cp.budget_chief_solver =
                    normalized_name(cp_json.at("budget_chief_solver").as_string(
                        cp.budget_chief_solver));
                cp.budget_chief_acceptance =
                    normalized_name(cp_json.at("budget_chief_acceptance").as_string(
                        cp.budget_chief_acceptance));
                cp.budget_chief_accept_stop_fraction =
                    cp_json.at("budget_chief_accept_stop_fraction").as_number(
                        cp.budget_chief_accept_stop_fraction);
                cp.budget_chief_residual_stop_fraction =
                    cp_json.at("budget_chief_residual_stop_fraction").as_number(
                        cp.budget_chief_residual_stop_fraction);
                cp.budget_chief_min_a =
                    cp_json.at("budget_chief_min_a").as_number(cp.budget_chief_min_a);
                cp.budget_chief_field_continuation_angle_deg =
                    cp_json.at("budget_chief_field_continuation_angle_deg").as_number(
                        cp.budget_chief_field_continuation_angle_deg);
                cp.budget_chief_ignore_aperture_clip =
                    cp_json.at("budget_chief_ignore_aperture_clip").as_bool(
                        cp.budget_chief_ignore_aperture_clip);
                cp.budget_chief_skip_abcd_angle_deg =
                    cp_json.at("budget_chief_skip_abcd_angle_deg").as_number(
                        cp.budget_chief_skip_abcd_angle_deg);
                cp.fallback = normalized_name(cp_json.at("fallback").as_string(cp.fallback));
                cp.correction_iterations = std::max(
                    0,
                    static_cast<int>(cp_json.at("correction_iterations").as_number(
                        cp.correction_iterations)));
                cp.edge_calibration_enabled =
                    cp_json.at("edge_calibration_enabled").as_bool(cp.edge_calibration_enabled);
                cp.edge_calibration_iterations = std::max(
                    0,
                    static_cast<int>(cp_json.at("edge_calibration_iterations").as_number(
                        cp.edge_calibration_iterations)));
                cp.edge_calibration_solver =
                    normalized_name(cp_json.at("edge_calibration_solver").as_string(
                        cp.edge_calibration_solver));
                cp.edge_calibration_weight_power =
                    cp_json.at("edge_calibration_weight_power").as_number(
                        cp.edge_calibration_weight_power);
                cp.edge_calibration_residual_stop_fraction =
                    cp_json.at("edge_calibration_residual_stop_fraction").as_number(
                        cp.edge_calibration_residual_stop_fraction);
            }
            if (corrected_calibrated_alias &&
                lens.ray_aiming.calibrated_paraxial.correction_iterations <= 0) {
                lens.ray_aiming.calibrated_paraxial.correction_iterations = 2;
            }
            const Json& ts_json = ray_aiming.at("three_step");
            if (ts_json.is_object()) {
                auto& ts = lens.ray_aiming.three_step;
                ts.enabled = ts_json.at("enabled").as_bool(ts.enabled);
                ts.sampling_mode =
                    normalized_name(ts_json.at("sampling_mode").as_string(ts.sampling_mode));
                ts.chief_reverse_trial_rays = std::max(
                    2,
                    static_cast<int>(ts_json.at("chief_reverse_trial_rays").as_number(
                        ts.chief_reverse_trial_rays)));
                ts.chief_reverse_max_angle_deg =
                    ts_json.at("chief_reverse_max_angle_deg").as_number(
                        ts.chief_reverse_max_angle_deg);
                ts.edge_passes = std::max(
                    1,
                    static_cast<int>(ts_json.at("edge_passes").as_number(ts.edge_passes)));
                ts.meridional_iterations = std::max(
                    0,
                    static_cast<int>(ts_json.at("meridional_iterations").as_number(
                        ts.meridional_iterations)));
                ts.sagittal_iterations = std::max(
                    0,
                    static_cast<int>(ts_json.at("sagittal_iterations").as_number(
                        ts.sagittal_iterations)));
                ts.edge_solver =
                    normalized_name(ts_json.at("edge_solver").as_string(ts.edge_solver));
                ts.edge_residual_stop_fraction =
                    ts_json.at("edge_residual_stop_fraction").as_number(
                        ts.edge_residual_stop_fraction);
                ts.edge_initial_radius_fraction =
                    ts_json.at("edge_initial_radius_fraction").as_number(
                        ts.edge_initial_radius_fraction);
                ts.accept_stop_fraction =
                    ts_json.at("accept_stop_fraction").as_number(ts.accept_stop_fraction);
                ts.correction_iterations = std::max(
                    0,
                    static_cast<int>(ts_json.at("correction_iterations").as_number(
                        ts.correction_iterations)));
                ts.sample_stop_residual_debug =
                    ts_json.at("sample_stop_residual_debug").as_bool(
                        ts.sample_stop_residual_debug);
                ts.fallback = normalized_name(ts_json.at("fallback").as_string(ts.fallback));
            }
            const Json& apg_json = ray_aiming.at("adaptive_pilot_grid");
            if (apg_json.is_object()) {
                auto& apg = lens.ray_aiming.adaptive_pilot_grid;
                apg.enabled = apg_json.at("enabled").as_bool(apg.enabled);
                apg.try_iterative_first =
                    apg_json.at("try_iterative_first").as_bool(apg.try_iterative_first);
                apg.try_iterative_for_each_ray =
                    apg_json.at("try_iterative_for_each_ray").as_bool(apg.try_iterative_for_each_ray);
                apg.iterative_failure_sample_limit = std::max(
                    0,
                    static_cast<int>(apg_json.at("iterative_failure_sample_limit").as_number(
                        apg.iterative_failure_sample_limit)));
                apg.require_final_convergence =
                    apg_json.at("require_final_convergence").as_bool(apg.require_final_convergence);
                apg.target_field_direction_tolerance =
                    apg_json.at("target_field_direction_tolerance").as_number(
                        apg.target_field_direction_tolerance);
                apg.use_chief_gate = apg_json.at("use_chief_gate").as_bool(apg.use_chief_gate);
                apg.use_primary_wave_as_anchor =
                    apg_json.at("use_primary_wave_as_anchor").as_bool(apg.use_primary_wave_as_anchor);
                apg.use_previous_field_chief =
                    apg_json.at("use_previous_field_chief").as_bool(apg.use_previous_field_chief);
                apg.use_previous_field_extrapolation =
                    apg_json.at("use_previous_field_extrapolation").as_bool(
                        apg.use_previous_field_extrapolation);
                apg.max_field_extrapolation_factor =
                    apg_json.at("max_field_extrapolation_factor").as_number(
                        apg.max_field_extrapolation_factor);
                apg.use_previous_wave_seed =
                    apg_json.at("use_previous_wave_seed").as_bool(apg.use_previous_wave_seed);
                apg.use_previous_field_seed =
                    apg_json.at("use_previous_field_seed").as_bool(apg.use_previous_field_seed);
                apg.use_previous_pupil_seed =
                    apg_json.at("use_previous_pupil_seed").as_bool(apg.use_previous_pupil_seed);
                apg.use_chief_jacobian_seed =
                    apg_json.at("use_chief_jacobian_seed").as_bool(apg.use_chief_jacobian_seed);
                apg.coarse_grid_n = std::max(
                    3,
                    static_cast<int>(apg_json.at("coarse_grid_n").as_number(apg.coarse_grid_n)));
                apg.fine_grid_n = std::max(
                    3,
                    static_cast<int>(apg_json.at("fine_grid_n").as_number(apg.fine_grid_n)));
                apg.nearest_k =
                    std::max(1, static_cast<int>(apg_json.at("nearest_k").as_number(apg.nearest_k)));
                apg.max_coarse_expansions = std::max(
                    0,
                    static_cast<int>(apg_json.at("max_coarse_expansions").as_number(
                        apg.max_coarse_expansions)));
                apg.max_fine_expansions = std::max(
                    0,
                    static_cast<int>(apg_json.at("max_fine_expansions").as_number(
                        apg.max_fine_expansions)));
                apg.coarse_min_halfwidth_fraction =
                    apg_json.at("coarse_min_halfwidth_fraction").as_number(
                        apg.coarse_min_halfwidth_fraction);
                apg.coarse_max_halfwidth_fraction =
                    apg_json.at("coarse_max_halfwidth_fraction").as_number(
                        apg.coarse_max_halfwidth_fraction);
                apg.fine_min_halfwidth_fraction =
                    apg_json.at("fine_min_halfwidth_fraction").as_number(
                        apg.fine_min_halfwidth_fraction);
                apg.fine_max_halfwidth_fraction =
                    apg_json.at("fine_max_halfwidth_fraction").as_number(
                        apg.fine_max_halfwidth_fraction);
                apg.fine_default_halfwidth_fraction =
                    apg_json.at("fine_default_halfwidth_fraction").as_number(
                        apg.fine_default_halfwidth_fraction);
                apg.jacobian_probe_fraction =
                    apg_json.at("jacobian_probe_fraction").as_number(apg.jacobian_probe_fraction);
                apg.jacobian_safety_factor =
                    apg_json.at("jacobian_safety_factor").as_number(apg.jacobian_safety_factor);
                apg.allow_delaunay_interpolation =
                    apg_json.at("allow_delaunay_interpolation").as_bool(
                        apg.allow_delaunay_interpolation);
                apg.allow_barycentric_interpolation =
                    apg_json.at("allow_barycentric_interpolation").as_bool(
                        apg.allow_barycentric_interpolation);
                apg.use_multistart = apg_json.at("use_multistart").as_bool(apg.use_multistart);
                apg.emit_diagnostics =
                    apg_json.at("emit_diagnostics").as_bool(apg.emit_diagnostics);
            }
        }

        const Json& field_items = root.at("fields");
        if (field_items.is_array()) {
            for (const auto& item : field_items.array) {
                Field field;
                field.hx = item.at("hx").as_number(0.0);
                field.hy = item.at("hy").as_number(0.0);
                field.fvcx = item.at("fvcx").as_number(0.0);
                field.fvcy = item.at("fvcy").as_number(0.0);
                field.fvdx = item.at("fvdx").as_number(item.at("vignette_x").as_number(0.0));
                field.fvdy = item.at("fvdy").as_number(item.at("vignette_y").as_number(0.0));
                field.vignette_x = field.fvdx;
                field.vignette_y = field.fvdy;
                lens.fields.push_back(field);
            }
        }

        const Json& wave_items = root.at("wavelengths");
        if (wave_items.is_array()) {
            for (const auto& item : wave_items.array) {
                Wavelength wave;
                wave.value_um = item.at("value_um").as_number(item.at("value").as_number(0.5875618));
                wave.weight = item.at("weight").as_number(1.0);
                wave.primary = item.at("primary").as_bool(item.at("is_primary").as_bool(false));
                lens.wavelengths.push_back(wave);
            }
        }

        const Json& surface_items = root.at("surfaces");
        if (surface_items.is_array()) {
            for (const auto& item : surface_items.array) {
                Surface surface;
                surface.kind = surface_kind_from_string(item.at("kind").as_string("standard"));
                surface.radius = item.at("radius").as_number(kInf);
                surface.conic = item.at("conic").as_number(0.0);
                const Json& thickness_json = item.at("thickness");
                surface.thickness = surface.kind == SurfaceKind::Object && thickness_json.is_null()
                                        ? kInf
                                        : thickness_json.as_number(0.0);
                surface.semi_aperture = item.at("semi_aperture").as_number(
                    item.at("clear_aperture_radius").as_number(0.0));
                surface.aperture_radius = item.at("aperture_radius").as_number(0.0);
                const Json& aperture_obj = item.at("aperture");
                if (surface.aperture_radius <= 0.0 && aperture_obj.is_object()) {
                    surface.aperture_radius = aperture_obj.at("r_max").as_number(0.0);
                }
                surface.is_stop = item.at("is_stop").as_bool(false);
                surface.mechanical_aperture = item.at("mechanical_aperture").as_number(
                    item.at("mechanical_aperture_radius").as_number(
                        std::max(surface.semi_aperture, surface.aperture_radius)));
                surface.clear_aperture_fixed = item.at("clear_aperture_fixed").as_bool(
                    surface.aperture_radius > 0.0 && !surface.is_stop);
                surface.vignetting_aperture_excluded = !surface.is_stop &&
                    item.at("vignetting_aperture_excluded").as_bool(false);
                if (surface.vignetting_aperture_excluded) {
                    surface.clear_aperture_fixed = false;
                    surface.aperture_radius = 0.0;
                }
                surface.mechanical_aperture_fixed = item.at("mechanical_aperture_fixed").as_bool(false);
                surface.reflective = item.at("reflective").as_bool(false);
                surface.material_after = simple_material_from_json(item.at("material_after"));
                const Json& coeffs = item.at("asphere_coefficients");
                if (coeffs.is_array()) {
                    for (const auto& c : coeffs.array) {
                        surface.asphere_coefficients.push_back(c.as_number(0.0));
                    }
                }
                lens.surfaces.push_back(surface);
            }
        }

        *system = lens;
        return true;
    } catch (const std::exception& e) {
        if (error) {
            *error = e.what();
        }
        return false;
    }
}

}  // namespace optiland
