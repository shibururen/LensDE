#pragma once
#include "optiland/core/merit_operands.hpp"
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace optiland {
struct MeritContribution {
    std::string source;
    std::string name;
    std::string mode;
    std::size_t index = 0;
    std::size_t term = 0;
    double value = std::numeric_limits<double>::quiet_NaN();
    double target = std::numeric_limits<double>::quiet_NaN();
    double lower = -kInf;
    double upper = kInf;
    double weight = 1.0;
    double field_weight = 1.0;
    double penalty = 0.0;
    MeritCategory category = MeritCategory::Other;
    double tolerance = std::numeric_limits<double>::quiet_NaN();
    double base_weight = 1.0;
    double gate = 1.0;
    double normalized_residual = std::numeric_limits<double>::quiet_NaN();
    double robust_penalty = 0.0;
    // Optional global-trim owner certificate.  NaN keeps these keys absent
    // from unrelated contribution JSON objects.
    double aperture_candidate_radius =
        std::numeric_limits<double>::quiet_NaN();
    double aperture_radius_before_trim =
        std::numeric_limits<double>::quiet_NaN();
    double aperture_protection_radius =
        std::numeric_limits<double>::quiet_NaN();
    double aperture_trim_window =
        std::numeric_limits<double>::quiet_NaN();
};

struct Score {
    std::string merit_model;
    double merit = 1.0e12;
    double uncapped_merit = std::numeric_limits<double>::quiet_NaN();
    double merit_sum = std::numeric_limits<double>::quiet_NaN();
    double merit_weight_sum = std::numeric_limits<double>::quiet_NaN();
    double efl = std::numeric_limits<double>::quiet_NaN();
    double ttl = std::numeric_limits<double>::quiet_NaN();
    double bfl = std::numeric_limits<double>::quiet_NaN();
    double working_f_number = std::numeric_limits<double>::quiet_NaN();
    double image_space_numerical_aperture =
        std::numeric_limits<double>::quiet_NaN();
    int working_f_number_valid_marginal_rays = 0;
    double mean_spot = std::numeric_limits<double>::quiet_NaN();
    double max_spot = std::numeric_limits<double>::quiet_NaN();
    double mean_worst_wavelength_spot = std::numeric_limits<double>::quiet_NaN();
    double max_worst_wavelength_spot = std::numeric_limits<double>::quiet_NaN();
    double mean_spectral_spot_spread = std::numeric_limits<double>::quiet_NaN();
    double max_spectral_spot_spread = std::numeric_limits<double>::quiet_NaN();
    double paraxial_axial_color_focus_shift_mm = std::numeric_limits<double>::quiet_NaN();
    double max_single_ray_spot = std::numeric_limits<double>::quiet_NaN();
    double max_single_ray_spot_ratio = std::numeric_limits<double>::quiet_NaN();
    double mean_wavefront_rms = std::numeric_limits<double>::quiet_NaN();
    double max_wavefront_rms = std::numeric_limits<double>::quiet_NaN();
    double max_single_ray_wavefront = std::numeric_limits<double>::quiet_NaN();
    double max_single_ray_wavefront_ratio = std::numeric_limits<double>::quiet_NaN();
    double max_distortion = std::numeric_limits<double>::quiet_NaN();
    std::string distortion_model = "auto";
    int distortion_sign_flip_fields = 0;
    double max_chief_ray_angle = std::numeric_limits<double>::quiet_NaN();
    double chief_ray_path_bend_mean_deg = std::numeric_limits<double>::quiet_NaN();
    double chief_ray_path_bend_mean_off_axis_deg = std::numeric_limits<double>::quiet_NaN();
    double chief_ray_path_bend_max_deg = std::numeric_limits<double>::quiet_NaN();
    double chief_ray_path_bend_max_step_deg = std::numeric_limits<double>::quiet_NaN();
    double chief_ray_path_excess_mean = std::numeric_limits<double>::quiet_NaN();
    double chief_ray_path_excess_max = std::numeric_limits<double>::quiet_NaN();
    double lateral_color = std::numeric_limits<double>::quiet_NaN();
    double finite_ray_field_focus_spread_mm = std::numeric_limits<double>::quiet_NaN();
    double finite_ray_astig_focus_gap_mm = std::numeric_limits<double>::quiet_NaN();
    double finite_ray_axial_color_focus_shift_mm = std::numeric_limits<double>::quiet_NaN();
    double finite_ray_lateral_color_mm = std::numeric_limits<double>::quiet_NaN();
    double finite_ray_spherical_focus_spread_mm = std::numeric_limits<double>::quiet_NaN();
    double finite_ray_coma_tangential_mm = std::numeric_limits<double>::quiet_NaN();
    double finite_ray_valid_fraction = std::numeric_limits<double>::quiet_NaN();
    double finite_ray_metric_count = 0.0;
    double finite_ray_evidence = 0.0;
    double topology_potential_score_v2 = std::numeric_limits<double>::quiet_NaN();
    double topology_potential_first_order_health_v2 = std::numeric_limits<double>::quiet_NaN();
    double topology_potential_hard_aberration_health_v2 = std::numeric_limits<double>::quiet_NaN();
    double topology_potential_image_health_v2 = std::numeric_limits<double>::quiet_NaN();
    double topology_potential_margin_health_v2 = std::numeric_limits<double>::quiet_NaN();
    double topology_potential_balance_health_v2 = std::numeric_limits<double>::quiet_NaN();
    double topology_potential_trace_health_v2 = std::numeric_limits<double>::quiet_NaN();
    double topology_potential_score_v3 = std::numeric_limits<double>::quiet_NaN();
    double topology_potential_first_order_health_v3 = std::numeric_limits<double>::quiet_NaN();
    double topology_potential_chief_path_health_v3 = std::numeric_limits<double>::quiet_NaN();
    double topology_potential_hard_aberration_health_v3 = std::numeric_limits<double>::quiet_NaN();
    double topology_potential_image_health_v3 = std::numeric_limits<double>::quiet_NaN();
    double topology_potential_margin_health_v3 = std::numeric_limits<double>::quiet_NaN();
    double topology_potential_balance_health_v3 = std::numeric_limits<double>::quiet_NaN();
    double topology_potential_trace_health_v3 = std::numeric_limits<double>::quiet_NaN();
    double min_edge = std::numeric_limits<double>::quiet_NaN();
    double trace_coverage = std::numeric_limits<double>::quiet_NaN();
    double min_field_coverage = std::numeric_limits<double>::quiet_NaN();
    double trace_progress = std::numeric_limits<double>::quiet_NaN();
    double min_trace_progress = std::numeric_limits<double>::quiet_NaN();
    double min_chief_progress = std::numeric_limits<double>::quiet_NaN();
    std::vector<double> relative_illumination_proxy_by_field;
    std::vector<double> relative_illumination_floor_by_field;
    double min_relative_illumination_proxy =
        std::numeric_limits<double>::quiet_NaN();
    double trace_priority_metric = std::numeric_limits<double>::quiet_NaN();
    double trace_priority_penalty = 0.0;
    double trace_group_error = 0.0;
    double constraint_group_error = 0.0;
    double first_order_group_error = 0.0;
    double image_group_error = 0.0;
    double trace_gate = 0.0;
    double constraint_gate = 1.0;
    double first_order_gate = 0.0;
    double image_gate = 0.0;
    double trace_penalty = 0.0;
    double first_order_penalty = 0.0;
    double constraint_penalty = 0.0;
    double image_penalty = 0.0;
    double other_penalty = 0.0;
    double failed_ray_weight = 0.0;
    std::vector<double> dls_ray_spot_residuals;
    std::vector<double> dls_single_ray_spot_guard_residuals;
    std::vector<double> dls_single_ray_wavefront_guard_residuals;
    bool chief_ray_ok = true;
    int chief_ray_failed_fields = 0;
    int zero_coverage_off_axis_fields = 0;
    int ray_path_axis_cross_fields = 0;
    int center_early_focus_fields = 0;
    int chief_entry_side_failed_fields = 0;
    int ray_path_structural_failure_fields = 0;
    int max_axis_cross_count = 0;
    double min_pupil_order_jacobian = std::numeric_limits<double>::quiet_NaN();
    double pupil_order_worst_surface_progress =
        std::numeric_limits<double>::quiet_NaN();
    double pupil_order_prior_penalty = 0.0;
    int pupil_order_fold_fields = 0;
    int pupil_order_hard_fold_fields = 0;
    bool trace_ok = true;
    std::string message;
    std::vector<MeritContribution> contributions;
};


}  // namespace optiland
