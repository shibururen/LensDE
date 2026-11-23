#pragma once
#include "cli_internal.hpp"

namespace lensde { namespace cli { namespace detail {

struct CommandContext {
    CommandContext(int count, char** values) : argc(count), argv(values) {}
    int argc;
    char** argv;

    LensSystem lens{};
    std::string candidate_path{};
    std::string system_path{};
    bool loaded_external_system{};
    AsphereRootPolicy optimizer_asphere_root_policy{};
    bool update_clear_apertures{};
    bool input_clear_apertures_materialized{};
    bool update_input_clear_apertures{};
    bool clear_aperture_rings_explicit{};
    int clear_aperture_rings{};
    MeritConfig merit{};
    bool run_adaptive_hybrid{};
    bool run_hybrid{};
    bool score_population_only{};
    int score_population_detail_index{};
    bool select_seed_population_only{};
    bool run_de{};
    bool run_local{};
    bool run_quick_focus{};
    bool run_polish{};
    bool allow_cuda_for_merit{};
    int population{};
    int popsize{};
    int generations{};
    int de_cuda_min_batch_size{};
    std::string out_dir{};
    bool score_only_output{};
    bool chief_aiming_probe{};
    bool three_step_chief_probe{};
    bool calibrated_edge_initial_probe{};
    bool center_edge_iterative_probe{};
    int chief_aiming_probe_iterations{};
    bool internal_scaling_explicit{};
    InternalScalingMode internal_scaling{};
    AsphereReferencePolicy asphere_reference_policy{};
    LocalBoundsMode local_bounds_mode{};
    int spot_num_rays{};
    int spot_pupil_arms{};
    std::string spot_distribution{};
    unsigned int spot_sampling_seed{};
    bool relative_illumination_requested{};
    RelativeIlluminationConfig relative_illumination_config{};
    Score initial_score{};
    Score final_optimizer_score{};
    bool final_optimizer_score_available{};
    LensSystem optimizer_input_lens{};

    int dispatch_queries();
    int load_system();
    int configure_calibrated_aiming();
    int configure_three_step_aiming();
    int configure_aperture_contract();
    int load_merit();
    int override_merit();
    int prepare_vignetting();
    int configure_execution();
    int prepare_output();
    int select_seed_population();
    int configure_variables();
    int evaluate_initial_system();
    int optimize();
    int publish();
    int execute();
};

} } }
