// Evaluate the prepared initial system and publish final scientific and diagnostic outputs.
// The command pipeline hands its optimizer input and validated final score across this boundary.
// Keep initial/final score ownership, root-policy changes, and optional output sampling distinct.

#include "command_context.hpp"

namespace lensde { namespace cli { namespace detail {

int CommandContext::evaluate_initial_system() {
    initial_score = evaluate_merit(lens, merit);
    print_score("Initial system", initial_score);
    save_score_json(initial_score, join_path(out_dir, "initial_score.json"));
    // final_optimizer_score is owned by this command stage.
    final_optimizer_score_available = false;
    if (chief_aiming_probe) {
        const std::string probe_path = join_path(out_dir, "chief_aiming_probe.csv");
        save_chief_aiming_probe_csv(lens, probe_path, chief_aiming_probe_iterations);
        std::cout << "Wrote " << probe_path << "\n";
    }
    if (three_step_chief_probe) {
        const std::string probe_path = join_path(out_dir, "three_step_chief_probe.csv");
        save_three_step_chief_probe_csv(lens, probe_path);
        std::cout << "Wrote " << probe_path << "\n";
    }
    if (calibrated_edge_initial_probe) {
        const std::string probe_path = join_path(out_dir, "calibrated_edge_initial_probe.csv");
        save_calibrated_edge_initial_probe_csv(lens, probe_path);
        std::cout << "Wrote " << probe_path << "\n";
    }
    if (center_edge_iterative_probe) {
        const std::string probe_path = join_path(out_dir, "center_edge_iterative_probe.csv");
        save_center_edge_iterative_probe_csv(lens, probe_path);
        std::cout << "Wrote " << probe_path << "\n";
    }
    if (run_quick_focus) {
        auto focused = quick_focus(lens, merit, 2.0, 30.0, 80);
        lens = focused.system;
        print_score("After quick focus", focused.score);
    }
    if (run_de) {
        set_asphere_root_policy(optimizer_asphere_root_policy);
        std::cout << "Asphere DE scoring root policy: "
                  << (optimizer_asphere_root_policy == AsphereRootPolicy::SlopeDomainTopologySeed
                          ? "production_slope_domain_topology_seed (shared checkpoint/final contract)"
                          : (optimizer_asphere_root_policy == AsphereRootPolicy::FastRecover
                          ? "fast_recover (certified checkpoint/final rescore)"
                          : (optimizer_asphere_root_policy == AsphereRootPolicy::CertifiedFirstRoot
                                 ? "certified_first_root"
                                 : (optimizer_asphere_root_policy ==
                                            AsphereRootPolicy::UnifiedCertifiedExperimental
                                        ? "unified_certified_experimental (shared checkpoint/final contract)"
                                        : "confirmed_with_fallback"))))
                  << "\n";
    }
    optimizer_input_lens = lens;
    Score hammer_final_guard_score;
    bool hammer_final_guard_score_valid = false;
    return kContinueCommand;
}

int CommandContext::publish() {
    if (!score_only_output) {
        save_layout_svg(lens, join_path(out_dir, "layout.svg"), 2);
        save_spot_csv(
            lens,
            join_path(out_dir, "spot.csv"),
            spot_num_rays,
            spot_distribution,
            spot_sampling_seed,
            spot_pupil_arms);
        save_raytrace_csv(
            lens,
            join_path(out_dir, "raytrace.csv"),
            merit.num_rays,
            merit.pupil_distribution,
            merit.sampling_seed,
            lens.ray_aiming.mode,
            merit.pupil_arms);
    }
    if (relative_illumination_requested) {
        const RelativeIlluminationResult illumination =
            relative_illumination(lens, relative_illumination_config);
        const std::string illumination_path =
            join_path(out_dir, "relative_illumination.csv");
        if (!save_relative_illumination_csv(illumination, illumination_path)) {
            std::cerr << "Failed to write " << illumination_path << "\n";
            return 4;
        }
        std::cout << "Wrote " << illumination_path << "\n";
    }
    save_system_json(lens, join_path(out_dir, "system.json"));
    save_score_json(initial_score, join_path(out_dir, "initial_score.json"));
    save_score_json(
        final_optimizer_score_available ? final_optimizer_score : evaluate_merit(lens, merit),
        join_path(out_dir, "score.json"));
    if (score_only_output) {
        std::cout << "Wrote " << join_path(out_dir, "system.json") << ", "
                  << join_path(out_dir, "initial_score.json") << " and "
                  << join_path(out_dir, "score.json") << "\n";
    } else {
        std::cout << "Wrote " << join_path(out_dir, "layout.svg") << ", "
                  << join_path(out_dir, "spot.csv") << ", "
                  << join_path(out_dir, "raytrace.csv") << ", "
                  << join_path(out_dir, "system.json") << ", "
                  << join_path(out_dir, "initial_score.json") << " and "
                  << join_path(out_dir, "score.json") << "\n";
    }
    print_ray_aiming_diagnostics(out_dir);
    return 0;
    return kContinueCommand;
}

} } }
