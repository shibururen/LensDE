// Print the native score and optical-health summary for initial and optimized systems.
// Command evaluation/publication supplies an already-computed Score to this presentation helper.
// Do not rescore candidates or change numeric state while formatting the console summary.

#include "cli_internal.hpp"

namespace lensde { namespace cli { namespace detail {

void print_score(const std::string& label, const Score& score) {
    std::cout << label << "\n";
    std::cout << "  merit      : " << score.merit << "\n";
    std::cout << "  raw merit  : " << score.uncapped_merit << "\n";
    std::cout << "  sum squares: " << score.merit_sum << "\n";
    std::cout << "  sum weights: " << score.merit_weight_sum << "\n";
    std::cout << "  efl        : " << score.efl << "\n";
    std::cout << "  ttl        : " << score.ttl << "\n";
    std::cout << "  bfl        : " << score.bfl << "\n";
    std::cout << "  working F/#: " << score.working_f_number
              << " (image NA " << score.image_space_numerical_aperture
              << ", marginal rays "
              << score.working_f_number_valid_marginal_rays << "/4)\n";
    std::cout << "  mean spot  : " << score.mean_spot << "\n";
    std::cout << "  max spot   : " << score.max_spot << "\n";
    std::cout << "  max ray spot: " << score.max_single_ray_spot
              << " (ratio " << score.max_single_ray_spot_ratio << ")\n";
    std::cout << "  mean wave  : " << score.mean_wavefront_rms << "\n";
    std::cout << "  max wave   : " << score.max_wavefront_rms << "\n";
    std::cout << "  max ray wave: " << score.max_single_ray_wavefront
              << " (ratio " << score.max_single_ray_wavefront_ratio << ")\n";
    std::cout << "  max distort: " << score.max_distortion
              << " (" << score.max_distortion * 100.0 << "%)"
              << " [" << score.distortion_model << "]\n";
    if (score.distortion_sign_flip_fields > 0) {
        std::cout << "  distort flip: " << score.distortion_sign_flip_fields << " field(s)\n";
    }
    std::cout << "  max CRA deg: " << score.max_chief_ray_angle << "\n";
    std::cout << "  chief bend : " << score.chief_ray_path_bend_max_deg
              << " deg max, excess " << score.chief_ray_path_excess_max << "\n";
    std::cout << "  lateral col: " << score.lateral_color << "\n";
    std::cout << "  min edge   : " << score.min_edge << "\n";
    std::cout << "  coverage   : " << score.trace_coverage
              << " (min field " << score.min_field_coverage
              << ", failed weight " << score.failed_ray_weight << ")\n";
    if (std::isfinite(score.trace_priority_metric) &&
        std::abs(score.trace_priority_penalty) > 1.0e-12) {
        std::cout << "  trace prio : " << score.trace_priority_metric
                  << " (penalty " << score.trace_priority_penalty << ")\n";
    }
    if (score.merit_model == "unified_trace_gate" ||
        score.trace_penalty > 0.0 ||
        score.constraint_penalty > 0.0 ||
        score.first_order_penalty > 0.0 ||
        score.image_penalty > 0.0) {
        std::cout << "  gates      : trace=" << score.trace_gate
                  << " constraint=" << score.constraint_gate
                  << " first-order=" << score.first_order_gate
                  << " image=" << score.image_gate << "\n";
        std::cout << "  groups     : trace=" << score.trace_penalty
                  << " constraint=" << score.constraint_penalty
                  << " first-order=" << score.first_order_penalty
                  << " image=" << score.image_penalty << "\n";
    }
    int shown = 0;
    for (const auto& contribution : score.contributions) {
        if (std::abs(contribution.penalty) <= 1.0e-12) {
            continue;
        }
        if (shown == 0) {
            std::cout << "  merit terms:\n";
        }
        std::cout << "    " << contribution.source << ":" << contribution.name
                  << " value=" << contribution.value
                  << " weight=" << contribution.weight
                  << " penalty=" << contribution.penalty << "\n";
        if (++shown >= 8) {
            std::cout << "    ... see score.json contributions for full breakdown\n";
            break;
        }
    }
}

} } }
