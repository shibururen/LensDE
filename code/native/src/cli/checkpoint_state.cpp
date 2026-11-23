// Provide checkpoint summaries, aligned auxiliary-file readers, and dependency/resume snapshots.
// Checkpoint saving and optimizer setup share these serialization and adaptive-state helpers.
// Dependency input_best_values are current callback values at 17 digits, not an invented restart state.

#include "cli_internal.hpp"

namespace lensde { namespace cli { namespace detail {

void write_json_number_or_null(std::ostream& out, double value) {
    if (std::isfinite(value)) {
        out << std::setprecision(12) << value;
    } else {
        out << "null";
    }
}

void write_score_checkpoint_summary(std::ostream& out, const Score& score, const std::string& indent) {
    out << "{\n";
    out << indent << "  \"chief_reference_contract\": \"" << kChiefReferenceContract << "\",\n";
    out << indent << "  \"principal_ray_required\": false,\n";
    out << indent << "  \"merit\": ";
    write_json_number_or_null(out, score.merit);
    out << ",\n" << indent << "  \"uncapped_merit\": ";
    write_json_number_or_null(out, score.uncapped_merit);
    out << ",\n" << indent << "  \"mean_spot\": ";
    write_json_number_or_null(out, score.mean_spot);
    out << ",\n" << indent << "  \"max_spot\": ";
    write_json_number_or_null(out, score.max_spot);
    out << ",\n" << indent << "  \"mean_wavefront_rms\": ";
    write_json_number_or_null(out, score.mean_wavefront_rms);
    out << ",\n" << indent << "  \"max_wavefront_rms\": ";
    write_json_number_or_null(out, score.max_wavefront_rms);
    out << ",\n" << indent << "  \"trace_ok\": " << (score.trace_ok ? "true" : "false");
    out << ",\n" << indent << "  \"trace_coverage\": ";
    write_json_number_or_null(out, score.trace_coverage);
    out << ",\n" << indent << "  \"min_field_coverage\": ";
    write_json_number_or_null(out, score.min_field_coverage);
    out << ",\n" << indent << "  \"failed_ray_weight\": ";
    write_json_number_or_null(out, score.failed_ray_weight);
    out << ",\n" << indent << "  \"trace_group_error\": ";
    write_json_number_or_null(out, score.trace_group_error);
    out << ",\n" << indent << "  \"constraint_group_error\": ";
    write_json_number_or_null(out, score.constraint_group_error);
    out << ",\n" << indent << "  \"first_order_group_error\": ";
    write_json_number_or_null(out, score.first_order_group_error);
    out << ",\n" << indent << "  \"image_group_error\": ";
    write_json_number_or_null(out, score.image_group_error);
    out << "\n" << indent << "}";
}

void write_score_checkpoint_summary_compact(std::ostream& out, const Score& score) {
    out << "{\"chief_reference_contract\":\"" << kChiefReferenceContract
        << "\",\"principal_ray_required\":false,\"merit\":";
    write_json_number_or_null(out, score.merit);
    out << ",\"uncapped_merit\":";
    write_json_number_or_null(out, score.uncapped_merit);
    out << ",\"mean_spot\":";
    write_json_number_or_null(out, score.mean_spot);
    out << ",\"max_spot\":";
    write_json_number_or_null(out, score.max_spot);
    out << ",\"mean_wavefront_rms\":";
    write_json_number_or_null(out, score.mean_wavefront_rms);
    out << ",\"max_wavefront_rms\":";
    write_json_number_or_null(out, score.max_wavefront_rms);
    out << ",\"trace_ok\":" << (score.trace_ok ? "true" : "false");
    out << ",\"trace_coverage\":";
    write_json_number_or_null(out, score.trace_coverage);
    out << ",\"min_field_coverage\":";
    write_json_number_or_null(out, score.min_field_coverage);
    out << ",\"min_chief_progress\":";
    write_json_number_or_null(out, score.min_chief_progress);
    out << ",\"chief_ray_ok\":" << (score.chief_ray_ok ? "true" : "false");
    out << ",\"chief_ray_failed_fields\":" << score.chief_ray_failed_fields;
    out << ",\"zero_coverage_off_axis_fields\":"
        << score.zero_coverage_off_axis_fields;
    out << ",\"min_edge\":";
    write_json_number_or_null(out, score.min_edge);
    out << ",\"failed_ray_weight\":";
    write_json_number_or_null(out, score.failed_ray_weight);
    out << ",\"trace_group_error\":";
    write_json_number_or_null(out, score.trace_group_error);
    out << ",\"constraint_group_error\":";
    write_json_number_or_null(out, score.constraint_group_error);
    out << ",\"first_order_group_error\":";
    write_json_number_or_null(out, score.first_order_group_error);
    out << ",\"image_group_error\":";
    write_json_number_or_null(out, score.image_group_error);
    out << "}";
}

void update_checkpoint_adaptive_glass_state(
    CheckpointAdaptiveGlassState& state,
    const HybridTraceEvent& event) {
    state.seen = true;
    state.event = event.event;
    state.epoch = event.epoch;
    state.de_generations = event.de_generations;
    state.enabled = event.adaptive_glass_tolerance_enabled;
    state.updated = event.adaptive_glass_tolerance_updated;
    state.ready = event.adaptive_glass_tolerance_ready;
    state.trigger_ready = event.adaptive_glass_tolerance_trigger_ready;
    state.bridge_active = event.adaptive_glass_tolerance_bridge_active;
    state.focus_frozen = event.adaptive_glass_tolerance_focus_frozen;
    state.stop_when_ready = event.adaptive_glass_tolerance_stop_when_ready;
    state.trigger_mode = event.adaptive_glass_tolerance_trigger_mode;
    state.focus_clear_count = event.adaptive_glass_tolerance_focus_clear_count;
    state.focus_clear_target = event.adaptive_glass_tolerance_focus_clear_target;
    state.sample_count = event.adaptive_glass_tolerance_sample_count;
    state.elite_count = event.adaptive_glass_tolerance_elite_count;
    state.previous = event.adaptive_glass_tolerance_previous;
    state.current = event.adaptive_glass_tolerance_current;
    state.target = event.adaptive_glass_tolerance_target;
    state.metric = event.adaptive_glass_tolerance_metric;
}

void write_checkpoint_adaptive_glass_state_json(
    std::ostream& out,
    const CheckpointAdaptiveGlassState& state) {
    out << "{";
    out << "\"seen\":" << (state.seen ? "true" : "false");
    out << ",\"event\":\"" << state.event << "\"";
    out << ",\"epoch\":" << state.epoch;
    out << ",\"de_generations\":" << state.de_generations;
    out << ",\"enabled\":" << (state.enabled ? "true" : "false");
    out << ",\"updated\":" << (state.updated ? "true" : "false");
    out << ",\"ready\":" << (state.ready ? "true" : "false");
    out << ",\"trigger_ready\":" << (state.trigger_ready ? "true" : "false");
    out << ",\"bridge_active\":" << (state.bridge_active ? "true" : "false");
    out << ",\"focus_frozen\":" << (state.focus_frozen ? "true" : "false");
    out << ",\"stop_when_ready\":" << (state.stop_when_ready ? "true" : "false");
    out << ",\"trigger_mode\":\"" << state.trigger_mode << "\"";
    out << ",\"focus_clear_count\":" << state.focus_clear_count;
    out << ",\"focus_clear_target\":" << state.focus_clear_target;
    out << ",\"sample_count\":" << state.sample_count;
    out << ",\"elite_count\":" << state.elite_count;
    out << ",\"previous\":";
    write_json_number_or_null(out, state.previous);
    out << ",\"current\":";
    write_json_number_or_null(out, state.current);
    out << ",\"target\":";
    write_json_number_or_null(out, state.target);
    out << ",\"metric\":";
    write_json_number_or_null(out, state.metric);
    out << "}";
}

double read_score_merit_from_json(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();
    const std::string key = "\"merit\"";
    std::size_t pos = text.find(key);
    if (pos == std::string::npos) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    pos = text.find(':', pos + key.size());
    if (pos == std::string::npos) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const std::size_t start = text.find_first_not_of(" \t\r\n", pos + 1);
    if (start == std::string::npos) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const char* begin = text.c_str() + start;
    char* end = nullptr;
    const double value = std::strtod(begin, &end);
    if (end == begin) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return value;
}

std::string trim_ascii(std::string text) {
    auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), [&](unsigned char ch) {
        return !is_space(ch);
    }));
    text.erase(std::find_if(text.rbegin(), text.rend(), [&](unsigned char ch) {
        return !is_space(ch);
    }).base(), text.end());
    return text;
}

