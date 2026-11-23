// Persist island populations, merits, optimizer summaries, and aggregate broker statistics.
// The application invokes these writers after all requested island results are available.
// Keep island/row ordering and existing numeric precision; do not substitute timing for scientific scores.

#include "interfaces.hpp"

namespace lensde::broker {

void save_population(const optiland::OptimizationResult& result, const fs::path& path) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("cannot write " + path.string());
    }
    out << std::setprecision(17);
    for (int i = 0; i < result.final_population_size; ++i) {
        for (int j = 0; j < result.final_population_dim; ++j) {
            if (j > 0) {
                out << ',';
            }
            out << result.final_population_values[
                static_cast<std::size_t>(i) * result.final_population_dim + j];
        }
        out << '\n';
    }
}

void save_merits(const optiland::OptimizationResult& result, const fs::path& path) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("cannot write " + path.string());
    }
    out << std::setprecision(17);
    for (double merit : result.final_population_merits) {
        out << merit << '\n';
    }
}

void save_result(const LoadedJob& loaded, const RunRecord& record) {
    const fs::path output_dir(loaded.job.output_dir);
    fs::create_directories(output_dir);
    if (!record.error.empty()) {
        std::ofstream out(output_dir / "error.txt");
        out << record.error << '\n';
        return;
    }
    save_population(record.result, output_dir / "population.csv");
    save_merits(record.result, output_dir / "merits.csv");
    optiland::LensSystem best_system = optiland::apply_mixed_variables(
        loaded.system,
        loaded.variables,
        record.result.best_values,
        loaded.glass);
    // Materialize the same post-DE aperture state that the Locked stage will
    // consume.  The optimizer's CUDA score is produced from per-candidate
    // device apertures; saving only best_values would otherwise leave
    // system.json on the pre-candidate aperture state and make the next CPU
    // rescore evaluate a different optical system.
    optiland::sync_stop_aperture_to_aiming_radius(best_system);
    optiland::Score materialized_score = record.result.best_score;
    if (loaded.merit.dynamic_apertures) {
        const int aperture_rings =
            loaded.merit.vignetting_merit_aperture_envelope_experimental
                ? std::max(1, loaded.merit.num_rays)
                : (loaded.merit.clear_aperture_rings > 0
                       ? loaded.merit.clear_aperture_rings
                       : loaded.merit.num_rays);
        optiland::update_clear_apertures_from_trace(
            best_system,
            aperture_rings,
            "gaussian_ring_arm",
            loaded.merit.sampling_seed,
            best_system.ray_aiming.mode,
            loaded.merit.effective_clear_aperture_margin(),
            true,
            loaded.merit.clear_aperture_arms,
            false,
            false,
            loaded.merit.vignetting_boundary_support_tolerance,
            loaded.merit.vignetting_boundary_support_selectivity_slope,
            loaded.merit.vignetting_boundary_support_contract ==
                optiland::VignettingBoundarySupportContract::GlobalTrimRealizability,
            loaded.merit.vignetting_physical_cut_match_enabled,
            loaded.merit.vignetting_optimization_enabled());
        materialized_score = optiland::evaluate_merit(best_system, loaded.merit);
    }
    if (!optiland::save_system_json(
            best_system, (output_dir / "system.json").string()) ||
        !optiland::save_score_json(
            materialized_score, (output_dir / "score.json").string())) {
        throw std::runtime_error("cannot save materialized island result");
    }
    const double optimizer_best_merit = record.result.best_score.merit;
    const double materialization_merit_delta =
        materialized_score.merit - optimizer_best_merit;
    const double materialization_merit_relative_difference =
        std::abs(materialization_merit_delta) /
        std::max(
            1.0,
            std::max(
                std::abs(materialized_score.merit),
                std::abs(optimizer_best_merit)));
    constexpr double kCpuCudaMaterializationMeritRelativeTolerance = 5.0e-3;
    const bool cpu_cuda_materialization_consistent =
        std::isfinite(optimizer_best_merit) &&
        std::isfinite(materialization_merit_relative_difference) &&
        materialization_merit_relative_difference <=
            kCpuCudaMaterializationMeritRelativeTolerance;
    const optiland::AdaptiveHybridDeDlsOptions options =
        checkpoint_options(loaded);
    const double materialization_coverage_epsilon =
        options.allow_near_trace_infeasible
            ? std::max(0.0, options.trace_coverage_epsilon)
            : 0.0;
    const double materialization_min_system_coverage = std::max(
        0.0,
        loaded.merit.min_system_coverage - materialization_coverage_epsilon);
    const double materialization_min_field_coverage = std::max(
        0.0,
        loaded.merit.min_field_coverage - materialization_coverage_epsilon);
    const bool materialization_valid =
        cpu_cuda_materialization_consistent &&
        std::isfinite(materialized_score.merit) &&
        materialized_score.merit < loaded.merit.fail_merit &&
        materialized_score.trace_ok &&
        std::isfinite(materialized_score.trace_coverage) &&
        materialized_score.trace_coverage + 1.0e-12 >=
            materialization_min_system_coverage &&
        std::isfinite(materialized_score.min_field_coverage) &&
        materialized_score.min_field_coverage + 1.0e-12 >=
            materialization_min_field_coverage &&
        (!loaded.merit.require_chief_ray ||
         (materialized_score.chief_ray_ok &&
          materialized_score.chief_ray_failed_fields == 0)) &&
        (!loaded.merit.require_off_axis_field_coverage ||
         materialized_score.zero_coverage_off_axis_fields == 0) &&
        std::isfinite(materialized_score.min_edge) &&
        materialized_score.min_edge >= -1.0e-12;
    std::ofstream out(output_dir / "result.json");
    out << std::setprecision(17)
        << "{\n"
        << "  \"island_id\": " << loaded.job.island_id << ",\n"
        << "  \"generations\": " << record.result.generations << ",\n"
        << "  \"profile\": \"" << loaded.job.profile << "\",\n"
        << "  \"internal_scaling\": \"" << loaded.job.internal_scaling << "\",\n"
        << "  \"used_cuda\": " << (record.result.used_cuda ? "true" : "false") << ",\n"
        << "  \"best_merit\": " << materialized_score.merit << ",\n"
        << "  \"optimizer_best_merit\": " << optimizer_best_merit << ",\n"
        << "  \"optimizer_best_min_edge\": " << record.result.best_score.min_edge << ",\n"
        << "  \"materialized_merit\": " << materialized_score.merit << ",\n"
        << "  \"materialized_min_edge\": " << materialized_score.min_edge << ",\n"
        << "  \"materialization_merit_delta\": " << materialization_merit_delta << ",\n"
        << "  \"materialization_merit_relative_difference\": "
        << materialization_merit_relative_difference << ",\n"
        << "  \"materialization_merit_relative_tolerance\": "
        << kCpuCudaMaterializationMeritRelativeTolerance << ",\n"
        << "  \"cpu_cuda_materialization_consistent\": "
        << (cpu_cuda_materialization_consistent ? "true" : "false") << ",\n"
        << "  \"materialization_coverage_epsilon\": "
        << materialization_coverage_epsilon << ",\n"
        << "  \"materialization_min_system_coverage\": "
        << materialization_min_system_coverage << ",\n"
        << "  \"materialization_min_field_coverage\": "
        << materialization_min_field_coverage << ",\n"
        << "  \"materialized_min_chief_progress\": "
        << materialized_score.min_chief_progress << ",\n"
        << "  \"materialized_chief_ray_ok\": "
        << (materialized_score.chief_ray_ok ? "true" : "false") << ",\n"
        << "  \"materialized_chief_ray_failed_fields\": "
        << materialized_score.chief_ray_failed_fields << ",\n"
        << "  \"materialized_zero_coverage_off_axis_fields\": "
        << materialized_score.zero_coverage_off_axis_fields << ",\n"
        << "  \"materialization_valid\": "
        << (materialization_valid ? "true" : "false") << ",\n"
        << "  \"population_size\": " << record.result.final_population_size << ",\n"
        << "  \"dimension\": " << record.result.final_population_dim << ",\n"
        << "  \"glass_group_count\": " << loaded.glass.groups.size() << ",\n"
        << "  \"evaluation_calls\": " << record.result.timing.evaluation_calls << ",\n"
        << "  \"evaluated_candidates\": " << record.result.timing.evaluated_candidates << ",\n"
        << "  \"cuda_calls_owned_by_island\": " << record.result.timing.cuda.calls << ",\n"
        << "  \"dls_iterations\": " << record.result.timing.dls_iterations << ",\n"
        << "  \"dls_accepted_steps\": " << record.result.timing.dls_accepted_steps << ",\n"
        << "  \"dls_rejected_steps\": " << record.result.timing.dls_rejected_steps << ",\n"
        << "  \"dls_jacobian_seconds\": " << record.result.timing.dls_jacobian_seconds << ",\n"
        << "  \"dls_linear_solve_seconds\": " << record.result.timing.dls_linear_solve_seconds << ",\n"
        << "  \"dls_trial_score_seconds\": " << record.result.timing.dls_trial_score_seconds << ",\n"
        << "  \"dls_trial_residual_seconds\": " << record.result.timing.dls_trial_residual_seconds << ",\n"
        << "  \"best_values\": [";
    for (std::size_t i = 0; i < record.result.best_values.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << record.result.best_values[i];
    }
    out << "]\n"
        << "}\n";
}

