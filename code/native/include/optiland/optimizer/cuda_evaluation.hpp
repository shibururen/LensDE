#pragma once
#include "optiland/core.hpp"
#include <cstdint>
#include <vector>

namespace optiland {
struct CudaEvaluationTiming {
    int calls = 0;
    long long candidates = 0;
    double context_seconds = 0.0;
    double host_setup_seconds = 0.0;
    double host_to_device_seconds = 0.0;
    double kernel_seconds = 0.0;
    double device_to_host_seconds = 0.0;
    double cleanup_seconds = 0.0;
    double total_seconds = 0.0;
};

struct CudaScoreSummary {
    double merit;
    double uncapped_merit;
    double merit_sum;
    double merit_weight_sum;
    double efl;
    double ttl;
    double bfl;
    double working_f_number;
    double image_space_numerical_aperture;
    int working_f_number_valid_marginal_rays;
    double mean_spot;
    double max_spot;
    double max_single_ray_spot;
    double max_single_ray_spot_ratio;
    double mean_wavefront_rms;
    double max_wavefront_rms;
    double max_single_ray_wavefront;
    double max_single_ray_wavefront_ratio;
    double max_distortion;
    int distortion_sign_flip_fields;
    double max_chief_ray_angle;
    double lateral_color;
    double paraxial_axial_color_focus_shift_mm;
    double min_edge;
    double trace_coverage;
    double min_field_coverage;
    double trace_progress;
    double min_trace_progress;
    double min_chief_progress;
    double min_relative_illumination_proxy;
    double trace_priority_metric;
    double trace_priority_penalty;
    double trace_group_error;
    double constraint_group_error;
    double first_order_group_error;
    double image_group_error;
    double trace_gate;
    double constraint_gate;
    double first_order_gate;
    double image_gate;
    double trace_penalty;
    double constraint_penalty;
    double first_order_penalty;
    double image_penalty;
    double other_penalty;
    double failed_ray_weight;
    int chief_ray_ok;
    int chief_ray_failed_fields;
    int zero_coverage_off_axis_fields;
    int ray_path_axis_cross_fields;
    int center_early_focus_fields;
    int chief_entry_side_failed_fields;
    int ray_path_structural_failure_fields;
    int max_axis_cross_count;
    double min_pupil_order_jacobian;
    double pupil_order_worst_surface_progress;
    double pupil_order_prior_penalty;
    int pupil_order_fold_fields;
    int pupil_order_hard_fold_fields;
    int trace_ok;
};

struct PopulationEvaluationResult {
    bool success = false;
    bool used_cuda = false;
    std::vector<double> merits;
    std::vector<CudaScoreSummary> summaries;
    std::string error;
};

// Optional evaluation seam used by the multi-island experiment.  The
// optimizer's random stream, mutation, selection, and population state remain
// private to each DifferentialEvolution instance; only already-generated
// physical candidate rows cross this interface.
class PopulationEvaluationBroker {
public:
    virtual ~PopulationEvaluationBroker() = default;
    // Custom brokers opt in only when they transport per-request proof state.
    virtual bool supports_evaluation_certificates() const { return false; }

    virtual PopulationEvaluationResult evaluate(
        int island_index,
        std::uint64_t logical_call_index,
        const std::vector<double>& physical_population,
        int population_size,
        bool need_score_summaries) = 0;
};


}  // namespace optiland