std::vector<std::string> load_key_list_file(const std::string& path) {
    std::vector<std::string> keys;
    if (path.empty()) {
        return keys;
    }
    std::ifstream in(path);
    if (!in) {
        return keys;
    }
    std::string line;
    std::unordered_set<std::string> seen;
    while (std::getline(in, line)) {
        std::string key = trim_ascii(line);
        if (key.empty() || key[0] == '#') {
            continue;
        }
        if (seen.insert(key).second) {
            keys.push_back(std::move(key));
        }
    }
    return keys;
}

std::vector<std::string> load_aligned_key_list_file(const std::string& path) {
    std::vector<std::string> keys;
    if (path.empty()) {
        return keys;
    }
    std::ifstream in(path);
    if (!in) {
        return keys;
    }
    std::string line;
    while (std::getline(in, line)) {
        std::string key = trim_ascii(line);
        keys.push_back(key.empty() ? "__unknown__" : std::move(key));
    }
    return keys;
}

std::vector<double> load_aligned_number_list_file(const std::string& path) {
    std::vector<double> values;
    if (path.empty()) {
        return values;
    }
    std::ifstream in(path);
    if (!in) {
        return values;
    }
    std::string line;
    while (std::getline(in, line)) {
        const std::string text = trim_ascii(line);
        if (text.empty()) {
            values.push_back(std::numeric_limits<double>::quiet_NaN());
            continue;
        }
        char* end = nullptr;
        const double value = std::strtod(text.c_str(), &end);
        values.push_back(
            end != text.c_str() && trim_ascii(end).empty()
                ? value
                : std::numeric_limits<double>::quiet_NaN());
    }
    return values;
}