void save_broker_stats(
    const optiland::BatchedPopulationBrokerStats& stats,
    const fs::path& path) {
    std::ofstream out(path);
    out << std::setprecision(17)
        << "{\n"
        << "  \"logical_requests\": " << stats.logical_requests << ",\n"
        << "  \"physical_launches\": " << stats.physical_launches << ",\n"
        << "  \"merged_requests\": " << stats.merged_requests << ",\n"
        << "  \"candidate_rows\": " << stats.candidate_rows << ",\n"
        << "  \"maximum_batch_rows\": " << stats.maximum_batch_rows << ",\n"
        << "  \"certificate_eligible\": " << stats.certificate_eligible << ",\n"
        << "  \"merit_certificate_eligible\": " << stats.merit_certificate_eligible << ",\n"
        << "  \"trace_feasible_parent_proofs\": " << stats.trace_feasible_parent_proofs << ",\n"
        << "  \"trace_infeasible_parent_proofs\": " << stats.trace_infeasible_parent_proofs << ",\n"
        << "  \"rms_proofs\": " << stats.rms_proofs << ",\n"
        << "  \"certified_rejections\": " << stats.certified_rejections << ",\n"
        << "  \"mean_batch_rows\": "
        << (stats.physical_launches > 0
                ? static_cast<double>(stats.candidate_rows) / stats.physical_launches
                : 0.0) << ",\n"
        << "  \"mean_queue_wait_seconds\": "
        << (stats.logical_requests > 0
                ? stats.queue_wait_seconds / stats.logical_requests
                : 0.0) << ",\n"
        << "  \"broker_wall_seconds\": " << stats.wall_seconds << ",\n"
        << "  \"cuda_calls\": " << stats.cuda.calls << ",\n"
        << "  \"cuda_candidates\": " << stats.cuda.candidates << ",\n"
        << "  \"cuda_total_seconds\": " << stats.cuda.total_seconds << ",\n"
        << "  \"cuda_kernel_seconds\": " << stats.cuda.kernel_seconds << "\n"
        << "}\n";
}

bool save_all_results(const std::vector<LoadedJob>& loaded, const std::vector<RunRecord>& records) {
bool failed = false;
for (std::size_t i = 0; i < loaded.size(); ++i) {
            save_result(loaded[i], records[i]);
            if (!records[i].error.empty()) {
                failed = true;
                std::cerr << "island " << loaded[i].job.island_id << " failed: "
                          << records[i].error << '\n';
            }
        }
    return failed;
}

} // namespace lensde::broker
