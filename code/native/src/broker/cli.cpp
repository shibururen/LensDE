// Parse broker command options and configure shared root, builder-schedule, and ray-layout controls.
// Application setup uses these helpers before creating the shared population evaluator.
// Keep recognized aliases and environment overrides stable because they select CUDA execution paths.

#include "interfaces.hpp"

namespace lensde::broker {

std::string arg_value(int argc, char** argv, const std::string& name) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (argv[i] == name) {
            return argv[i + 1];
        }
    }
    return {};
}

bool has_arg(int argc, char** argv, const std::string& name) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == name) {
            return true;
        }
    }
    return false;
}

std::string normalized_option_token(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        if (c == '-' || c == ' ') {
            return '_';
        }
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

optiland::AsphereRootPolicy parse_asphere_root_policy(const std::string& value) {
    std::string token = normalized_option_token(value);
    if (token.empty()) {
        token = optiland::kDefaultAsphereRootPolicyToken;
    }
    if (token == "production" ||
        token == "slope_domain_topology_seed" ||
        token == "topology_seed") {
        return optiland::AsphereRootPolicy::SlopeDomainTopologySeed;
    }
    if (token == "confirmed_with_fallback" || token == "confirmed") {
        return optiland::AsphereRootPolicy::ConfirmedWithCertifiedFallback;
    }
    if (token == "fast_recover" ||
        token == "two_level_fast_recover" ||
        token == "two_level") {
        return optiland::AsphereRootPolicy::FastRecover;
    }
    if (token == "certified_first_root" ||
        token == "strict" ||
        token == "strict_first_root") {
        return optiland::AsphereRootPolicy::CertifiedFirstRoot;
    }
    if (token == "unified_certified" ||
        token == "unified_certified_experimental") {
        return optiland::AsphereRootPolicy::UnifiedCertifiedExperimental;
    }
    throw std::invalid_argument("unknown --asphere-root-policy: " + value);
}

const char* asphere_root_policy_name(optiland::AsphereRootPolicy policy) {
    switch (policy) {
        case optiland::AsphereRootPolicy::SlopeDomainTopologySeed:
            return "production_slope_domain_topology_seed";
        case optiland::AsphereRootPolicy::ConfirmedWithCertifiedFallback:
            return "confirmed_with_fallback";
        case optiland::AsphereRootPolicy::FastRecover:
            return "fast_recover";
        case optiland::AsphereRootPolicy::CertifiedFirstRoot:
            return "certified_first_root";
        case optiland::AsphereRootPolicy::UnifiedCertifiedExperimental:
            return "unified_certified_experimental";
    }
    return "unknown";
}

void set_process_environment(const char* name, const std::string& value) {
#ifdef _WIN32
    if (_putenv_s(name, value.c_str()) != 0) {
        throw std::runtime_error(std::string("cannot set environment variable ") + name);
    }
#else
    if (setenv(name, value.c_str(), 1) != 0) {
        throw std::runtime_error(std::string("cannot set environment variable ") + name);
    }
#endif
}

std::string configure_calibrated_builder_schedule(const std::string& value) {
    const std::string token = normalized_option_token(
        value.empty() ? "auto" : value);
    if (token == "auto" || token == "adaptive") {
        return "auto";
    }
    if (token == "fused" || token == "round17_fused") {
        set_process_environment("OPTILAND_DE_CUDA_CALIBRATED_ORDERED_TILE8", "0");
        return "fused";
    }
    if (token == "ordered" || token == "ordered_tile8") {
        set_process_environment("OPTILAND_DE_CUDA_CALIBRATED_ORDERED_TILE8", "1");
        return "ordered_tile8";
    }
    throw std::invalid_argument(
        "unknown --calibrated-builder-schedule: " + value);
}

std::string configure_transpose_ray_tasks(const std::string& value) {
    const std::string token = normalized_option_token(
        value.empty() ? "auto" : value);
    if (token == "auto") {
        return "auto";
    }
    if (token == "on" || token == "true" || token == "1") {
        set_process_environment("OPTILAND_DE_CUDA_TRANSPOSE_RAY_TASKS", "1");
        return "on";
    }
    if (token == "off" || token == "false" || token == "0") {
        set_process_environment("OPTILAND_DE_CUDA_TRANSPOSE_RAY_TASKS", "0");
        return "off";
    }
    throw std::invalid_argument("unknown --transpose-ray-tasks: " + value);
}

} // namespace lensde::broker
