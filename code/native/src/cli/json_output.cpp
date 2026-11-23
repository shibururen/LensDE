// Supply shared JSON escaping, numeric summaries, token normalization, and CUDA merit gates.
// Trace/checkpoint writers and command setup depend on these small native CLI conventions.
// Preserve null handling and emitted precision; these helpers must not recompute or alter scores.

#include "cli_internal.hpp"

namespace lensde { namespace cli { namespace detail {

std::string json_escape_local(const std::string& value) {
    std::ostringstream out;
    for (char ch : value) {
        switch (ch) {
            case '\\':
                out << "\\\\";
                break;
            case '"':
                out << "\\\"";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                out << ch;
                break;
        }
    }
    return out.str();
}

std::string hex_hash_local(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << value;
    return out.str();
}

void write_json_number_local(std::ostream& out, double value) {
    if (std::isfinite(value)) {
        out << std::setprecision(12) << value;
    } else {
        out << "null";
    }
}

void write_active_niche_best_merits_json(
    std::ostream& out,
    const std::vector<ActiveNicheMeritSnapshot>& snapshots) {
    out << "[";
    for (std::size_t i = 0; i < snapshots.size(); ++i) {
        const ActiveNicheMeritSnapshot& snapshot = snapshots[i];
        if (i > 0) {
            out << ",";
        }
        out << "{";
        out << "\"niche_index\":" << snapshot.niche_index;
        out << ",\"key\":\"" << json_escape_local(snapshot.key) << "\"";
        out << ",\"member_count\":" << snapshot.member_count;
        out << ",\"best_population_index\":" << snapshot.best_population_index;
        out << ",\"best_merit\":";
        write_json_number_local(out, snapshot.best_merit);
        out << ",\"focus\":" << (snapshot.focus ? "true" : "false");
        out << "}";
    }
    out << "]";
}

std::string normalized_token_local(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        if (ch == '-' || std::isspace(ch)) {
            return '_';
        }
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

bool distortion_model_allows_cuda_local(const MeritConfig& merit) {
    const std::string model = normalized_token_local(merit.distortion_model);
    return model.empty() || model == "auto" ||
           model == "f_tan_theta" || model == "ftantheta" ||
           model == "tan_theta" || model == "tantheta" ||
           model == "f_theta" || model == "ftheta" || model == "theta";
}

bool de_cuda_required_local() {
#ifdef OPTILAND_DE_CUDA_ONLY
    return true;
#else
    return std::getenv("OPTILAND_DE_CUDA_REQUIRE") != nullptr;
#endif
}

void write_score_summary_json(std::ostream& out, const Score& score) {
    out << "{\"chief_reference_contract\":\"" << kChiefReferenceContract
        << "\",\"principal_ray_required\":false,";
    out << "\"merit\":";
    write_json_number_local(out, score.merit);
    out << ",\"uncapped_merit\":";
    write_json_number_local(out, score.uncapped_merit);
    out << ",\"efl\":";
    write_json_number_local(out, score.efl);
    out << ",\"ttl\":";
    write_json_number_local(out, score.ttl);
    out << ",\"bfl\":";
    write_json_number_local(out, score.bfl);
    out << ",\"mean_spot\":";
    write_json_number_local(out, score.mean_spot);
    out << ",\"max_spot\":";
    write_json_number_local(out, score.max_spot);
    out << ",\"max_single_ray_spot\":";
    write_json_number_local(out, score.max_single_ray_spot);
    out << ",\"mean_worst_wavelength_spot\":";
    write_json_number_local(out, score.mean_worst_wavelength_spot);
    out << ",\"max_worst_wavelength_spot\":";
    write_json_number_local(out, score.max_worst_wavelength_spot);
    out << ",\"mean_spectral_spot_spread\":";
    write_json_number_local(out, score.mean_spectral_spot_spread);
    out << ",\"max_spectral_spot_spread\":";
    write_json_number_local(out, score.max_spectral_spot_spread);
    out << ",\"paraxial_axial_color_focus_shift_mm\":";
    write_json_number_local(out, score.paraxial_axial_color_focus_shift_mm);
    out << ",\"max_single_ray_spot_ratio\":";
    write_json_number_local(out, score.max_single_ray_spot_ratio);
    out << ",\"mean_wavefront_rms\":";
    write_json_number_local(out, score.mean_wavefront_rms);
    out << ",\"max_wavefront_rms\":";
    write_json_number_local(out, score.max_wavefront_rms);
    out << ",\"max_distortion\":";
    write_json_number_local(out, score.max_distortion);
    out << ",\"distortion_model\":\"" << json_escape_local(score.distortion_model) << "\"";
    out << ",\"distortion_sign_flip_fields\":" << score.distortion_sign_flip_fields;
    out << ",\"max_chief_ray_angle\":";
    write_json_number_local(out, score.max_chief_ray_angle);
    out << ",\"chief_ray_path_bend_mean_deg\":";
    write_json_number_local(out, score.chief_ray_path_bend_mean_deg);
    out << ",\"chief_ray_path_bend_mean_off_axis_deg\":";
    write_json_number_local(out, score.chief_ray_path_bend_mean_off_axis_deg);
    out << ",\"chief_ray_path_bend_max_deg\":";
    write_json_number_local(out, score.chief_ray_path_bend_max_deg);
    out << ",\"chief_ray_path_bend_max_step_deg\":";
    write_json_number_local(out, score.chief_ray_path_bend_max_step_deg);
    out << ",\"chief_ray_path_excess_mean\":";
    write_json_number_local(out, score.chief_ray_path_excess_mean);
    out << ",\"chief_ray_path_excess_max\":";
    write_json_number_local(out, score.chief_ray_path_excess_max);
    out << ",\"lateral_color\":";
    write_json_number_local(out, score.lateral_color);
    out << ",\"finite_ray_field_focus_spread_mm\":";
    write_json_number_local(out, score.finite_ray_field_focus_spread_mm);
    out << ",\"finite_ray_astig_focus_gap_mm\":";
    write_json_number_local(out, score.finite_ray_astig_focus_gap_mm);
    out << ",\"finite_ray_axial_color_focus_shift_mm\":";
    write_json_number_local(out, score.finite_ray_axial_color_focus_shift_mm);
    out << ",\"finite_ray_lateral_color_mm\":";
    write_json_number_local(out, score.finite_ray_lateral_color_mm);
    out << ",\"finite_ray_spherical_focus_spread_mm\":";
    write_json_number_local(out, score.finite_ray_spherical_focus_spread_mm);
    out << ",\"finite_ray_coma_tangential_mm\":";
    write_json_number_local(out, score.finite_ray_coma_tangential_mm);
    out << ",\"finite_ray_valid_fraction\":";
    write_json_number_local(out, score.finite_ray_valid_fraction);
    out << ",\"finite_ray_metric_count\":";
    write_json_number_local(out, score.finite_ray_metric_count);
    out << ",\"finite_ray_evidence\":";
    write_json_number_local(out, score.finite_ray_evidence);
    out << ",\"topology_potential_score_v2\":";
    write_json_number_local(out, score.topology_potential_score_v2);
    out << ",\"topology_potential_first_order_health_v2\":";
    write_json_number_local(out, score.topology_potential_first_order_health_v2);
    out << ",\"topology_potential_hard_aberration_health_v2\":";
    write_json_number_local(out, score.topology_potential_hard_aberration_health_v2);
    out << ",\"topology_potential_image_health_v2\":";
    write_json_number_local(out, score.topology_potential_image_health_v2);
    out << ",\"topology_potential_margin_health_v2\":";
    write_json_number_local(out, score.topology_potential_margin_health_v2);
    out << ",\"topology_potential_balance_health_v2\":";
    write_json_number_local(out, score.topology_potential_balance_health_v2);
    out << ",\"topology_potential_trace_health_v2\":";
    write_json_number_local(out, score.topology_potential_trace_health_v2);
    out << ",\"topology_potential_score_v3\":";
    write_json_number_local(out, score.topology_potential_score_v3);
    out << ",\"topology_potential_first_order_health_v3\":";
    write_json_number_local(out, score.topology_potential_first_order_health_v3);
    out << ",\"topology_potential_chief_path_health_v3\":";
    write_json_number_local(out, score.topology_potential_chief_path_health_v3);
    out << ",\"topology_potential_hard_aberration_health_v3\":";
    write_json_number_local(out, score.topology_potential_hard_aberration_health_v3);
    out << ",\"topology_potential_image_health_v3\":";
    write_json_number_local(out, score.topology_potential_image_health_v3);
    out << ",\"topology_potential_margin_health_v3\":";
    write_json_number_local(out, score.topology_potential_margin_health_v3);
    out << ",\"topology_potential_balance_health_v3\":";
    write_json_number_local(out, score.topology_potential_balance_health_v3);
    out << ",\"topology_potential_trace_health_v3\":";
    write_json_number_local(out, score.topology_potential_trace_health_v3);
    out << ",\"min_edge\":";
    write_json_number_local(out, score.min_edge);
    out << ",\"trace_ok\":" << (score.trace_ok ? "true" : "false");
    out << "}";
}

} } }
