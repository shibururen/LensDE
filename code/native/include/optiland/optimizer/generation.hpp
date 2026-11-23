#pragma once
#include "optiland/optimizer/de_strategies.hpp"
#include <string>
#include <vector>

namespace optiland {
enum class PopulationGenerationMode {
    Random,
    RestrictedPerturbation,
    SagGeometryPerturbation,
    TopHalfSingleSurfaceCorrelatedSag,
    TopHalfSingleBlock,
    TopHalfCoupledSurface,
    PhysicsConstrained,
};

struct TopologyDescriptorOptions {
    TopologyDiversityMode mode = TopologyDiversityMode::Legacy;
    int window_lmin = 2;
    int window_lmax = 4;
    int max_active_axes = 5;
    int audit_interval_generations = 25;
    double weak_tau_low = 0.08;
    double weak_tau_high = 0.20;
    double similarity_threshold = 0.90;
    bool use_local_continuous_distance = true;
    bool lazy_diagnostic_strings = true;
};

struct PopulationGenerationOptions {
    PopulationGenerationMode mode = PopulationGenerationMode::Random;
    bool trace_feasible_gate = true;
    int target_count = 0;
    int max_candidates = 0;
    double max_seconds = 0.0;
    int parent_count = 8;
    double curvature_relative_span = 0.20;
    double air_thickness_relative_span = 0.15;
    double glass_thickness_relative_span = 0.08;
    double curvature_absolute_span = 0.05;
    double air_thickness_absolute_span = 5.0;
    double glass_thickness_absolute_span = 0.5;
    double asphere_sag_perturbation_um = 1.0;
    double asphere_sag_correlation_length = 0.35;
    double geometry_perturbation_fraction = 0.08;
    double max_merit_ratio = 0.0;
    double geometry_child_fraction = 0.0;
    double geometry_local_scale = 0.0;
    bool geometry_single_variable = false;
    double vignetting_child_fraction = 0.0;
    double vignetting_bound_fraction = 0.001;
    bool perturb_material_variables = true;
    bool perturb_other_variables = false;
    double physics_target_ttl = 0.0;
    double physics_target_efl = 0.0;
    double physics_target_bfl = 0.0;
    double physics_ttl_min_fraction = 0.75;
    double physics_ttl_max_fraction = 1.00;
    double physics_power_jitter = 0.15;
    int physics_sa_iterations = 48;
    double physics_sa_temperature = 0.05;
    std::string seed_pool_path;
    std::vector<std::vector<double>> seed_pool_values;
    double seed_pool_fraction = 0.0;
    double seed_pool_perturbation = 0.0;
    bool trace_archive_enabled = true;
    int trace_archive_capacity = 4096;
    double trace_archive_fraction = 1.0;
    double trace_archive_min_distance = 0.02;
    bool adaptive_trigger_enabled = true;
    double adaptive_max_fraction = 0.10;
    double adaptive_min_acceptance_rate = 0.02;
    double adaptive_min_rel_improvement = 0.005;
};


}  // namespace optiland
