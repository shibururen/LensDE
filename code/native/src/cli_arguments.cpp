/*
 * CLI argument readers and canonical enum/token conversion shared by command setup.
 * The cli command and broker option builders use these fallback and validation rules.
 * Preserve accepted spellings, numeric defaults, and first-occurrence argument order.
 */
#include "optiland/cli_arguments.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <stdexcept>

namespace lensde { namespace cli {
using namespace optiland;
bool has_arg(int argc, char** argv, const std::string& name) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == name) {
            return true;
        }
    }
    return false;
}

std::string string_arg(int argc, char** argv, const std::string& name, const std::string& fallback ) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (argv[i] == name) {
            return argv[i + 1];
        }
    }
    return fallback;
}

int int_arg(int argc, char** argv, const std::string& name, int fallback) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (argv[i] == name) {
            return std::stoi(argv[i + 1]);
        }
    }
    return fallback;
}

double double_arg(int argc, char** argv, const std::string& name, double fallback) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (argv[i] == name) {
            return std::stod(argv[i + 1]);
        }
    }
    return fallback;
}

std::string normalized_option_token(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        if (ch == '-' || std::isspace(ch)) {
            return '_';
        }
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

DistanceMetric distance_metric_from_string(const std::string& raw) {
    const std::string value = normalized_option_token(raw);
    if (value == "legacy" || value == "legacy_scaled" || value == "scaled") {
        return DistanceMetric::LegacyScaled;
    }
    if (value == "structure" ||
        value == "structural" ||
        value == "structure_bound_normalized" ||
        value == "structural_bound_normalized" ||
        value == "geometry" ||
        value == "geometry_bound_normalized") {
        return DistanceMetric::StructureBoundNormalized;
    }
    return DistanceMetric::BoundNormalized;
}

std::string distance_metric_name(DistanceMetric metric) {
    switch (metric) {
        case DistanceMetric::LegacyScaled:
            return "legacy_scaled";
        case DistanceMetric::StructureBoundNormalized:
            return "structure_bound_normalized";
        case DistanceMetric::BoundNormalized:
            return "bound_normalized";
    }
    return "bound_normalized";
}

InternalScalingMode internal_scaling_from_string(const std::string& raw) {
    const std::string value = normalized_option_token(raw);
    if (value == "edge_sag" ||
        value == "edge_sag_normalized" ||
        value == "asphere_edge_sag" ||
        value == "asphere_edge_sag_normalized") {
        return InternalScalingMode::EdgeSagNormalized;
    }
    if (value == "bound" || value == "normalized" || value == "bound_normalized") {
        return InternalScalingMode::BoundNormalized;
    }
    return InternalScalingMode::LegacyOptical;
}

std::string internal_scaling_name(InternalScalingMode mode) {
    switch (mode) {
        case InternalScalingMode::BoundNormalized:
            return "bound_normalized";
        case InternalScalingMode::EdgeSagNormalized:
            return "edge_sag_normalized";
        case InternalScalingMode::LegacyOptical:
            return "legacy_optical";
    }
    return "legacy_optical";
}

bool parse_asphere_reference_policy(
    const std::string& raw,
    AsphereReferencePolicy* policy) {
    if (policy == nullptr) {
        return false;
    }
    const std::string value = normalized_option_token(raw);
    if (value == "current") {
        *policy = AsphereReferencePolicy::Current;
        return true;
    }
    if (value == "input") {
        *policy = AsphereReferencePolicy::Input;
        return true;
    }
    return false;
}

const char* asphere_reference_policy_name(AsphereReferencePolicy policy) {
    switch (policy) {
        case AsphereReferencePolicy::Current:
            return "current";
        case AsphereReferencePolicy::Input:
            return "input";
    }
    return "current";
}

LocalBoundsMode local_bounds_mode_from_string(const std::string& raw) {
    const std::string value = normalized_option_token(raw);
    if (value == "safety" ||
        value == "safe" ||
        value == "relaxed" ||
        value == "relaxed_safety" ||
        value == "no_hard_bounds" ||
        value == "soft") {
        return LocalBoundsMode::Safety;
    }
    if (value == "safety_material_strict" ||
        value == "safety_geometry_material_strict" ||
        value == "mixed" ||
        value == "catalog_material") {
        return LocalBoundsMode::SafetyMaterialStrict;
    }
    return LocalBoundsMode::Variable;
}

std::string local_bounds_mode_name(LocalBoundsMode mode) {
    if (mode == LocalBoundsMode::Safety) {
        return "safety";
    }
    if (mode == LocalBoundsMode::SafetyMaterialStrict) {
        return "safety_material_strict";
    }
    return "variable";
}

LocalBoundsMode local_bounds_mode_arg(int argc, char** argv) {
    LocalBoundsMode mode = local_bounds_mode_from_string(
        string_arg(argc, argv, "--local-bounds-mode",
            string_arg(argc, argv, "--dls-bounds-mode", "safety")));
    if (has_arg(argc, argv, "--local-safety-bounds") ||
        has_arg(argc, argv, "--local-no-hard-bounds") ||
        has_arg(argc, argv, "--dls-safety-bounds")) {
        mode = LocalBoundsMode::Safety;
    }
    if (has_arg(argc, argv, "--local-variable-bounds") ||
        has_arg(argc, argv, "--local-hard-bounds") ||
        has_arg(argc, argv, "--dls-variable-bounds")) {
        mode = LocalBoundsMode::Variable;
    }
    if (has_arg(argc, argv, "--local-safety-material-strict-bounds")) {
        mode = LocalBoundsMode::SafetyMaterialStrict;
    }
    return mode;
}

AdaptiveDeVariant adaptive_de_variant_from_string(const std::string& raw) {
    const std::string value = normalized_option_token(raw);
    if (value == "classic_best1bin" || value == "best1bin") {
        return AdaptiveDeVariant::ClassicBest1Bin;
    }
    if (value == "classic_rand1bin" || value == "rand1bin") {
        return AdaptiveDeVariant::ClassicRand1Bin;
    }
    if (value == "classic_currenttobest1bin" ||
        value == "classic_current_to_best1bin" ||
        value == "currenttobest1bin" ||
        value == "current_to_best1bin") {
        return AdaptiveDeVariant::ClassicCurrentToBest1Bin;
    }
    if (value == "jde_rand1bin" || value == "jde") {
        return AdaptiveDeVariant::JdeRand1Bin;
    }
    if (value == "jade_pbest_archive" || value == "jade") {
        return AdaptiveDeVariant::JadePBestArchive;
    }
    if (value == "jade_pbest_archive_lpsr" || value == "jade_lpsr") {
        return AdaptiveDeVariant::JadePBestArchiveLpsr;
    }
    if (value == "basic_ga" || value == "ga" || value == "genetic_algorithm") {
        throw std::invalid_argument("Historical GA/PSO/SA variants are outside the LensDE paper scope");
    }
    if (value == "basic_pso" || value == "pso" || value == "particle_swarm") {
        throw std::invalid_argument("Historical GA/PSO/SA variants are outside the LensDE paper scope");
    }
    if (value == "basic_sa" || value == "sa" || value == "simulated_annealing") {
        throw std::invalid_argument("Historical GA/PSO/SA variants are outside the LensDE paper scope");
    }
    return AdaptiveDeVariant::JadePBestArchiveLpsrFeas;
}

std::string adaptive_de_variant_name(AdaptiveDeVariant variant) {
    switch (variant) {
        case AdaptiveDeVariant::ClassicBest1Bin:
            return "classic_best1bin";
        case AdaptiveDeVariant::ClassicRand1Bin:
            return "classic_rand1bin";
        case AdaptiveDeVariant::ClassicCurrentToBest1Bin:
            return "classic_currenttobest1bin";
        case AdaptiveDeVariant::JdeRand1Bin:
            return "jde_rand1bin";
        case AdaptiveDeVariant::JadePBestArchive:
            return "jade_pbest_archive";
        case AdaptiveDeVariant::JadePBestArchiveLpsr:
            return "jade_pbest_archive_lpsr";
        case AdaptiveDeVariant::JadePBestArchiveLpsrFeas:
            return "jade_pbest_archive_lpsr_feas";
    }
    return "jade_pbest_archive_lpsr_feas";
}

PBestGuideScope pbest_guide_scope_from_string(const std::string& raw) {
    const std::string value = normalized_option_token(raw);
    if (value == "global" || value == "jade" || value == "standard_jade" ||
        value == "global_top_p") {
        return PBestGuideScope::Global;
    }
    if (value == "samekey" || value == "same_key" || value == "same_topology" ||
        value == "same_topology_only" || value == "same_key_only") {
        return PBestGuideScope::SameKey;
    }
    if (value == "basin" || value == "basinlocal" || value == "basin_local" ||
        value == "local_basin" || value == "archive_basin") {
        return PBestGuideScope::BasinLocal;
    }
    if (value == "mixed" || value == "mixture") {
        return PBestGuideScope::Mixed;
    }
    if (value == "none" || value == "off" || value == "disabled" || value == "rand" ||
        value == "no_pbest") {
        return PBestGuideScope::None;
    }
    return PBestGuideScope::Legacy;
}

std::string pbest_guide_scope_name(PBestGuideScope scope) {
    switch (scope) {
        case PBestGuideScope::Legacy:
            return "legacy";
        case PBestGuideScope::Global:
            return "global";
        case PBestGuideScope::SameKey:
            return "same_key";
        case PBestGuideScope::BasinLocal:
            return "basin_local";
        case PBestGuideScope::Mixed:
            return "mixed";
        case PBestGuideScope::None:
            return "none";
    }
    return "legacy";
}

DeMatingScope de_mating_scope_from_string(const std::string& raw) {
    const std::string value = normalized_option_token(raw);
    if (value == "samekey" || value == "same_key" || value == "same_topology" ||
        value == "same_topology_only" || value == "same_key_only") {
        return DeMatingScope::SameKey;
    }
    if (value == "basin" || value == "basinlocal" || value == "basin_local" ||
        value == "local_basin" || value == "basin_mating") {
        return DeMatingScope::BasinLocal;
    }
    if (value == "activebasin" || value == "active_basin" ||
        value == "activebasinonly" || value == "active_basin_only" ||
        value == "active_basin_local" || value == "active_local") {
        return DeMatingScope::ActiveBasinOnly;
    }
    if (value == "mixed" || value == "mixture" || value == "local_global") {
        return DeMatingScope::Mixed;
    }
    return DeMatingScope::Global;
}

std::string de_mating_scope_name(DeMatingScope scope) {
    switch (scope) {
        case DeMatingScope::Global:
            return "global";
        case DeMatingScope::SameKey:
            return "same_key";
        case DeMatingScope::BasinLocal:
            return "basin_local";
        case DeMatingScope::ActiveBasinOnly:
            return "active_basin_only";
        case DeMatingScope::Mixed:
            return "mixed";
    }
    return "global";
}

ConstraintHandlingMode constraint_handling_from_string(const std::string& raw) {
    const std::string value = normalized_option_token(raw);
    if (value == "scalar" || value == "scalar_merit" || value == "legacy") {
        return ConstraintHandlingMode::ScalarMerit;
    }
    if (value == "epsilon" || value == "epsilon_constraint") {
        return ConstraintHandlingMode::EpsilonConstraint;
    }
    return ConstraintHandlingMode::FeasibilityRules;
}

std::string constraint_handling_name(ConstraintHandlingMode mode) {
    switch (mode) {
        case ConstraintHandlingMode::ScalarMerit:
            return "scalar_merit";
        case ConstraintHandlingMode::EpsilonConstraint:
            return "epsilon_constraint";
        case ConstraintHandlingMode::FeasibilityRules:
            return "feasibility_rules";
    }
    return "feasibility_rules";
}

TopologyDiversityMode topology_diversity_mode_from_string(const std::string& raw) {
    const std::string value = normalized_option_token(raw);
    if (value == "descriptor_shadow" || value == "shadow") {
        return TopologyDiversityMode::DescriptorShadow;
    }
    if (value == "descriptor_audit" || value == "audit") {
        return TopologyDiversityMode::DescriptorAudit;
    }
    if (value == "descriptor_select" || value == "select") {
        return TopologyDiversityMode::DescriptorSelect;
    }
    if (value == "descriptor_full" || value == "full") {
        return TopologyDiversityMode::DescriptorFull;
    }
    if (value == "topology_similarity" || value == "similarity" ||
        value == "weighted_similarity") {
        return TopologyDiversityMode::TopologySimilarity;
    }
    if (value == "topology_family_similarity" || value == "family_similarity" ||
        value == "topology_family" || value == "family") {
        return TopologyDiversityMode::TopologyFamilySimilarity;
    }
    return TopologyDiversityMode::Legacy;
}

std::string topology_diversity_mode_name(TopologyDiversityMode mode) {
    switch (mode) {
        case TopologyDiversityMode::DescriptorShadow:
            return "descriptor_shadow";
        case TopologyDiversityMode::DescriptorAudit:
            return "descriptor_audit";
        case TopologyDiversityMode::DescriptorSelect:
            return "descriptor_select";
        case TopologyDiversityMode::DescriptorFull:
            return "descriptor_full";
        case TopologyDiversityMode::TopologySimilarity:
            return "topology_similarity";
        case TopologyDiversityMode::TopologyFamilySimilarity:
            return "topology_family_similarity";
        case TopologyDiversityMode::Legacy:
            return "legacy";
    }
    return "legacy";
}

PopulationGenerationMode population_generation_mode_from_string(const std::string& raw) {
    const std::string value = normalized_option_token(raw);
    if (value == "restricted" || value == "restricted_perturbation" ||
        value == "limited" || value == "limited_perturbation") {
        return PopulationGenerationMode::RestrictedPerturbation;
    }
    if (value == "sag_geometry" || value == "sag_geometry_perturbation" ||
        value == "asphere_sag_restart" || value == "periodic_sag_restart") {
        return PopulationGenerationMode::SagGeometryPerturbation;
    }
    if (value == "top_half_single_surface_correlated_sag" ||
        value == "top_half_correlated_sag" ||
        value == "periodic_top_half_sag") {
        return PopulationGenerationMode::TopHalfSingleSurfaceCorrelatedSag;
    }
    if (value == "top_half_single_block") {
        return PopulationGenerationMode::TopHalfSingleBlock;
    }
    if (value == "top_half_coupled_surface") {
        return PopulationGenerationMode::TopHalfCoupledSurface;
    }
    if (value == "physics_constrained" || value == "physical_constraints" ||
        value == "manufacturing_constrained" || value == "manufacturing_constraints" ||
        value == "opti_neuro" || value == "optineuro" ||
        value == "gao_2025" || value == "gao_physics") {
        return PopulationGenerationMode::PhysicsConstrained;
    }
    return PopulationGenerationMode::Random;
}

std::string population_generation_mode_name(PopulationGenerationMode mode) {
    switch (mode) {
        case PopulationGenerationMode::PhysicsConstrained:
            return "physics_constrained";
        case PopulationGenerationMode::RestrictedPerturbation:
            return "restricted_perturbation";
        case PopulationGenerationMode::SagGeometryPerturbation:
            return "sag_geometry_perturbation";
        case PopulationGenerationMode::TopHalfSingleSurfaceCorrelatedSag:
            return "top_half_single_surface_correlated_sag";
        case PopulationGenerationMode::TopHalfSingleBlock:
            return "top_half_single_block";
        case PopulationGenerationMode::TopHalfCoupledSurface:
            return "top_half_coupled_surface";
        case PopulationGenerationMode::Random:
            return "random";
    }
    return "random";
}

RayAimingMode aiming_arg(int argc, char** argv, RayAimingMode fallback ) {
    const std::string mode = string_arg(argc, argv, "--aiming");
    if (mode.empty()) {
        return fallback;
    }
    std::string value = mode;
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return c == '-' || c == ' ' ? '_' : static_cast<char>(std::tolower(c));
    });
    if (value == "direct") {
        return RayAimingMode::Direct;
    }
    if (value == "paraxial") {
        return RayAimingMode::Paraxial;
    }
    if (value == "iterative") {
        return RayAimingMode::Iterative;
    }
    if (value == "calibrated_paraxial" || value == "calibrated_paraxial_fast" ||
        value == "calibrated_paraxial_auto" ||
        value == "calibrated_paraxial_corrected" ||
        value == "field_pupil_plane" || value == "field_calibrated_paraxial") {
        return RayAimingMode::CalibratedParaxial;
    }
    if (value == "three_step" || value == "three_step_ray_aiming" ||
        value == "gao_three_step" || value == "gao_2025" ||
        value == "opti_neuro" || value == "optineuro") {
        return RayAimingMode::ThreeStep;
    }
    if (value == "robust") {
        return RayAimingMode::Robust;
    }
    if (value == "adaptive" || value == "adaptive_pilot_grid" ||
        value == "pilot_grid" || value == "adaptive_pilot") {
        return RayAimingMode::AdaptivePilotGrid;
    }
    return fallback;
}