void write_checkpoint_active_trace_fields(
    std::ostream& out,
    const HybridTraceEvent* event) {
    out << ",\"active_trace_event\":";
    if (event == nullptr) {
        out << "null";
    } else {
        out << "\"" << json_escape_local(event->event) << "\"";
    }
    out << ",\"active_trace_epoch\":"
        << (event == nullptr ? -1 : event->epoch);
    out << ",\"active_focus_best_merit\":";
    write_json_number_or_null(
        out,
        event == nullptr ? std::numeric_limits<double>::quiet_NaN()
                         : event->active_focus_best_merit);
    out << ",\"active_focus_best_key\":\""
        << (event == nullptr ? std::string{}
                             : json_escape_local(event->active_focus_best_key))
        << "\"";
    out << ",\"active_focus_best_niche_index\":"
        << (event == nullptr ? -1 : event->active_focus_best_niche_index);
    out << ",\"active_focus_best_population_index\":"
        << (event == nullptr ? -1 : event->active_focus_best_population_index);
    out << ",\"active_focus_best_member_count\":"
        << (event == nullptr ? 0 : event->active_focus_best_member_count);
    out << ",\"active_niche_hard_cap_release_count\":"
        << (event == nullptr ? 0 : event->active_niche_hard_cap_release_count);
    out << ",\"active_niche_best_merits\":";
    if (event == nullptr) {
        out << "[]";
    } else {
        write_active_niche_best_merits_json(out, event->active_niche_best_merits);
    }
}

const char* asphere_root_policy_token(AsphereRootPolicy policy) {
    switch (policy) {
        case AsphereRootPolicy::SlopeDomainTopologySeed:
            return "production";
        case AsphereRootPolicy::ConfirmedWithCertifiedFallback:
            return "confirmed_with_fallback";
        case AsphereRootPolicy::FastRecover:
            return "fast_recover";
        case AsphereRootPolicy::CertifiedFirstRoot:
            return "certified_first_root";
        case AsphereRootPolicy::UnifiedCertifiedExperimental:
            return "unified_certified";
    }
    return "unknown";
}

const char* checkpoint_evaluation_mode_token(CheckpointEvaluationMode mode) {
    switch (mode) {
        case CheckpointEvaluationMode::Current:
            return "current";
        case CheckpointEvaluationMode::NoMid:
            return "no_mid";
        case CheckpointEvaluationMode::PersistOnly:
            return "persist_only";
        case CheckpointEvaluationMode::FixCandidate:
            return "fix_candidate";
    }
    return "unknown";
}

void write_checkpoint_aperture_state_json(
    std::ostream& out,
    const LensSystem& system) {
    out << "{\"system_aperture_type\":"
        << static_cast<int>(system.aperture.type)
        << ",\"system_aperture_value\":" << std::setprecision(17)
        << system.aperture.value << ",\"surfaces\":[";
    for (std::size_t index = 0; index < system.surfaces.size(); ++index) {
        const Surface& surface = system.surfaces[index];
        if (index > 0) {
            out << ",";
        }
        out << "[" << surface.semi_aperture
            << "," << surface.mechanical_aperture
            << "," << surface.aperture_radius
            << "," << (surface.clear_aperture_fixed ? "true" : "false")
            << "," << (surface.mechanical_aperture_fixed ? "true" : "false")
            << "]";
    }
    out << "]}";
}

