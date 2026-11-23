// Provide the built-in demonstration lens, variables, merit settings, and native help text.
// Input dispatch uses these defaults only when the corresponding external inputs are absent.
// The demo is not a replacement for the saved benchmark inputs or their recorded budgets.

#include "cli_internal.hpp"

namespace lensde { namespace cli { namespace detail {

LensSystem make_demo_lens() {
    LensSystem lens;
    lens.name = "optiland-cpp-demo";
    lens.aperture = {ApertureType::EntrancePupilDiameter, 2.0};
    lens.max_field_deg = 20.0;
    lens.fields = {
        {0.0, 0.0, 0.0, 0.0},
        {0.0, 0.707, 0.0, 0.0},
        {0.0, 1.0, 0.0, 0.0},
    };
    lens.wavelengths = {
        {0.4861, 1.0, false},
        {0.5876, 1.0, true},
        {0.6563, 1.0, false},
    };

    Surface stop;
    stop.kind = SurfaceKind::Plane;
    stop.radius = kInf;
    stop.thickness = 1.0;
    stop.semi_aperture = 1.0;
    stop.aperture_radius = 1.0;
    stop.is_stop = true;
    stop.material_after = Material::air();
    stop.comment = "stop";

    Surface s1;
    s1.radius = 18.0;
    s1.thickness = 2.0;
    s1.semi_aperture = 4.0;
    s1.material_after = Material::abbe("demo-crown", 1.5168, 64.17);

    Surface s2;
    s2.radius = -18.0;
    s2.thickness = 0.8;
    s2.semi_aperture = 4.0;
    s2.material_after = Material::air();

    Surface s3;
    s3.radius = -28.0;
    s3.thickness = 1.6;
    s3.semi_aperture = 4.0;
    s3.material_after = Material::abbe("demo-flint", 1.6727, 32.2);

    Surface s4;
    s4.radius = 38.0;
    s4.thickness = 10.0;
    s4.semi_aperture = 4.0;
    s4.material_after = Material::air();

    Surface image;
    image.kind = SurfaceKind::Image;
    image.radius = kInf;
    image.thickness = 0.0;
    image.semi_aperture = 8.0;
    image.material_after = Material::air();
    image.comment = "image";

    lens.surfaces = {stop, s1, s2, s3, s4, image};
    return lens;
}

std::vector<VariableSpec> make_demo_variables(const LensSystem& lens) {
    std::vector<VariableSpec> vars;
    for (std::size_t i = 1; i <= 4 && i < lens.surfaces.size(); ++i) {
        if (!lens.surfaces[i].is_plane()) {
            const double curvature = 1.0 / lens.surfaces[i].radius;
            vars.push_back({VariableKind::Curvature, i, -0.2, 0.2, curvature, "curvature"});
        }
        if (i + 1 < lens.surfaces.size()) {
            vars.push_back({VariableKind::Thickness, i, 0.2, 18.0, lens.surfaces[i].thickness, "thickness"});
        }
        if (lens.surfaces[i].material_after.vd > 0.0) {
            vars.push_back({VariableKind::MaterialNd, i, 1.45, 1.90, lens.surfaces[i].material_after.nd, "nd"});
            vars.push_back({VariableKind::MaterialVd, i, 20.0, 95.0, lens.surfaces[i].material_after.vd, "vd"});
        }
    }
    for (std::size_t i = 1; i < lens.fields.size(); ++i) {
        vars.push_back({VariableKind::VignetteY, i, 0.0, 0.35, lens.fields[i].fvdy, "field-fvdy"});
    }
    return vars;
}

MeritConfig make_demo_merit() {
    MeritConfig cfg;
    cfg.target_efl = 18.0;
    cfg.target_ttl = 15.4;
    cfg.target_bfl = 10.0;
    cfg.spot_target = 0.05;
    cfg.weight_efl = 1.0;
    cfg.weight_ttl = 0.4;
    cfg.weight_bfl = 0.5;
    cfg.weight_spot = 8.0;
    cfg.weight_edge = 0.8;
    cfg.min_edge_thickness = 0.1;
    cfg.num_rays = 3;
    cfg.pupil_arms = 6;
    cfg.pupil_distribution = "gaussian_ring_arm";
    cfg.codev_sampling_profile = "spherical";
    return cfg;
}

void print_command_help(std::ostream& output) {
    output << "LensDE native optical engine\n"
           << "Input: --system FILE --variables FILE --merit FILE --out-dir DIR\n"
           << "Operations: trace (default), --de, --local, --hybrid-de-dls, --adaptive-hybrid-de-dls\n"
           << "Preparation: --select-seed-population-only, --score-population-only, --set-vignetting\n"
           << "Persistent workers: --server\n"
           << "Paper workflows are configured by python -m lensde run --benchmark TASK --kind workflow\n";
}

} } }
