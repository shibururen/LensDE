// Implement the explicit native asphere-intersection and material-scaling self-test commands.
// Query dispatch invokes these checks independently of the ordinary optimizer command pipeline.
// Keep reference geometry, root tolerances, and failure exit status separate from benchmark claims.

#include "cli_internal.hpp"

namespace lensde { namespace cli { namespace detail {

bool run_asphere_intersection_self_test() {
    // Most fixtures below are certificate regressions (including cases where
    // every analytic/Newton seed intentionally converges to a later root).
    // Keep the legacy reference fixtures independent of the UC10 default.
    ScopedAsphereRootPolicy policy_scope(
        AsphereRootPolicy::CertifiedFirstRoot);
    bool ok = true;
    auto check = [&](bool condition, const std::string& message) {
        if (!condition) {
            ok = false;
            std::cerr << "ASPHERE INTERSECTION SELFTEST FAIL: " << message << "\n";
        }
    };
    auto check_root = [&](
                          const Surface& surface,
                          const Ray& ray,
                          double expected,
                          double tolerance,
                          const std::string& label) {
        double t = std::numeric_limits<double>::quiet_NaN();
        const bool hit = intersect_surface(surface, ray, &t);
        check(hit, label + " did not intersect");
        if (!hit) {
            return;
        }
        check(
            std::abs(t - expected) <= tolerance,
            label + " selected t=" + std::to_string(t) +
                " instead of first root " + std::to_string(expected));
        const double x = ray.p.x + t * ray.d.x;
        const double y = ray.p.y + t * ray.d.y;
        const double z = ray.p.z + t * ray.d.z;
        const double residual = surface_sag(surface, x, y) - z;
        check(
            std::isfinite(residual) && std::abs(residual) <= 1.0e-8,
            label + " returned a root with excessive residual");
    };

    Surface regression;
    regression.kind = SurfaceKind::EvenAsphere;
    regression.radius = 8.4180143331;
    regression.conic = 6.9171242411;
    regression.asphere_coefficients = {
        0.0,
        -0.00363573793716,
        -0.00155628778353,
        -8.47032101119e-05,
        -6.65447191728e-05,
        9.03604433674e-06,
        3.65841833675e-06,
        -2.29711292067e-07,
    };
    Ray regression_ray;
    regression_ray.p = {
        -0.11324589000563,
        0.7844539212036,
        -0.2899211155982,
    };
    regression_ray.d = {
        0.010814746204492,
        0.71948883156865,
        0.69441980279404,
    };
    check_root(
        regression,
        regression_ray,
        0.5224258388469845,
        2.0e-9,
        "20260721 S8 blue double-root regression");

    // Only the plane seed is available and it is exactly the later root t=2.
    // The first-root certification must still recover the earlier t=0.5 root.
    Surface later_seed;
    later_seed.kind = SurfaceKind::EvenAsphere;
    later_seed.radius = std::numeric_limits<double>::infinity();
    later_seed.asphere_coefficients = {-6.4, 1.6};
    Ray later_seed_ray;
    later_seed_ray.p = {0.0, 0.0, -2.0};
    later_seed_ray.d = {1.0, 0.0, 1.0};
    check_root(
        later_seed,
        later_seed_ray,
        0.5,
        2.0e-9,
        "all available seeds converge to a later root");

    // The first root is tangent: F(t) touches zero at t=0.5 without a sign
    // change, while the only plane seed is the later root t=2.
    Surface tangent_first;
    tangent_first.kind = SurfaceKind::EvenAsphere;
    tangent_first.radius = std::numeric_limits<double>::infinity();
    tangent_first.asphere_coefficients = {-13.44, 31.52, -7.04};
    check_root(
        tangent_first,
        later_seed_ray,
        0.5,
        1.0e-5,
        "tangent first root before a converged later root");

    // A refracting aperture-stop surface is finite even when its analytic sag
    // function has a mathematically valid continuation outside the opening.
    // The certified production trace must ignore the earlier outside root and
    // choose the first root inside aperture_radius. Aperture calibration must
    // still be able to request the unbounded branch explicitly.
    Surface finite_stop;
    finite_stop.kind = SurfaceKind::EvenAsphere;
    finite_stop.radius = 3.2385196249;
    finite_stop.conic = -0.27795317496;
    finite_stop.asphere_coefficients = {
        0.0,
        -0.00304576428535,
        -0.00472340480126,
        0.00377678842687,
        -0.00173369833533,
        -0.000934773232306,
        0.000582935064451,
        -0.000171790870214,
    };
    finite_stop.is_stop = true;
    finite_stop.aperture_radius = 1.15741326242;
    Ray finite_stop_ray;
    finite_stop_ray.p = {
        0.0,
        -1.849818383414541,
        -1.4826662965134165,
    };
    finite_stop_ray.d = {
        0.0,
        0.38384532029442153,
        0.92339740636850021,
    };
    finite_stop_ray.wavelength_um = 0.5876;
    finite_stop_ray.valid = true;
    finite_stop_ray.intensity = 1.0;
    check_root(
        finite_stop,
        finite_stop_ray,
        1.817389016566865,
        2.0e-8,
        "S5 finite-stop in-aperture first root");

    double unbounded_stop_t = std::numeric_limits<double>::quiet_NaN();
    const bool unbounded_stop_hit =
        intersect_surface(finite_stop, finite_stop_ray, &unbounded_stop_t, true);
    check(unbounded_stop_hit, "S5 calibration bypass did not intersect");
    if (unbounded_stop_hit) {
        check(
            std::abs(unbounded_stop_t - 0.00030614428233767445) <= 2.0e-8,
            "S5 calibration bypass did not preserve the unbounded first root");
    }

    // The base conic has no real ray intersection and the plane seed is
    // outside the conic's real radial domain. The high-order departure still
    // creates the physical S3 intersection observed by Zemax in run
    // 20260728_193724_289, so the forward-domain fallback must recover it.
    Surface domain_fallback;
    domain_fallback.kind = SurfaceKind::EvenAsphere;
    domain_fallback.radius = -8.34053130814;
    domain_fallback.conic = 27.5637957005;
    domain_fallback.asphere_coefficients = {
        0.0,
        -0.0271521250057,
        -0.00928934015251,
        -0.00279194431871,
        0.000114146874522,
        0.000423939386709,
        -0.000108471452979,
        -0.0000678211851671,
    };
    const Vec3 domain_start{0.0, 1.2974308352, -0.77977666158404};
    const Vec3 domain_target{0.0, 1.4768680403, -0.49533306233};
    Ray domain_fallback_ray;
    domain_fallback_ray.p = domain_start;
    domain_fallback_ray.d = normalize(domain_target - domain_start);
    check_root(
        domain_fallback,
        domain_fallback_ray,
        norm(domain_target - domain_start),
        2.0e-8,
        "20260728 S3 high-order-only domain intersection");

    Surface sphere;
    sphere.kind = SurfaceKind::Standard;
    sphere.radius = 10.0;
    Ray axial;
    axial.p = {0.0, 0.0, -2.0};
    axial.d = {0.0, 0.0, 1.0};
    check_root(sphere, axial, 2.0, 1.0e-12, "standard sphere compatibility");

    // Production-policy coverage: locked real-world multi-root regression,
    // high-order-only domain rescue, and non-asphere compatibility. The two
    // synthetic posterior-certificate fixtures above deliberately do not
    // belong to this faster no-postverify contract.
    set_asphere_root_policy(AsphereRootPolicy::SlopeDomainTopologySeed);
    check_root(
        regression,
        regression_ray,
        0.5224258388469845,
        2.0e-9,
        "production 20260721 S8 blue regression");
    check_root(
        domain_fallback,
        domain_fallback_ray,
        norm(domain_target - domain_start),
        2.0e-8,
        "production 20260728 S3 domain rescue");
    check_root(
        sphere,
        axial,
        2.0,
        1.0e-12,
        "production standard sphere compatibility");

    if (ok) {
        std::cout << "asphere intersection self-test passed\n";
    }
    return ok;
}

bool run_material_scaling_self_test() {
    bool ok = true;
    auto check = [&](bool condition, const std::string& message) {
        if (!condition) {
            ok = false;
            std::cerr << "SELFTEST FAIL: " << message << "\n";
        }
    };

    Material formula;
    formula.name = "synthetic-catalog";
    formula.reference = "selftest";
    formula.source_path = "synthetic";
    formula.model = Material::Model::Formula;
    formula.nd = 1.5168;
    formula.vd = 64.17;
    formula.formula_type = 2;
    formula.coefficients = {
        0.0,
        1.03961212,
        0.00600069867,
        0.231792344,
        0.0200179144,
        1.01046945,
        103.560653};

    LensSystem lens;
    Surface surface;
    surface.material_after = formula;
    lens.surfaces.push_back(surface);

    const double wave_f = 0.4861327;
    const double wave_d = 0.5875618;
    const double wave_c = 0.6562725;

    VariableSpec vd_var{VariableKind::MaterialVd, 0, 20.0, 95.0, formula.vd, "vd"};
    LensSystem vd_low = apply_variables(lens, {vd_var}, {30.0});
    LensSystem vd_high = apply_variables(lens, {vd_var}, {70.0});
    const Material& vd_material = vd_low.surfaces[0].material_after;
    check(vd_material.model == Material::Model::Abbe, "MaterialVd should force Abbe model");
    check(vd_material.coefficients.empty(), "MaterialVd should clear formula coefficients");
    check(std::abs(vd_material.refractive_index(wave_d) - vd_material.nd) < 1.0e-10,
          "Abbe d-line refractive index should equal nd");
    const double low_fc =
        vd_low.surfaces[0].material_after.refractive_index(wave_f) -
        vd_low.surfaces[0].material_after.refractive_index(wave_c);
    const double high_fc =
        vd_high.surfaces[0].material_after.refractive_index(wave_f) -
        vd_high.surfaces[0].material_after.refractive_index(wave_c);
    check(std::abs(low_fc - high_fc) > 1.0e-5,
          "MaterialVd should change F/C dispersion");

    VariableSpec nd_var{VariableKind::MaterialNd, 0, 1.45, 1.90, formula.nd, "nd"};
    LensSystem nd_changed = apply_variables(lens, {nd_var}, {1.70});
    const Material& nd_material = nd_changed.surfaces[0].material_after;
    check(nd_material.model == Material::Model::Abbe, "MaterialNd should force Abbe model");
    check(std::abs(nd_material.refractive_index(wave_d) - 1.70) < 1.0e-10,
          "MaterialNd should change d-line refractive index");

    VariableSpec thickness_var{VariableKind::Thickness, 0, 0.0, 10.0, 1.0, "thickness"};
    LensSystem untouched = apply_variables(lens, {thickness_var}, {2.0});
    const Material& untouched_material = untouched.surfaces[0].material_after;
    check(untouched_material.model == Material::Model::Formula,
          "non-material variables should preserve catalog formula model");
    check(!untouched_material.coefficients.empty(),
          "non-material variables should preserve formula coefficients");

    std::vector<VariableSpec> transform_vars = {
        {VariableKind::Radius, 0, 10.0, 100.0, 50.0, "radius"},
        {VariableKind::Curvature, 0, -0.2, 0.2, 0.05, "curvature"},
        {VariableKind::Thickness, 0, 0.0, 20.0, 7.5, "thickness"},
        {VariableKind::Conic, 0, -50.0, 50.0, -3.0, "conic"},
        {VariableKind::AsphereCoeff, 0, -1.0e-3, 1.0e-3, 2.0e-5, "asphere", 1},
        {VariableKind::MaterialNd, 0, 1.45, 1.90, 1.60, "nd"},
        {VariableKind::MaterialVd, 0, 20.0, 95.0, 50.0, "vd"},
        {VariableKind::VignetteX, 0, -0.35, 0.35, 0.05, "vignette_x"},
        {VariableKind::VignetteY, 0, -0.35, 0.35, -0.05, "vignette_y"}};
    for (const VariableSpec& var : transform_vars) {
        for (InternalScalingMode mode : {InternalScalingMode::LegacyOptical,
                                         InternalScalingMode::BoundNormalized,
                                         InternalScalingMode::EdgeSagNormalized}) {
            VariableTransform transform{mode, var};
            const double internal = transform.to_internal(var.initial);
            const double physical = transform.to_physical(internal);
            check(std::abs(physical - var.initial) < 1.0e-12,
                  "VariableTransform should round-trip " + var.role);
        }
    }

    VariableSpec vd_span{VariableKind::MaterialVd, 0, 20.0, 100.0, 60.0, "vd"};
    VariableSpec t_span{VariableKind::Thickness, 0, 0.0, 20.0, 10.0, "thickness"};
    VariableTransform vd_norm{InternalScalingMode::BoundNormalized, vd_span};
    VariableTransform t_norm{InternalScalingMode::BoundNormalized, t_span};
    const double vd_half_delta = vd_norm.to_internal(60.0) - vd_norm.to_internal(20.0);
    const double t_half_delta = t_norm.to_internal(10.0) - t_norm.to_internal(0.0);
    check(std::abs(vd_half_delta - t_half_delta) < 1.0e-12,
          "bound-normalized transform should equalize same fractional span changes");

    VariableSpec conic_merit_bounded{
        VariableKind::Conic, 0, -100.0, 100.0, 0.0, "merit-bounded conic"};
    check(project_variable_value(conic_merit_bounded, 350.0) == 350.0,
          "conic projection should preserve merit-penalized values beyond +/-100");
    VariableTransform conic_norm{
        InternalScalingMode::BoundNormalized, conic_merit_bounded};
    check(std::abs(conic_norm.to_physical(2.25) - 350.0) < 1.0e-12,
          "bound-normalized conic transform should not collapse out-of-box DE coordinates");

    LensSystem front_stop_lens;
    front_stop_lens.aperture.type = ApertureType::EntrancePupilDiameter;
    front_stop_lens.aperture.value = 4.0;
    Surface object_surface;
    object_surface.kind = SurfaceKind::Object;
    front_stop_lens.surfaces.push_back(object_surface);
    Surface automatic_front_stop;
    automatic_front_stop.kind = SurfaceKind::Plane;
    automatic_front_stop.is_stop = true;
    automatic_front_stop.clear_aperture_fixed = false;
    automatic_front_stop.semi_aperture = 1.5;
    automatic_front_stop.aperture_radius = 1.5;
    front_stop_lens.surfaces.push_back(automatic_front_stop);
    Surface image_surface;
    image_surface.kind = SurfaceKind::Image;
    front_stop_lens.surfaces.push_back(image_surface);
    sync_stop_aperture_to_aiming_radius(front_stop_lens);
    check(std::abs(front_stop_lens.surfaces[1].aperture_radius - 2.0) < 1.0e-12,
          "automatic front stop should refresh to EPD/2 after an EPD change");

    VariableSpec edge_sag_var{
        VariableKind::AsphereCoeff,
        0,
        -0.2 / 43046721.0,
        0.2 / 43046721.0,
        0.05 / 43046721.0,
        "A16 edge sag",
        7};
    edge_sag_var.asphere_reference_radius = 3.0;
    edge_sag_var.edge_sag_lower = -0.2;
    edge_sag_var.edge_sag_upper = 0.2;
    VariableTransform edge_sag_transform{InternalScalingMode::EdgeSagNormalized, edge_sag_var};
    const double edge_internal = edge_sag_transform.to_internal(edge_sag_var.initial);
    check(std::abs(edge_internal - 0.625) < 1.0e-12,
          "edge-sag transform should normalize A16 through q=A*r_ref^16");
    check(std::abs(edge_sag_transform.to_physical(edge_internal) - edge_sag_var.initial) < 1.0e-18,
          "edge-sag transform should round-trip A16");
    check(std::abs(edge_sag_transform.physical_delta_to_internal(0.04 / 43046721.0) - 0.1) < 1.0e-12,
          "edge-sag transform should scale physical coefficient deltas through q");

    constexpr double seed_q_power = 1.0e16;
    VariableSpec seed_q_var{
        VariableKind::AsphereCoeff,
        0,
        -0.2 / seed_q_power,
        0.2 / seed_q_power,
        0.0,
        "A16 q-aware seed selector",
        7};
    seed_q_var.asphere_reference_radius = 10.0;
    seed_q_var.edge_sag_lower = -0.2;
    seed_q_var.edge_sag_upper = 0.2;
    const std::vector<VariableSpec> seed_q_variables{seed_q_var};
    const std::vector<double> seed_q_zero{0.0};
    const std::vector<double> seed_q_positive{0.05 / seed_q_power};
    const std::vector<double> seed_q_negative{-0.05 / seed_q_power};
    {
        std::ostringstream raw_key;
        raw_key << std::fixed << std::setprecision(12) << seed_q_positive[0];
        check(raw_key.str() == "0.000000000000",
              "A16 seed regression fixture should collapse under the legacy raw key");
    }
    check(seed_row_key_cpp(seed_q_zero, seed_q_variables) !=
              seed_row_key_cpp(seed_q_positive, seed_q_variables),
          "seed selector should distinguish high-order coefficients through q-space keys");
    check(active_count_cpp(seed_q_positive, seed_q_variables) == 1,
          "seed selector should recognize a high-order q-space coefficient as active");
    check(std::abs(seed_axis_normalized_cpp(seed_q_positive[0], seed_q_var) - 0.625) < 1.0e-12,
          "seed selector should normalize high-order coefficient distances in q space");

    VariableSpec seed_plain_var{
        VariableKind::Thickness, 0, 0.0, 1.0, 0.0, "plain seed axis"};
    const std::vector<VariableSpec> seed_plain_variables{seed_plain_var};
    const std::vector<double> seed_plain_row{0.25};
    check(seed_row_key_cpp(seed_plain_row, seed_plain_variables) == "0.250000000000",
          "seed selector should preserve raw keys for non-asphere variables");
    check(std::abs(seed_axis_normalized_cpp(seed_plain_row[0], seed_plain_var) - 0.25) < 1.0e-12,
          "seed selector should preserve raw normalized coordinates for non-asphere variables");

    std::vector<SeedPoolCandidate> seed_q_candidates;
    for (const auto& row : {
             seed_q_zero,
             seed_q_positive,
             seed_q_negative,
             seed_q_positive}) {
        SeedPoolCandidate candidate;
        candidate.row = row;
        seed_q_candidates.push_back(std::move(candidate));
    }
    double seed_q_selection_seconds = 0.0;
    int seed_q_deduped_count = 0;
    const auto seed_q_selected = select_seed_population_cpp(
        std::move(seed_q_candidates),
        seed_q_variables,
        3,
        20260715U,
        0.5,
        0.5,
        &seed_q_selection_seconds,
        &seed_q_deduped_count);
    check(seed_q_deduped_count == 3 && seed_q_selected.size() == 3,
          "seed selector should retain distinct A16 q candidates while deduplicating repeats");

    LensSystem rebase_lens;
    rebase_lens.aperture.value = 2.0;
    Surface rebase_surface;
    rebase_surface.aperture_radius = 4.0;
    rebase_surface.asphere_coefficients.resize(8, 0.0);
    rebase_surface.asphere_coefficients[7] = 0.05 / 1.0e16;
    rebase_lens.surfaces.push_back(rebase_surface);
    VariableSpec rebase_var{
        VariableKind::AsphereCoeff,
        0,
        -0.2 / 1.0e16,
        0.2 / 1.0e16,
        0.05 / 1.0e16,
        "A16 aperture rebase",
        7};
    rebase_var.asphere_reference_radius = 10.0;
    rebase_var.edge_sag_lower = -0.2;
    rebase_var.edge_sag_upper = 0.2;
    std::vector<VariableSpec> rebase_vars{rebase_var};
    check(rebase_asphere_edge_sag_variables(rebase_lens, rebase_vars) == 1,
          "aperture update should rebase one A16 variable");
    check(std::abs(rebase_vars[0].asphere_reference_radius - 4.0) < 1.0e-12,
          "aperture update should freeze the new A16 reference radius");
    check(std::abs(rebase_vars[0].upper - 0.2 / 4294967296.0) < 1.0e-20,
          "aperture update should preserve the A16 edge-sag budget");

    if (ok) {
        std::cout << "material/scaling audit self-test passed\n";
    }
    return ok;
}

} } }