bool write_checkpoint_dependency_snapshot(
    const LensSystem& input_system,
    const LensSystem& output_system,
    const std::vector<double>& values,
    const std::string& out_dir,
    const std::string& phase,
    int step,
    const std::string& action,
    bool validated) {
    if (out_dir.empty()) {
        return false;
    }
    const std::string stem =
        "checkpoint_dependency_" + std::to_string(step);
    const std::string system_path = join_path(out_dir, stem + "_system.json");
    const bool system_saved = save_system_json(output_system, system_path);
    std::ofstream out(join_path(out_dir, stem + ".json"));
    if (!out) {
        return false;
    }
    out << std::setprecision(17);
    out << "{\n  \"phase\": \"" << json_escape_local(phase) << "\",\n";
    out << "  \"step\": " << step << ",\n";
    out << "  \"action\": \"" << json_escape_local(action) << "\",\n";
    out << "  \"validated\": " << (validated ? "true" : "false") << ",\n";
    out << "  \"input_best_values\": [";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            out << ",";
        }
        out << values[index];
    }
    out << "],\n  \"input_aperture_state\": ";
    write_checkpoint_aperture_state_json(out, input_system);
    out << ",\n  \"output_aperture_state\": ";
    write_checkpoint_aperture_state_json(out, output_system);
    out << ",\n  \"output_system_path\": \""
        << json_escape_local(system_path) << "\"\n}\n";
    return system_saved;
}

bool persist_unverified_checkpoint_state(
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const std::vector<double>& values,
    const GlassSearchContext& glass_search,
    const std::string& out_dir,
    const std::string& phase,
    int step,
    const Score& optimizer_score,
    CheckpointEvaluationMode mode,
    bool dependency_diagnostics) {
    if (values.size() != variables.size() || out_dir.empty()) {
        return false;
    }
    const auto started = std::chrono::steady_clock::now();
    LensSystem resume_state = apply_mixed_variables(base, variables, values, glass_search);
    const double materialize_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    if (dependency_diagnostics) {
        write_checkpoint_dependency_snapshot(
            base,
            resume_state,
            values,
            out_dir,
            phase,
            step,
            "persist_unverified",
            false);
    }
    const auto commit_started = std::chrono::steady_clock::now();
    const bool system_saved = save_system_json(
        resume_state, join_path(out_dir, "checkpoint_resume_system.json"));
    const bool score_saved = save_score_json(
        optimizer_score, join_path(out_dir, "checkpoint_resume_optimizer_score.json"));
    std::ofstream meta(join_path(out_dir, "checkpoint_resume_meta.json"));
    if (meta) {
        meta << "{\n";
        meta << "  \"phase\": \"" << json_escape_local(phase) << "\",\n";
        meta << "  \"step\": " << step << ",\n";
        meta << "  \"checkpoint_evaluation_mode\": \""
             << checkpoint_evaluation_mode_token(mode) << "\",\n";
        meta << "  \"validated\": false,\n";
        meta << "  \"promotion_contract\": \"validate_before_use\"\n";
        meta << "}\n";
    }
    const double commit_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - commit_started).count();
    std::ofstream diag(join_path(out_dir, "checkpoint_diagnostics.jsonl"), std::ios::app);
    if (diag) {
        diag << "{\"phase\":\"" << json_escape_local(phase)
             << "\",\"step\":" << step;
        diag << ",\"checkpoint_evaluation_mode\":\""
             << checkpoint_evaluation_mode_token(mode) << "\"";
        diag << ",\"checkpoint_score_source\":\"optimizer_resume_state\"";
        diag << ",\"validated\":false,\"accepted\":false";
        diag << ",\"promotion_contract\":\"validate_before_use\"";
        diag << ",\"aperture_update_performed\":false";
        diag << ",\"static_rescore_performed\":false";
        diag << ",\"resume_system_saved\":" << (system_saved ? "true" : "false");
        diag << ",\"resume_score_saved\":" << (score_saved ? "true" : "false");
        diag << ",\"incoming_best_score\":";
        write_score_checkpoint_summary_compact(diag, optimizer_score);
        diag << ",\"timing_seconds\":{";
        diag << "\"materialize\":" << materialize_seconds;
        diag << ",\"aperture_update\":0";
        diag << ",\"static_rescore\":0";
        diag << ",\"commit\":" << commit_seconds;
        diag << ",\"total\":" << std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started).count();
        diag << "}}\n";
    }
    return system_saved && score_saved && static_cast<bool>(meta);
}

} } }
