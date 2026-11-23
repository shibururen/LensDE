#pragma once
#include "optiland/optimizer.hpp"
#include "optiland/raytrace.hpp"
#include <string>

namespace lensde { namespace cli {
using namespace optiland;
enum class AsphereReferencePolicy {
    Current,
    Input,
};

enum class LocalBoundsMode {
    Variable,
    Safety,
    SafetyMaterialStrict,
};

bool has_arg(int argc, char** argv, const std::string& name);

std::string string_arg(int argc, char** argv, const std::string& name, const std::string& fallback = "");

int int_arg(int argc, char** argv, const std::string& name, int fallback);

double double_arg(int argc, char** argv, const std::string& name, double fallback);

std::string normalized_option_token(std::string text);

DistanceMetric distance_metric_from_string(const std::string& raw);

std::string distance_metric_name(DistanceMetric metric);

InternalScalingMode internal_scaling_from_string(const std::string& raw);

std::string internal_scaling_name(InternalScalingMode mode);

bool parse_asphere_reference_policy(
    const std::string& raw,
    AsphereReferencePolicy* policy);

const char* asphere_reference_policy_name(AsphereReferencePolicy policy);

LocalBoundsMode local_bounds_mode_from_string(const std::string& raw);

std::string local_bounds_mode_name(LocalBoundsMode mode);

LocalBoundsMode local_bounds_mode_arg(int argc, char** argv);

AdaptiveDeVariant adaptive_de_variant_from_string(const std::string& raw);

std::string adaptive_de_variant_name(AdaptiveDeVariant variant);

PBestGuideScope pbest_guide_scope_from_string(const std::string& raw);

std::string pbest_guide_scope_name(PBestGuideScope scope);

DeMatingScope de_mating_scope_from_string(const std::string& raw);

std::string de_mating_scope_name(DeMatingScope scope);

ConstraintHandlingMode constraint_handling_from_string(const std::string& raw);

std::string constraint_handling_name(ConstraintHandlingMode mode);

TopologyDiversityMode topology_diversity_mode_from_string(const std::string& raw);

std::string topology_diversity_mode_name(TopologyDiversityMode mode);

PopulationGenerationMode population_generation_mode_from_string(const std::string& raw);

std::string population_generation_mode_name(PopulationGenerationMode mode);

RayAimingMode aiming_arg(int argc, char** argv, RayAimingMode fallback = RayAimingMode::Paraxial);

RayAimingStopRadiusSource aiming_stop_radius_source_arg(
    int argc,
    char** argv,
    RayAimingStopRadiusSource fallback = RayAimingStopRadiusSource::Paraxial);

TraceFailurePolicy trace_policy_arg(int argc, char** argv, TraceFailurePolicy fallback);
} } // namespace lensde::cli
