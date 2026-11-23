// Handle query-only requests and load the system before establishing its aperture contract.
// CommandContext owns the resulting lens and input-materialization flags used by later stages.
// Keep explicit aperture settings distinct from defaults and already-materialized input geometry.

#include "command_context.hpp"

namespace lensde { namespace cli { namespace detail {

int CommandContext::dispatch_queries() {
    // Reset persistent command servers to UC10 before reading each request.
    set_asphere_root_policy(kDefaultAsphereRootPolicy);
    if (has_arg(argc, argv, "--help") ||
        has_arg(argc, argv, "-h") ||
        has_arg(argc, argv, "--help-asphere-reference-policy")) {
        print_command_help(std::cout);
        return 0;
    }
    if (has_arg(argc, argv, "--self-test-material-scaling") ||
        has_arg(argc, argv, "--audit-self-test")) {
        return run_material_scaling_self_test() ? 0 : 2;
    }
    if (has_arg(argc, argv, "--self-test-asphere-intersection")) {
        return run_asphere_intersection_self_test() ? 0 : 2;
    }
    if (has_arg(argc, argv, "--hammer") ||
        normalized_option_token(string_arg(argc, argv, "--optimizer")) == "hammer") {
        throw std::invalid_argument("Historical Hammer optimization is not part of LensDE");
    }
    const std::string glass_name = string_arg(argc, argv, "--glass");
    if (!glass_name.empty()) {
        Material glass;
        std::string error;
        const std::string reference = string_arg(argc, argv, "--reference");
        if (!load_glass_from_catalog(
                lensde_default_database(),
                glass_name,
                reference,
                &glass,
                &error)) {
            std::cerr << "Failed to load glass: " << error << "\n";
            return 3;
        }
        std::cout << glass.name << " " << glass.reference
                  << " n(0.4861)=" << glass.refractive_index(0.4861)
                  << " n(0.5876)=" << glass.refractive_index(0.5876)
                  << " n(0.6563)=" << glass.refractive_index(0.6563) << "\n";
        return 0;
    }
    return kContinueCommand;
}

int CommandContext::load_system() {
    lens = make_demo_lens();
    candidate_path = string_arg(argc, argv, "--candidate");
    system_path = string_arg(argc, argv, "--system");
    loaded_external_system = false;
    if (!candidate_path.empty()) {
        std::string error;
        if (!load_optiland_json(candidate_path, &lens, &error)) {
            std::cerr << "Failed to load candidate: " << error << "\n";
            return 2;
        }
        lens.name = "candidate_001";
        loaded_external_system = true;
    } else if (!system_path.empty()) {
        std::string error;
        if (!load_system_json(system_path, &lens, &error)) {
            std::cerr << "Failed to load system: " << error << "\n";
            return 2;
        }
        loaded_external_system = true;
    }
    const char* environment_asphere_root_policy =
        std::getenv("OPTILAND_ASPHERE_ROOT_POLICY");
    const std::string serialized_asphere_root_policy =
        normalized_option_token(lens.asphere_root_policy);
    const std::string default_asphere_root_policy =
        environment_asphere_root_policy != nullptr &&
                environment_asphere_root_policy[0] != '\0'
            ? environment_asphere_root_policy
            : (serialized_asphere_root_policy.empty()
                   ? kDefaultAsphereRootPolicyToken
                   : serialized_asphere_root_policy);
    std::string requested_asphere_root_policy_token = normalized_option_token(
        string_arg(
            argc,
            argv,
            "--asphere-root-policy",
            default_asphere_root_policy));
    if (requested_asphere_root_policy_token.empty()) {
        requested_asphere_root_policy_token = kDefaultAsphereRootPolicyToken;
    }
    optimizer_asphere_root_policy = kDefaultAsphereRootPolicy;
    if (requested_asphere_root_policy_token == "production" ||
        requested_asphere_root_policy_token == "slope_domain_topology_seed" ||
        requested_asphere_root_policy_token == "topology_seed") {
        optimizer_asphere_root_policy =
            AsphereRootPolicy::SlopeDomainTopologySeed;
    } else if (requested_asphere_root_policy_token == "fast_recover" ||
        requested_asphere_root_policy_token == "two_level_fast_recover" ||
        requested_asphere_root_policy_token == "two_level") {
        optimizer_asphere_root_policy = AsphereRootPolicy::FastRecover;
    } else if (requested_asphere_root_policy_token == "certified_first_root" ||
               requested_asphere_root_policy_token == "strict" ||
               requested_asphere_root_policy_token == "strict_first_root") {
        optimizer_asphere_root_policy = AsphereRootPolicy::CertifiedFirstRoot;
    } else if (requested_asphere_root_policy_token == "confirmed_with_fallback" ||
               requested_asphere_root_policy_token == "confirmed") {
        optimizer_asphere_root_policy =
            AsphereRootPolicy::ConfirmedWithCertifiedFallback;
    } else if (requested_asphere_root_policy_token == "unified_certified" ||
               requested_asphere_root_policy_token ==
                   "unified_certified_experimental") {
        optimizer_asphere_root_policy =
            AsphereRootPolicy::UnifiedCertifiedExperimental;
    } else {
        std::cerr << "Unknown --asphere-root-policy: "
                  << requested_asphere_root_policy_token << "\n";
        return 2;
    }
    // Apply the selected contract before any initial score, aperture update,
    // aiming model, or optimizer stage is evaluated.
    set_asphere_root_policy(optimizer_asphere_root_policy);
    lens.asphere_root_policy =
        asphere_root_policy_token(optimizer_asphere_root_policy);
    lens.bfl_reference_surface = int_arg(
        argc,
        argv,
        "--bfl-reference-surface",
        lens.bfl_reference_surface);
    return kContinueCommand;
}

int CommandContext::configure_aperture_contract() {
    update_clear_apertures = loaded_external_system &&
            !has_arg(argc, argv, "--no-clear-aperture-update") &&
            (!system_path.empty() ? has_arg(argc, argv, "--clear-aperture-update") : true);
    input_clear_apertures_materialized = loaded_external_system &&
            has_arg(argc, argv, "--input-clear-apertures-materialized");
    update_input_clear_apertures = update_clear_apertures && !input_clear_apertures_materialized;
    // Keep aperture-update sampling from the merit contract.  A command-line
    // --clear-rings value is an explicit override; otherwise a loaded merit
    // JSON value (or its MeritConfig default) must survive the load/override
    // pass so DE and Locked rescoring use the same aperture rays.
    clear_aperture_rings_explicit = has_arg(argc, argv, "--clear-rings");
    clear_aperture_rings = int_arg(argc, argv, "--clear-rings", 4);
    return kContinueCommand;
}

} } }
