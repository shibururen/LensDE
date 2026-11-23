// Serialize the optimizer basin archive and its topology, eligibility, and variable records.
// Adaptive result publication uses these CSV/JSON writers after final basin selection.
// Keep archive order, column meanings, and the existing high-precision numeric serialization.

#include "cli_internal.hpp"

namespace lensde { namespace cli { namespace detail {

bool save_basin_archive_csv(const OptimizationResult& result, const std::string& path) {
    if (result.basin_elites.empty()) {
        return false;
    }
    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out.precision(17);
    out << "id,rank,source,topology_key,structural_topology_key,semantic_key,"
           "descriptor_key,descriptor_diagnostic,continuous_topology_signature,"
           "avoided_topology,avoidance_penalty,avoidance_match_channel,"
           "avoidance_match_score,"
           "similarity_family_id,similarity_family_key,similarity_to_family,"
           "generation,polished,objective,total_violation,"
           "feasible,trace_feasible,constraint_feasible,first_order_feasible,"
           "trace_violation,constraint_violation,first_order_violation,"
           "merit,trace_coverage,min_field_coverage,efl,ttl,bfl,mean_spot,max_spot,"
           "mean_wavefront_rms,max_wavefront_rms,"
           "max_distortion,max_chief_ray_angle,chief_ray_path_bend_mean_deg,"
           "chief_ray_path_bend_mean_off_axis_deg,chief_ray_path_bend_max_deg,"
           "chief_ray_path_bend_max_step_deg,chief_ray_path_excess_mean,"
           "chief_ray_path_excess_max,lateral_color,"
           "finite_ray_field_focus_spread_mm,finite_ray_astig_focus_gap_mm,"
           "finite_ray_axial_color_focus_shift_mm,finite_ray_lateral_color_mm,"
           "finite_ray_spherical_focus_spread_mm,finite_ray_coma_tangential_mm,"
           "finite_ray_evidence,topology_potential_score_v2,"
           "topology_potential_first_order_health_v2,"
           "topology_potential_hard_aberration_health_v2,"
           "topology_potential_image_health_v2,topology_potential_score_v3,"
           "topology_potential_first_order_health_v3,"
           "topology_potential_chief_path_health_v3,"
           "topology_potential_hard_aberration_health_v3,"
           "topology_potential_image_health_v3,min_edge,nearest_distance,"
           "active_eligible,active_reject_reason\n";
    auto write_number = [&](double value) {
        if (std::isfinite(value)) {
            out << value;
        }
    };
    for (std::size_t i = 0; i < result.basin_elites.size(); ++i) {
        const BasinElite& basin = result.basin_elites[i];
        const CandidateFitness& fitness = basin.fitness;
        const Score& score = fitness.score;
        const std::string topology_key =
            basin.topology_key.empty() ? fitness.topology_key : basin.topology_key;
        const std::string semantic_key =
            basin.semantic_key.empty() ? fitness.semantic_key : basin.semantic_key;
        const std::string descriptor_key =
            basin.descriptor_key.empty() ? fitness.descriptor_key : basin.descriptor_key;
        const std::string descriptor_diagnostic =
            basin.descriptor_diagnostic.empty()
                ? fitness.descriptor_diagnostic
                : basin.descriptor_diagnostic;
        out << basin.id << ',' << (i + 1) << ','
            << json_escape_local(basin.source.empty() ? fitness.source : basin.source) << ','
            << json_escape_local(topology_key) << ','
            << json_escape_local(topology_key) << ','
            << json_escape_local(semantic_key) << ','
            << json_escape_local(descriptor_key) << ','
            << json_escape_local(descriptor_diagnostic) << ','
            << json_escape_local(fitness.continuous_topology_signature) << ','
            << (fitness.avoided_topology ? 1 : 0) << ',';
        write_number(fitness.avoidance_penalty);
        out << ',' << json_escape_local(fitness.avoidance_match_channel) << ',';
        write_number(fitness.avoidance_match_score);
        out << ',' << basin.similarity_family_id << ','
            << json_escape_local(basin.similarity_family_key) << ',';
        write_number(basin.similarity_to_family);
        out << ','
            << basin.generation << ','
            << (fitness.polished ? 1 : 0) << ',';
        write_number(fitness.objective);
        out << ',';
        write_number(fitness.total_violation);
        out << ',' << (fitness.feasible ? 1 : 0)
            << ',' << (fitness.trace_feasible ? 1 : 0)
            << ',' << (fitness.constraint_feasible ? 1 : 0)
            << ',' << (fitness.first_order_feasible ? 1 : 0) << ',';
        write_number(fitness.trace_violation);
        out << ',';
        write_number(fitness.constraint_violation);
        out << ',';
        write_number(fitness.first_order_violation);
        out << ',';
        write_number(score.merit);
        out << ',';
        write_number(score.trace_coverage);
        out << ',';
        write_number(score.min_field_coverage);
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
        out << ',';
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
        write_number(score.min_edge);
        out << ',';
        write_number(basin.nearest_distance);
        out << ',' << (basin.active_eligible ? 1 : 0)
            << ',' << json_escape_local(basin.active_reject_reason);
        out << '\n';
    }
    return true;
}

bool save_basin_archive_json(const OptimizationResult& result, const std::string& path) {
    if (result.basin_elites.empty()) {
        return false;
    }
    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out << std::setprecision(17);
    std::vector<std::string> topology_keys;
    std::vector<std::string> semantic_keys;
    std::vector<std::string> descriptor_keys;
    std::vector<std::string> similarity_family_keys;
    int active_eligible_count = 0;
    for (const BasinElite& basin : result.basin_elites) {
        if (basin.active_eligible) {
            ++active_eligible_count;
        }
        std::string key = basin.topology_key.empty() ? basin.fitness.topology_key : basin.topology_key;
        if (key.empty()) {
            key = "__unknown__";
        }
        if (std::find(topology_keys.begin(), topology_keys.end(), key) == topology_keys.end()) {
            topology_keys.push_back(key);
        }
        std::string semantic_key =
            basin.semantic_key.empty() ? basin.fitness.semantic_key : basin.semantic_key;
        if (semantic_key.empty()) {
            semantic_key = "__unknown__";
        }
        if (std::find(semantic_keys.begin(), semantic_keys.end(), semantic_key) == semantic_keys.end()) {
            semantic_keys.push_back(std::move(semantic_key));
        }
        std::string descriptor_key =
            basin.descriptor_key.empty() ? basin.fitness.descriptor_key : basin.descriptor_key;
        if (descriptor_key.empty()) {
            descriptor_key = "__unknown__";
        }
        if (std::find(descriptor_keys.begin(), descriptor_keys.end(), descriptor_key) ==
            descriptor_keys.end()) {
            descriptor_keys.push_back(std::move(descriptor_key));
        }
        std::string similarity_family_key = basin.similarity_family_key;
        if (similarity_family_key.empty()) {
            similarity_family_key = key;
        }
        if (similarity_family_key.empty()) {
            similarity_family_key = "__unknown__";
        }
        if (std::find(
                similarity_family_keys.begin(),
                similarity_family_keys.end(),
                similarity_family_key) == similarity_family_keys.end()) {
            similarity_family_keys.push_back(std::move(similarity_family_key));
        }
    }
    out << "{\n  \"count\": " << result.basin_elites.size() << ",\n";
    out << "  \"topology_count\": " << topology_keys.size() << ",\n";
    out << "  \"structural_topology_count\": " << topology_keys.size() << ",\n";
    out << "  \"semantic_count\": " << semantic_keys.size() << ",\n";
    out << "  \"descriptor_count\": " << descriptor_keys.size() << ",\n";
    out << "  \"similarity_family_count\": " << similarity_family_keys.size() << ",\n";
    out << "  \"active_eligible_count\": " << active_eligible_count << ",\n";
    out << "  \"basins\": [\n";
    for (std::size_t i = 0; i < result.basin_elites.size(); ++i) {
        const BasinElite& basin = result.basin_elites[i];
        const CandidateFitness& fitness = basin.fitness;
        const Score& score = fitness.score;
        const std::string topology_key =
            basin.topology_key.empty() ? fitness.topology_key : basin.topology_key;
        const std::string semantic_key =
            basin.semantic_key.empty() ? fitness.semantic_key : basin.semantic_key;
        const std::string descriptor_key =
            basin.descriptor_key.empty() ? fitness.descriptor_key : basin.descriptor_key;
        const std::string descriptor_diagnostic =
            basin.descriptor_diagnostic.empty()
                ? fitness.descriptor_diagnostic
                : basin.descriptor_diagnostic;
        const std::vector<std::uint64_t>& descriptor_axis_values =
            basin.descriptor_axis_values.empty()
                ? fitness.descriptor_axis_values
                : basin.descriptor_axis_values;
        out << "    {\n";
        out << "      \"id\": " << basin.id << ",\n";
        out << "      \"rank\": " << (i + 1) << ",\n";
        out << "      \"source\": \"" << json_escape_local(
            basin.source.empty() ? fitness.source : basin.source) << "\",\n";
        out << "      \"topology_key\": \"" << json_escape_local(topology_key) << "\",\n";
        out << "      \"structural_topology_key\": \"" << json_escape_local(topology_key) << "\",\n";
        out << "      \"semantic_key\": \"" << json_escape_local(semantic_key) << "\",\n";
        out << "      \"descriptor_key\": \""
            << json_escape_local(descriptor_key) << "\",\n";
        out << "      \"descriptor_diagnostic\": \""
            << json_escape_local(descriptor_diagnostic) << "\",\n";
        out << "      \"continuous_topology_signature\": \""
            << json_escape_local(fitness.continuous_topology_signature) << "\",\n";
        out << "      \"avoided_topology\": "
            << (fitness.avoided_topology ? "true" : "false") << ",\n";
        out << "      \"avoidance_penalty\": ";
        write_json_number_local(out, fitness.avoidance_penalty);
        out << ",\n";
        out << "      \"avoidance_match_channel\": \""
            << json_escape_local(fitness.avoidance_match_channel) << "\",\n";
        out << "      \"avoidance_match_score\": ";
        write_json_number_local(out, fitness.avoidance_match_score);
        out << ",\n";
        out << "      \"similarity_family_id\": " << basin.similarity_family_id << ",\n";
        out << "      \"similarity_family_key\": \""
            << json_escape_local(basin.similarity_family_key) << "\",\n";
        out << "      \"similarity_to_family\": ";
        write_json_number_local(out, basin.similarity_to_family);
        out << ",\n";
        out << "      \"descriptor_axis_values\": [";
        for (std::size_t j = 0; j < descriptor_axis_values.size(); ++j) {
            if (j > 0) {
                out << ", ";
            }
            out << "\"" << hex_hash_local(descriptor_axis_values[j]) << "\"";
        }
        out << "],\n";
        out << "      \"generation\": " << basin.generation << ",\n";
        out << "      \"polished\": " << (fitness.polished ? "true" : "false") << ",\n";
        out << "      \"objective\": ";
        write_json_number_local(out, fitness.objective);
        out << ",\n      \"total_violation\": ";
        write_json_number_local(out, fitness.total_violation);
        out << ",\n      \"feasible\": " << (fitness.feasible ? "true" : "false") << ",\n";
        out << "      \"trace_feasible\": " << (fitness.trace_feasible ? "true" : "false") << ",\n";
        out << "      \"constraint_feasible\": " << (fitness.constraint_feasible ? "true" : "false") << ",\n";
        out << "      \"first_order_feasible\": " << (fitness.first_order_feasible ? "true" : "false") << ",\n";
        out << "      \"trace_violation\": ";
        write_json_number_local(out, fitness.trace_violation);
        out << ",\n      \"constraint_violation\": ";
        write_json_number_local(out, fitness.constraint_violation);
        out << ",\n      \"first_order_violation\": ";
        write_json_number_local(out, fitness.first_order_violation);
        out << ",\n      \"nearest_distance\": ";
        write_json_number_local(out, basin.nearest_distance);
        out << ",\n      \"active_eligible\": "
            << (basin.active_eligible ? "true" : "false");
        out << ",\n      \"active_reject_reason\": \""
            << json_escape_local(basin.active_reject_reason) << "\"";
        out << ",\n      \"score\": ";
        write_score_summary_json(out, score);
        out << ",\n      \"values\": [";
        for (std::size_t j = 0; j < basin.physical_values.size(); ++j) {
            if (j > 0) {
                out << ", ";
            }
            write_json_number_local(out, basin.physical_values[j]);
        }
        out << "]\n    }";
        if (i + 1 < result.basin_elites.size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ]\n}\n";
    return true;
}

} } }
