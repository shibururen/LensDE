// Write candidate score CSV rows from cached fitness or explicit per-candidate evaluation.
// Population-only scoring and optimizer result publication use the same column contract.
// Keep ranking/row identity, evaluation order, and the existing 17-digit numeric output unchanged.

#include "cli_internal.hpp"

namespace lensde { namespace cli { namespace detail {

bool save_population_scores_csv(
    const OptimizationResult& result,
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const GlassSearchContext& glass_search,
    const MeritConfig& merit,
    const std::string& path,
    bool prefer_cuda ,
    CudaEvaluationTiming* cuda_timing ,
    bool* used_cuda ) {
    if (result.final_population_size <= 0 || result.final_population_values.empty()) {
        return false;
    }
    if (used_cuda) {
        *used_cuda = false;
    }
    const int rows = result.final_population_size;
    const int dim = static_cast<int>(variables.size());
    const bool has_fitness =
        static_cast<int>(result.final_population_fitness.size()) >= rows;
    if (std::getenv("OPTILAND_DE_CUDA_SUPPORT_DEBUG")) {
        std::cerr << "DE-CUDA score CSV gate: rows=" << rows
                  << " has_fitness=" << (has_fitness ? 1 : 0)
                  << " prefer_cuda=" << (prefer_cuda ? 1 : 0)
                  << " distortion_allowed="
                  << (distortion_model_allows_cuda_local(merit) ? 1 : 0)
                  << " cuda_available="
                  << (evaluate_population_cuda_available() ? 1 : 0)
                  << " path='" << path << "'"
                  << "\n";
    }
    std::ofstream out(path);
    if (!out) {
        if (std::getenv("OPTILAND_DE_CUDA_SUPPORT_DEBUG")) {
            std::cerr << "DE-CUDA score CSV open failed: " << path << "\n";
        }
        return false;
    }
    out.precision(17);
    if (has_fitness) {
        out << "index,rank,topology_key,structural_topology_key,semantic_key,descriptor_key,"
                "continuous_topology_signature,avoided_topology,avoidance_penalty,"
                "avoidance_match_channel,avoidance_match_score,"
                "objective,total_violation,feasible,trace_feasible,constraint_feasible,"
               "first_order_feasible,trace_violation,constraint_violation,first_order_violation,"
               "image_objective,merit,trace_ok,trace_coverage,min_field_coverage,"
               "min_trace_progress,min_chief_progress,min_relative_illumination_proxy,"
               "trace_gate,constraint_gate,"
               "first_order_gate,image_gate,trace_penalty,constraint_penalty,"
               "first_order_penalty,image_penalty,efl,ttl,bfl,mean_spot,max_spot,"
               "mean_wavefront_rms,max_wavefront_rms,"
               "max_distortion,distortion_sign_flip_fields,min_edge,"
               "chief_ray_failed_fields,zero_coverage_off_axis_fields,"
               "ray_path_axis_cross_fields,center_early_focus_fields,"
               "chief_entry_side_failed_fields,ray_path_structural_failure_fields,"
               "max_axis_cross_count,min_pupil_order_jacobian,"
               "pupil_order_worst_surface_progress,pupil_order_prior_penalty,"
               "pupil_order_fold_fields,pupil_order_hard_fold_fields,"
               "max_chief_ray_angle,chief_ray_path_bend_mean_deg,"
               "chief_ray_path_bend_mean_off_axis_deg,chief_ray_path_bend_max_deg,"
               "chief_ray_path_bend_max_step_deg,chief_ray_path_excess_mean,"
               "chief_ray_path_excess_max,lateral_color,finite_ray_field_focus_spread_mm,"
               "finite_ray_astig_focus_gap_mm,finite_ray_axial_color_focus_shift_mm,"
               "finite_ray_lateral_color_mm,finite_ray_spherical_focus_spread_mm,"
               "finite_ray_coma_tangential_mm,finite_ray_valid_fraction,"
               "finite_ray_metric_count,finite_ray_evidence,topology_potential_score_v2,"
               "topology_potential_first_order_health_v2,"
               "topology_potential_hard_aberration_health_v2,"
               "topology_potential_image_health_v2,topology_potential_margin_health_v2,"
               "topology_potential_balance_health_v2,topology_potential_trace_health_v2,"
               "topology_potential_score_v3,topology_potential_first_order_health_v3,"
               "topology_potential_chief_path_health_v3,"
               "topology_potential_hard_aberration_health_v3,"
               "topology_potential_image_health_v3,topology_potential_margin_health_v3,"
               "topology_potential_balance_health_v3,topology_potential_trace_health_v3,chief_reference_contract\n";
    } else {
        out << "index,merit,trace_ok,trace_coverage,min_field_coverage,min_trace_progress,min_chief_progress,"
               "min_relative_illumination_proxy,"
               "trace_gate,constraint_gate,first_order_gate,image_gate,"
               "trace_penalty,constraint_penalty,first_order_penalty,image_penalty,"
               "efl,ttl,bfl,mean_spot,max_spot,mean_wavefront_rms,max_wavefront_rms,"
               "max_distortion,distortion_sign_flip_fields,"
               "min_edge,chief_ray_failed_fields,zero_coverage_off_axis_fields,"
               "ray_path_axis_cross_fields,center_early_focus_fields,"
               "chief_entry_side_failed_fields,ray_path_structural_failure_fields,"
               "max_axis_cross_count,min_pupil_order_jacobian,"
               "pupil_order_worst_surface_progress,pupil_order_prior_penalty,"
               "pupil_order_fold_fields,pupil_order_hard_fold_fields,"
               "max_chief_ray_angle,chief_ray_path_bend_mean_deg,"
               "chief_ray_path_bend_mean_off_axis_deg,chief_ray_path_bend_max_deg,"
               "chief_ray_path_bend_max_step_deg,chief_ray_path_excess_mean,"
               "chief_ray_path_excess_max,lateral_color,finite_ray_field_focus_spread_mm,"
               "finite_ray_astig_focus_gap_mm,finite_ray_axial_color_focus_shift_mm,"
               "finite_ray_lateral_color_mm,finite_ray_spherical_focus_spread_mm,"
               "finite_ray_coma_tangential_mm,finite_ray_valid_fraction,"
               "finite_ray_metric_count,finite_ray_evidence,topology_potential_score_v2,"
               "topology_potential_first_order_health_v2,"
               "topology_potential_hard_aberration_health_v2,"
               "topology_potential_image_health_v2,topology_potential_margin_health_v2,"
               "topology_potential_balance_health_v2,topology_potential_trace_health_v2,"
               "topology_potential_score_v3,topology_potential_first_order_health_v3,"
               "topology_potential_chief_path_health_v3,"
               "topology_potential_hard_aberration_health_v3,"
               "topology_potential_image_health_v3,topology_potential_margin_health_v3,"
               "topology_potential_balance_health_v3,topology_potential_trace_health_v3,chief_reference_contract\n";
    }
    auto write_number = [&](double value) {
        if (std::isfinite(value)) {
            out << value;
        }
    };
    std::vector<int> order(static_cast<std::size_t>(rows));
    std::iota(order.begin(), order.end(), 0);
    if (has_fitness) {
        auto unified_scalar_merit_fitness = [](const CandidateFitness& fitness) {
            std::string model = fitness.score.merit_model;
            std::transform(model.begin(), model.end(), model.begin(), [](unsigned char ch) {
                if (ch == '-' || std::isspace(ch)) {
                    return '_';
                }
                return static_cast<char>(std::tolower(ch));
            });
            return model == "unified_trace_gate" ||
                   model == "trace_guarded_adaptive" ||
                   model == "unified_adaptive" ||
                   model == "trace_guarded";
        };
        auto finite_prefer_less = [](double a, double b, double tolerance = 1.0e-12) {
            const bool fa = std::isfinite(a);
            const bool fb = std::isfinite(b);
            if (fa != fb) {
                return fa;
            }
            if (!fa && !fb) {
                return false;
            }
            return a + tolerance < b;
        };
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            const CandidateFitness& fa =
                result.final_population_fitness[static_cast<std::size_t>(a)];
            const CandidateFitness& fb =
                result.final_population_fitness[static_cast<std::size_t>(b)];
            if (unified_scalar_merit_fitness(fa) || unified_scalar_merit_fitness(fb)) {
                if (fa.trace_feasible != fb.trace_feasible) {
                    return fa.trace_feasible;
                }
                if (!fa.trace_feasible &&
                    (finite_prefer_less(fa.trace_violation, fb.trace_violation) ||
                     finite_prefer_less(fb.trace_violation, fa.trace_violation))) {
                    return finite_prefer_less(fa.trace_violation, fb.trace_violation);
                }
                if (finite_prefer_less(fa.score.merit, fb.score.merit) ||
                    finite_prefer_less(fb.score.merit, fa.score.merit)) {
                    return finite_prefer_less(fa.score.merit, fb.score.merit);
                }
                if (finite_prefer_less(fa.total_violation, fb.total_violation) ||
                    finite_prefer_less(fb.total_violation, fa.total_violation)) {
                    return finite_prefer_less(fa.total_violation, fb.total_violation);
                }
                return finite_prefer_less(fa.objective, fb.objective);
            }
            if (fa.feasible != fb.feasible) {
                return fa.feasible;
            }
            if (fa.trace_feasible != fb.trace_feasible) {
                return fa.trace_feasible;
            }
            if (fa.constraint_feasible != fb.constraint_feasible) {
                return fa.constraint_feasible;
            }
            if (fa.first_order_feasible != fb.first_order_feasible) {
                return fa.first_order_feasible;
            }
            if (std::isfinite(fa.total_violation) && std::isfinite(fb.total_violation) &&
                std::abs(fa.total_violation - fb.total_violation) > 1.0e-12) {
                return fa.total_violation < fb.total_violation;
            }
            if (std::isfinite(fa.objective) != std::isfinite(fb.objective)) {
                return std::isfinite(fa.objective);
            }
            if (std::isfinite(fa.objective) && std::isfinite(fb.objective) &&
                std::abs(fa.objective - fb.objective) > 1.0e-12) {
                return fa.objective < fb.objective;
            }
            return fa.score.merit < fb.score.merit;
        });
    }
    if (!has_fitness && prefer_cuda && distortion_model_allows_cuda_local(merit) &&
        evaluate_population_cuda_available()) {
        std::vector<double> cuda_merits;
        std::vector<CudaScoreSummary> cuda_summaries;
        if (evaluate_population_cuda_scores(
                base,
                variables,
                merit,
                result.final_population_values,
                rows,
                &cuda_merits,
                &cuda_summaries,
                cuda_timing,
                &glass_search) &&
            static_cast<int>(cuda_summaries.size()) == rows) {
            if (used_cuda) {
                *used_cuda = true;
            }
            for (int rank = 0; rank < rows; ++rank) {
                const int i = order[static_cast<std::size_t>(rank)];
                const CudaScoreSummary& summary = cuda_summaries[static_cast<std::size_t>(i)];
                out << i << ',';
                write_number(i < static_cast<int>(cuda_merits.size())
                                 ? cuda_merits[static_cast<std::size_t>(i)]
                                 : summary.merit);
                out << ',' << (summary.trace_ok ? 1 : 0) << ',';
                write_number(summary.trace_coverage);
                out << ',';
                write_number(summary.min_field_coverage);
                out << ',';
                write_number(summary.min_trace_progress);
                out << ',';
                write_number(summary.min_chief_progress);
                out << ',';
                write_number(summary.min_relative_illumination_proxy);
                out << ',';
                write_number(summary.trace_gate);
                out << ',';
                write_number(summary.constraint_gate);
                out << ',';
                write_number(summary.first_order_gate);
                out << ',';
                write_number(summary.image_gate);
                out << ',';
                write_number(summary.trace_penalty);
                out << ',';
                write_number(summary.constraint_penalty);
                out << ',';
                write_number(summary.first_order_penalty);
                out << ',';
                write_number(summary.image_penalty);
                out << ',';
                write_number(summary.efl);
                out << ',';
                write_number(summary.ttl);
                out << ',';
                write_number(summary.bfl);
                out << ',';
                write_number(summary.mean_spot);
                out << ',';
                write_number(summary.max_spot);
                out << ',';
                write_number(summary.mean_wavefront_rms);
                out << ',';
                write_number(summary.max_wavefront_rms);
                out << ',';
                write_number(summary.max_distortion);
                out << ',' << summary.distortion_sign_flip_fields << ',';
                write_number(summary.min_edge);
                out << ',' << summary.chief_ray_failed_fields
                    << ',' << summary.zero_coverage_off_axis_fields
                    << ',' << summary.ray_path_axis_cross_fields
                    << ',' << summary.center_early_focus_fields
                    << ',' << summary.chief_entry_side_failed_fields
                    << ',' << summary.ray_path_structural_failure_fields
                    << ',' << summary.max_axis_cross_count << ',';
                write_number(summary.min_pupil_order_jacobian);
                out << ',';
                write_number(summary.pupil_order_worst_surface_progress);
                out << ',';
                write_number(summary.pupil_order_prior_penalty);
                out << ',' << summary.pupil_order_fold_fields
                    << ',' << summary.pupil_order_hard_fold_fields << ',';
                write_number(summary.max_chief_ray_angle);
                out << ",,,,,,,";
                write_number(summary.lateral_color);
                for (int blank = 0; blank < 24; ++blank) {
                    out << ',';
                }
                out << ',' << kChiefReferenceContract << '\n';
            }
            return true;
        }
        if (de_cuda_required_local()) {
            std::cerr << "DE-CUDA required, but CUDA score evaluation rejected this population\n";
            return false;
        }
    } else if (prefer_cuda && de_cuda_required_local()) {
        std::cerr << "DE-CUDA required, but CUDA score evaluation is unavailable for this merit/config\n";
        return false;
    }
    std::vector<Score> cpu_scores;
    if (!has_fitness) {
        cpu_scores = evaluate_population_scores_cpu(
            result,
            base,
            variables,
            glass_search,
            merit);
    }
    for (int rank = 0; rank < rows; ++rank) {
        const int i = order[static_cast<std::size_t>(rank)];
        Score score;
        if (!cpu_scores.empty() && i < static_cast<int>(cpu_scores.size())) {
            score = cpu_scores[static_cast<std::size_t>(i)];
        } else if (has_fitness) {
            score = result.final_population_fitness[static_cast<std::size_t>(i)].score;
        } else if (i < static_cast<int>(result.final_population_merits.size())) {
            score.merit = result.final_population_merits[static_cast<std::size_t>(i)];
        }
        out << i << ',';
        if (has_fitness) {
            const CandidateFitness& fit =
                result.final_population_fitness[static_cast<std::size_t>(i)];
            out << (rank + 1) << ','
                << json_escape_local(fit.topology_key) << ','
                << json_escape_local(fit.topology_key) << ','
                << json_escape_local(fit.semantic_key) << ','
                << json_escape_local(fit.descriptor_key) << ','
                << json_escape_local(fit.continuous_topology_signature) << ','
                << (fit.avoided_topology ? 1 : 0) << ',';
            write_number(fit.avoidance_penalty);
            out << ',' << json_escape_local(fit.avoidance_match_channel) << ',';
            write_number(fit.avoidance_match_score);
            out << ',';
            write_number(fit.objective);
            out << ',';
            write_number(fit.total_violation);
            out << ',' << (fit.feasible ? 1 : 0)
                << ',' << (fit.trace_feasible ? 1 : 0)
                << ',' << (fit.constraint_feasible ? 1 : 0)
                << ',' << (fit.first_order_feasible ? 1 : 0) << ',';
            write_number(fit.trace_violation);
            out << ',';
            write_number(fit.constraint_violation);
            out << ',';
            write_number(fit.first_order_violation);
            out << ',';
            write_number(fit.image_objective);
            out << ',';
        }
        write_number(score.merit);
        out << ',' << (score.trace_ok ? 1 : 0) << ',';
        write_number(score.trace_coverage);
        out << ',';
        write_number(score.min_field_coverage);
        out << ',';
        write_number(score.min_trace_progress);
        out << ',';
        write_number(score.min_chief_progress);
        out << ',';
        write_number(score.min_relative_illumination_proxy);
        out << ',';
        write_number(score.trace_gate);
        out << ',';
        write_number(score.constraint_gate);
        out << ',';
        write_number(score.first_order_gate);
        out << ',';
        write_number(score.image_gate);
        out << ',';
        write_number(score.trace_penalty);
        out << ',';
        write_number(score.constraint_penalty);
        out << ',';
        write_number(score.first_order_penalty);
        out << ',';
        write_number(score.image_penalty);
        out << ',';
        write_number(score.efl);
        out << ',';
        write_number(score.ttl);
        out << ',';
        write_number(score.bfl);
        out << ',';
        write_number(score.mean_spot);
        out << ',';
        write_number(score.max_spot);
        out << ',';
        write_number(score.mean_wavefront_rms);
        out << ',';
        write_number(score.max_wavefront_rms);
        out << ',';
        write_number(score.max_distortion);
        out << ',' << score.distortion_sign_flip_fields << ',';
        write_number(score.min_edge);
        out << ',' << score.chief_ray_failed_fields
            << ',' << score.zero_coverage_off_axis_fields
            << ',' << score.ray_path_axis_cross_fields
            << ',' << score.center_early_focus_fields
            << ',' << score.chief_entry_side_failed_fields
            << ',' << score.ray_path_structural_failure_fields
            << ',' << score.max_axis_cross_count << ',';
        write_number(score.min_pupil_order_jacobian);
        out << ',';
        write_number(score.pupil_order_worst_surface_progress);
        out << ',';
        write_number(score.pupil_order_prior_penalty);
        out << ',' << score.pupil_order_fold_fields
            << ',' << score.pupil_order_hard_fold_fields << ',';
        write_number(score.max_chief_ray_angle);
        out << ',';
        write_number(score.chief_ray_path_bend_mean_deg);
        out << ',';
        write_number(score.chief_ray_path_bend_mean_off_axis_deg);
        out << ',';
        write_number(score.chief_ray_path_bend_max_deg);
        out << ',';
        write_number(score.chief_ray_path_bend_max_step_deg);
        out << ',';
        write_number(score.chief_ray_path_excess_mean);
        out << ',';
        write_number(score.chief_ray_path_excess_max);
        out << ',';
        write_number(score.lateral_color);
        out << ',';
        write_number(score.finite_ray_field_focus_spread_mm);
        out << ',';
        write_number(score.finite_ray_astig_focus_gap_mm);
        out << ',';
        write_number(score.finite_ray_axial_color_focus_shift_mm);
        out << ',';
        write_number(score.finite_ray_lateral_color_mm);
        out << ',';
        write_number(score.finite_ray_spherical_focus_spread_mm);
        out << ',';
        write_number(score.finite_ray_coma_tangential_mm);
        out << ',';
        write_number(score.finite_ray_valid_fraction);
        out << ',';
        write_number(score.finite_ray_metric_count);
        out << ',';
        write_number(score.finite_ray_evidence);
        out << ',';
        write_number(score.topology_potential_score_v2);
        out << ',';
        write_number(score.topology_potential_first_order_health_v2);
        out << ',';
        write_number(score.topology_potential_hard_aberration_health_v2);
        out << ',';
        write_number(score.topology_potential_image_health_v2);
        out << ',';
        write_number(score.topology_potential_margin_health_v2);
        out << ',';
        write_number(score.topology_potential_balance_health_v2);
        out << ',';
        write_number(score.topology_potential_trace_health_v2);
        out << ',';
        write_number(score.topology_potential_score_v3);
        out << ',';
        write_number(score.topology_potential_first_order_health_v3);
        out << ',';
        write_number(score.topology_potential_chief_path_health_v3);
        out << ',';
        write_number(score.topology_potential_hard_aberration_health_v3);
        out << ',';
        write_number(score.topology_potential_image_health_v3);
        out << ',';
        write_number(score.topology_potential_margin_health_v3);
        out << ',';
        write_number(score.topology_potential_balance_health_v3);
        out << ',';
        write_number(score.topology_potential_trace_health_v3);
        out << ',' << kChiefReferenceContract << '\n';
    }
    return true;
}

} } }