RayAimingStopRadiusSource aiming_stop_radius_source_arg(
    int argc,
    char** argv,
    RayAimingStopRadiusSource fallback ) {
    std::string source = string_arg(argc, argv, "--aiming-stop-radius-source");
    if (source.empty()) {
        source = string_arg(argc, argv, "--aiming-stop-radius-mode");
    }
    if (source.empty()) {
        return fallback;
    }
    std::transform(source.begin(), source.end(), source.begin(), [](unsigned char c) {
        return c == '-' || c == ' ' ? '_' : static_cast<char>(std::tolower(c));
    });
    if (source == "real" || source == "real_ray" || source == "real_rays" ||
        source == "real_stop" || source == "real_stop_radius" ||
        source == "actual" || source == "actual_ray") {
        return RayAimingStopRadiusSource::Real;
    }
    if (source == "paraxial" || source == "paraxial_ray" || source == "paraxial_rays" ||
        source == "paraxial_stop" || source == "paraxial_stop_radius") {
        return RayAimingStopRadiusSource::Paraxial;
    }
    return fallback;
}

TraceFailurePolicy trace_policy_arg(int argc, char** argv, TraceFailurePolicy fallback) {
    const std::string raw = string_arg(argc, argv, "--trace-policy");
    if (raw.empty()) {
        return fallback;
    }
    std::string value = raw;
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return c == '-' || c == ' ' ? '_' : static_cast<char>(std::tolower(c));
    });
    if (value == "soft" || value == "soft_coverage" || value == "coverage") {
        return TraceFailurePolicy::SoftCoverage;
    }
    if (value == "geometry_only" || value == "geometry_only_when_trace_bad" ||
        value == "geometry_when_trace_bad") {
        return TraceFailurePolicy::GeometryOnlyWhenTraceBad;
    }
    return TraceFailurePolicy::Strict;
}
} } // namespace lensde::cli
