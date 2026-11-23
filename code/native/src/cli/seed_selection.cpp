// Load candidate pools and select the requested seed population under the configured diversity rules.
// The seed-only command publishes source-aligned rows before ordinary optimization can start.
// Preserve deterministic source indices, activity groups, distance comparisons, and output row order.

#include "cli_internal.hpp"

namespace lensde { namespace cli { namespace detail {

bool load_seed_candidate_pool_csv(
    const std::string& path,
    int dim,
    std::vector<SeedPoolCandidate>* candidates,
    std::string* error) {
    if (!candidates) {
        return false;
    }
    candidates->clear();
    std::ifstream in(path);
    if (!in) {
        if (error) {
            *error = "cannot open " + path;
        }
        return false;
    }

    std::string line;
    if (!std::getline(in, line)) {
        return true;
    }
    std::vector<std::string> first = split_csv_line_simple(line);
    int v0_index = -1;
    int strict_index = -1;
    int merit_index = -1;
    for (std::size_t i = 0; i < first.size(); ++i) {
        if (first[i] == "v0") {
            v0_index = static_cast<int>(i);
        } else if (first[i] == "strict") {
            strict_index = static_cast<int>(i);
        } else if (first[i] == "merit") {
            merit_index = static_cast<int>(i);
        }
    }
    const bool has_header = v0_index >= 0;
    int line_no = 1;

    auto consume_numeric_line = [&](const std::string& numeric_line, int current_line) -> bool {
        SeedPoolCandidate candidate;
        if (!parse_numeric_seed_row_line(numeric_line, dim, &candidate.row)) {
            if (error) {
                *error = "line " + std::to_string(current_line) +
                         " has a non-numeric or incomplete variable row";
            }
            return false;
        }
        candidates->push_back(std::move(candidate));
        return true;
    };

    auto consume_cells = [&](const std::vector<std::string>& cells, int current_line) -> bool {
        if (cells.empty()) {
            return true;
        }
        const int start = has_header ? v0_index : 0;
        if (static_cast<int>(cells.size()) < start + dim) {
            if (error) {
                *error = "line " + std::to_string(current_line) +
                         " has too few columns for dim " + std::to_string(dim);
            }
            return false;
        }
        if (has_header && strict_index >= 0 &&
            strict_index < static_cast<int>(cells.size()) &&
            !parse_bool_cell(cells[static_cast<std::size_t>(strict_index)], true)) {
            return true;
        }
        SeedPoolCandidate candidate;
        candidate.row.reserve(static_cast<std::size_t>(dim));
        for (int j = 0; j < dim; ++j) {
            double value = 0.0;
            if (!parse_double_cell(cells[static_cast<std::size_t>(start + j)], &value)) {
                if (error) {
                    *error = "line " + std::to_string(current_line) +
                             " has a non-numeric variable cell";
                }
                return false;
            }
            candidate.row.push_back(value);
        }
        if (has_header && merit_index >= 0 &&
            merit_index < static_cast<int>(cells.size())) {
            double merit = std::numeric_limits<double>::infinity();
            if (parse_double_cell(cells[static_cast<std::size_t>(merit_index)], &merit)) {
                candidate.merit = merit;
            }
        }
        candidates->push_back(std::move(candidate));
        return true;
    };

    if (!has_header && !consume_numeric_line(line, line_no)) {
        return false;
    }
    while (std::getline(in, line)) {
        ++line_no;
        if (line.empty()) {
            continue;
        }
        if (has_header) {
            if (!consume_cells(split_csv_line_simple(line), line_no)) {
                return false;
            }
        } else {
            if (!consume_numeric_line(line, line_no)) {
                return false;
            }
        }
    }
    return true;
}

bool write_rows_csv(const std::string& path, const std::vector<std::vector<double>>& rows) {
    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out << std::setprecision(17);
    for (const auto& row : rows) {
        for (std::size_t i = 0; i < row.size(); ++i) {
            if (i > 0) {
                out << ',';
            }
            out << row[i];
        }
        out << '\n';
    }
    return true;
}

namespace {
class SeedPopulationSelection {
public:
    SeedPopulationSelection(std::vector<SeedPoolCandidate> candidates,
    const std::vector<VariableSpec>& variables,
    int target, unsigned int seed, double min_active_fraction,
    double min_curvature_active_fraction, double* selection_seconds, int* deduped_count)
        : candidates(std::move(candidates)), variables(variables), target(target), seed(seed),
          min_active_fraction(min_active_fraction), min_curvature_active_fraction(min_curvature_active_fraction),
          selection_seconds(selection_seconds), deduped_count(deduped_count) {}

    std::vector<std::vector<double>> execute() {
        if (!deduplicate()) return {};
        initialize_dimensions();
        reduce_candidate_pool();
        normalize_candidates();
        initialize_selection();
        seed_axis_extremes();
        prepare_greedy_scores();
        select_diverse_seeds();
        finish_coverage();
        return collect_rows();
    }

private:
    std::vector<SeedPoolCandidate> candidates;
    const std::vector<VariableSpec>& variables;
    int target;
    unsigned int seed;
    double min_active_fraction;
    double min_curvature_active_fraction;
    double* selection_seconds;
    int* deduped_count;
    std::optional<std::mt19937> rng_storage;
    std::chrono::steady_clock::time_point started{};
    int dim{};
    std::vector<SeedPoolCandidate> strict_objects{};
    std::vector<char> is_curvature_variable{};
    int curvature_dim{};
    int n{};
    std::vector<double> normalized{};
    std::vector<int> active_counts{};
    std::vector<int> active_curvature_counts{};
    std::vector<std::vector<int>> active_indices{};
    std::vector<std::string> curvature_masks{};
    std::vector<SeedPoolCandidate*> selected{};
    std::vector<char> selected_mask{};
    std::unordered_set<std::string> selected_keys{};
    std::vector<double> min_distances{};
    int greedy_target{};
    std::vector<double> jitter{};

    int required_active_count_for_variable(int variable_index) {
        if (variable_index < 0 || variable_index >= dim || target <= 0) {
            return 0;
        }
        const double fraction =
            is_curvature_variable[static_cast<std::size_t>(variable_index)]
                ? min_curvature_active_fraction
                : min_active_fraction;
        if (fraction <= 0.0) {
            return 0;
        }
        return std::max(1, std::min(target, static_cast<int>(std::ceil(target * fraction))));
    }

    void update_distances(int obj_index) {
        const std::size_t base = static_cast<std::size_t>(obj_index) * dim;
        for (int i = 0; i < n; ++i) {
            if (selected_mask[static_cast<std::size_t>(i)]) {
                min_distances[static_cast<std::size_t>(i)] = 0.0;
                continue;
            }
            double acc = 0.0;
            const std::size_t row_base = static_cast<std::size_t>(i) * dim;
            for (int j = 0; j < dim; ++j) {
                const double delta =
                    normalized[row_base + static_cast<std::size_t>(j)] -
                    normalized[base + static_cast<std::size_t>(j)];
                acc += delta * delta;
            }
            const double distance = acc / std::max(1, dim);
            if (distance < min_distances[static_cast<std::size_t>(i)]) {
                min_distances[static_cast<std::size_t>(i)] = distance;
            }
        }
    }

    bool add_index(int obj_index) {
        if (static_cast<int>(selected.size()) >= target ||
            obj_index < 0 ||
            obj_index >= n ||
            selected_mask[static_cast<std::size_t>(obj_index)]) {
            return false;
        }
        const std::string key = seed_row_key_cpp(
            strict_objects[static_cast<std::size_t>(obj_index)].row,
            variables);
        if (!selected_keys.insert(key).second) {
            selected_mask[static_cast<std::size_t>(obj_index)] = 1;
            return false;
        }
        selected.push_back(&strict_objects[static_cast<std::size_t>(obj_index)]);
        selected_mask[static_cast<std::size_t>(obj_index)] = 1;
        min_distances[static_cast<std::size_t>(obj_index)] = 0.0;
        update_distances(obj_index);
        return true;
    }

    std::tuple<double, double, double> score_candidate(int i, const std::unordered_set<std::string>& selected_masks, bool include_mask_bonus) {
        double min_distance = min_distances[static_cast<std::size_t>(i)];
        if (!std::isfinite(min_distance)) {
            min_distance = 0.0;
        }
        const double active_bonus =
            static_cast<double>(active_counts[static_cast<std::size_t>(i)]) / std::max(1, dim);
        const double curvature_active_bonus =
            static_cast<double>(active_curvature_counts[static_cast<std::size_t>(i)]) /
            std::max(1, curvature_dim);
        const double mask_bonus =
            include_mask_bonus &&
                    selected_masks.find(curvature_masks[static_cast<std::size_t>(i)]) == selected_masks.end()
                ? 0.15
                : 0.0;
        const double merit = strict_objects[static_cast<std::size_t>(i)].merit;
        const double negative_merit =
            std::isfinite(merit) ? -merit : -std::numeric_limits<double>::infinity();
        return std::tuple<double, double, double>(
            min_distance + 0.05 * active_bonus + 0.20 * curvature_active_bonus + mask_bonus,
            negative_merit,
            jitter[static_cast<std::size_t>(i)]);
    }

    bool append_without_distance_update(int obj_index) {
        if (static_cast<int>(selected.size()) >= target ||
            obj_index < 0 ||
            obj_index >= n ||
            selected_mask[static_cast<std::size_t>(obj_index)]) {
            return false;
        }
        const std::string key = seed_row_key_cpp(
            strict_objects[static_cast<std::size_t>(obj_index)].row,
            variables);
        if (!selected_keys.insert(key).second) {
            selected_mask[static_cast<std::size_t>(obj_index)] = 1;
            return false;
        }
        selected.push_back(&strict_objects[static_cast<std::size_t>(obj_index)]);
        selected_mask[static_cast<std::size_t>(obj_index)] = 1;
        return true;
    }

    int required_active_count(int variable_index) {
        if (variable_index < 0 || variable_index >= dim || target <= 0) {
            return 0;
        }
        const double fraction =
            is_curvature_variable[static_cast<std::size_t>(variable_index)]
                ? min_curvature_active_fraction
                : min_active_fraction;
        if (fraction <= 0.0) {
            return 0;
        }
        return std::max(1, std::min(target, static_cast<int>(std::ceil(target * fraction))));
    }

    void ensure_variable_active_coverage() {
        if (static_cast<int>(selected.size()) >= target) {
            return;
        }
        std::vector<int> required_counts(static_cast<std::size_t>(dim), 0);
        std::vector<int> selected_active_counts(static_cast<std::size_t>(dim), 0);
        std::vector<int> available_active_counts(static_cast<std::size_t>(dim), 0);
        bool any_required = false;
        for (int j = 0; j < dim; ++j) {
            required_counts[static_cast<std::size_t>(j)] = required_active_count(j);
            if (required_counts[static_cast<std::size_t>(j)] > 0) {
                any_required = true;
            }
        }
        if (!any_required) {
            return;
        }
        for (int i = 0; i < n; ++i) {
            for (int variable_index : active_indices[static_cast<std::size_t>(i)]) {
                if (variable_index >= 0 && variable_index < dim) {
                    ++available_active_counts[static_cast<std::size_t>(variable_index)];
                }
            }
        }
        for (int j = 0; j < dim; ++j) {
            required_counts[static_cast<std::size_t>(j)] = std::min(
                required_counts[static_cast<std::size_t>(j)],
                available_active_counts[static_cast<std::size_t>(j)]);
        }
        for (int i = 0; i < n; ++i) {
            if (!selected_mask[static_cast<std::size_t>(i)]) {
                continue;
            }
            for (int variable_index : active_indices[static_cast<std::size_t>(i)]) {
                if (variable_index >= 0 && variable_index < dim) {
                    ++selected_active_counts[static_cast<std::size_t>(variable_index)];
                }
            }
        }

        std::vector<char> skipped(static_cast<std::size_t>(n), 0);
        while (static_cast<int>(selected.size()) < target) {
            std::vector<char> deficit(static_cast<std::size_t>(dim), 0);
            bool has_deficit = false;
            for (int j = 0; j < dim; ++j) {
                if (required_counts[static_cast<std::size_t>(j)] >
                    selected_active_counts[static_cast<std::size_t>(j)]) {
                    deficit[static_cast<std::size_t>(j)] = 1;
                    has_deficit = true;
                }
            }
            if (!has_deficit) {
                break;
            }
            int best_index = -1;
            std::tuple<double, double, double, double> best_score{
                -std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity()};
            for (int i = 0; i < n; ++i) {
                if (selected_mask[static_cast<std::size_t>(i)] ||
                    skipped[static_cast<std::size_t>(i)]) {
                    continue;
                }
                double coverage_gain = 0.0;
                for (int variable_index : active_indices[static_cast<std::size_t>(i)]) {
                    if (variable_index < 0 ||
                        variable_index >= dim ||
                        !deficit[static_cast<std::size_t>(variable_index)]) {
                        continue;
                    }
                    const double weight =
                        is_curvature_variable[static_cast<std::size_t>(variable_index)]
                            ? 2.0
                            : 1.0;
                    coverage_gain += weight / std::max(1, required_counts[static_cast<std::size_t>(variable_index)]);
                }
                if (coverage_gain <= 0.0) {
                    continue;
                }
                const double merit = strict_objects[static_cast<std::size_t>(i)].merit;
                const double negative_merit =
                    std::isfinite(merit) ? -merit : -std::numeric_limits<double>::infinity();
                const auto score = std::tuple<double, double, double, double>(
                    coverage_gain,
                    static_cast<double>(active_curvature_counts[static_cast<std::size_t>(i)]) /
                        std::max(1, curvature_dim),
                    static_cast<double>(active_counts[static_cast<std::size_t>(i)]) /
                        std::max(1, dim),
                    negative_merit + 1.0e-9 * jitter[static_cast<std::size_t>(i)]);
                if (best_index < 0 || score > best_score) {
                    best_index = i;
                    best_score = score;
                }
            }
            if (best_index < 0) {
                break;
            }
            if (append_without_distance_update(best_index)) {
                for (int variable_index : active_indices[static_cast<std::size_t>(best_index)]) {
                    if (variable_index >= 0 && variable_index < dim) {
                        ++selected_active_counts[static_cast<std::size_t>(variable_index)];
                    }
                }
            } else {
                skipped[static_cast<std::size_t>(best_index)] = 1;
            }
        }
    }

    bool deduplicate() {
        started = std::chrono::steady_clock::now();
        target = std::max(0, target);
        min_active_fraction = std::clamp(min_active_fraction, 0.0, 1.0);
        min_curvature_active_fraction = std::clamp(min_curvature_active_fraction, 0.0, 1.0);
        dim = static_cast<int>(variables.size());
        std::unordered_set<std::string> seen;
        // strict_objects is retained across the selection stages.
        strict_objects.reserve(candidates.size());
        for (auto& candidate : candidates) {
            if (static_cast<int>(candidate.row.size()) != dim) {
                continue;
            }
            const std::string key = seed_row_key_cpp(candidate.row, variables);
            if (!seen.insert(key).second) {
                continue;
            }
            strict_objects.push_back(std::move(candidate));
        }
        if (deduped_count) {
            *deduped_count = static_cast<int>(strict_objects.size());
        }
        if (target <= 0 || strict_objects.empty()) {
            return false;
        }
        return true;
    }

    void initialize_dimensions() {
        rng_storage.emplace(seed);
        is_curvature_variable = std::vector<char>(static_cast<std::size_t>(dim), 0);
        curvature_dim = 0;
        for (int j = 0; j < dim; ++j) {
            if (variables[static_cast<std::size_t>(j)].kind == VariableKind::Curvature) {
                is_curvature_variable[static_cast<std::size_t>(j)] = 1;
                ++curvature_dim;
            }
        }
    }

    void reduce_candidate_pool() {
        auto& rng = *rng_storage;
        const int old_selection_limit =
            std::max({2048, target * 2, target + 2 * std::max(1, dim)});
        const int selection_limit = std::max(
            target,
            std::min(
                old_selection_limit,
                std::max({
                    512,
                    static_cast<int>(std::ceil(target * 1.25)),
                    target + std::max(256, 6 * std::max(1, dim)),
                })));
        if (static_cast<int>(strict_objects.size()) > selection_limit) {
            std::unordered_set<int> reserved;
            for (int variable_index = 0; variable_index < dim; ++variable_index) {
                int min_index = 0;
                int max_index = 0;
                for (int i = 1; i < static_cast<int>(strict_objects.size()); ++i) {
                    const VariableSpec& variable =
                        variables[static_cast<std::size_t>(variable_index)];
                    const double value = seed_axis_coordinate_cpp(
                        strict_objects[static_cast<std::size_t>(i)]
                            .row[static_cast<std::size_t>(variable_index)],
                        variable);
                    const double min_value = seed_axis_coordinate_cpp(
                        strict_objects[static_cast<std::size_t>(min_index)]
                            .row[static_cast<std::size_t>(variable_index)],
                        variable);
                    const double max_value = seed_axis_coordinate_cpp(
                        strict_objects[static_cast<std::size_t>(max_index)]
                            .row[static_cast<std::size_t>(variable_index)],
                        variable);
                    if (value < min_value) {
                        min_index = i;
                    }
                    if (value > max_value) {
                        max_index = i;
                    }
                }
                reserved.insert(min_index);
                reserved.insert(max_index);
            }
            std::vector<int> merit_order(strict_objects.size());
            std::iota(merit_order.begin(), merit_order.end(), 0);
            auto merit_less = [&](int a, int b) {
                return strict_objects[static_cast<std::size_t>(a)].merit <
                       strict_objects[static_cast<std::size_t>(b)].merit;
            };
            const int merit_count = std::min(
                std::max(64, target / 4),
                static_cast<int>(merit_order.size()));
            if (merit_count < static_cast<int>(merit_order.size())) {
                std::nth_element(
                    merit_order.begin(),
                    merit_order.begin() + merit_count,
                    merit_order.end(),
                    merit_less);
            } else {
                std::sort(merit_order.begin(), merit_order.end(), merit_less);
            }
            for (int i = 0; i < merit_count; ++i) {
                reserved.insert(merit_order[static_cast<std::size_t>(i)]);
            }
            std::unordered_map<std::string, int> mask_representatives;
            for (int i = 0; i < static_cast<int>(strict_objects.size()); ++i) {
                const std::string mask = row_curvature_mask_cpp(
                    strict_objects[static_cast<std::size_t>(i)].row,
                    variables);
                auto it = mask_representatives.find(mask);
                if (it == mask_representatives.end() ||
                    strict_objects[static_cast<std::size_t>(i)].merit <
                        strict_objects[static_cast<std::size_t>(it->second)].merit) {
                    mask_representatives[mask] = i;
                }
            }
            for (const auto& item : mask_representatives) {
                reserved.insert(item.second);
            }
            std::vector<std::vector<int>> prelimit_active_indices(strict_objects.size());
            std::vector<int> prelimit_active_counts(strict_objects.size(), 0);
            std::vector<int> prelimit_curvature_counts(strict_objects.size(), 0);
            std::uniform_real_distribution<double> prelimit_uniform01(0.0, 1.0);
            std::vector<double> prelimit_jitter(strict_objects.size(), 0.0);
            for (int i = 0; i < static_cast<int>(strict_objects.size()); ++i) {
                const auto& row = strict_objects[static_cast<std::size_t>(i)].row;
                prelimit_active_indices[static_cast<std::size_t>(i)] = active_indices_cpp(row, variables);
                prelimit_active_counts[static_cast<std::size_t>(i)] =
                    static_cast<int>(prelimit_active_indices[static_cast<std::size_t>(i)].size());
                int curvature_active = 0;
                for (int variable_index : prelimit_active_indices[static_cast<std::size_t>(i)]) {
                    if (variable_index >= 0 &&
                        variable_index < dim &&
                        is_curvature_variable[static_cast<std::size_t>(variable_index)]) {
                        ++curvature_active;
                    }
                }
                prelimit_curvature_counts[static_cast<std::size_t>(i)] = curvature_active;
                prelimit_jitter[static_cast<std::size_t>(i)] = prelimit_uniform01(rng);
            }
            auto contains_active_variable = [&](int obj_index, int variable_index) {
                const auto& indices = prelimit_active_indices[static_cast<std::size_t>(obj_index)];
                return std::find(indices.begin(), indices.end(), variable_index) != indices.end();
            };
            for (int variable_index = 0; variable_index < dim; ++variable_index) {
                const int required = required_active_count_for_variable(variable_index);
                if (required <= 0) {
                    continue;
                }
                int already_reserved = 0;
                for (int obj_index : reserved) {
                    if (contains_active_variable(obj_index, variable_index)) {
                        ++already_reserved;
                    }
                }
                const int need = std::max(0, required - already_reserved);
                if (need <= 0) {
                    continue;
                }
                std::vector<int> active_candidates;
                active_candidates.reserve(static_cast<std::size_t>(need));
                for (int i = 0; i < static_cast<int>(strict_objects.size()); ++i) {
                    if (reserved.find(i) != reserved.end() ||
                        !contains_active_variable(i, variable_index)) {
                        continue;
                    }
                    active_candidates.push_back(i);
                }
                if (active_candidates.empty()) {
                    continue;
                }
                auto active_preserve_less = [&](int a, int b) {
                    const auto score_a = std::tuple<int, int, double, double>(
                        -prelimit_curvature_counts[static_cast<std::size_t>(a)],
                        -prelimit_active_counts[static_cast<std::size_t>(a)],
                        strict_objects[static_cast<std::size_t>(a)].merit,
                        prelimit_jitter[static_cast<std::size_t>(a)]);
                    const auto score_b = std::tuple<int, int, double, double>(
                        -prelimit_curvature_counts[static_cast<std::size_t>(b)],
                        -prelimit_active_counts[static_cast<std::size_t>(b)],
                        strict_objects[static_cast<std::size_t>(b)].merit,
                        prelimit_jitter[static_cast<std::size_t>(b)]);
                    return score_a < score_b;
                };
                if (static_cast<int>(active_candidates.size()) > need) {
                    std::nth_element(
                        active_candidates.begin(),
                        active_candidates.begin() + need,
                        active_candidates.end(),
                        active_preserve_less);
                    active_candidates.resize(static_cast<std::size_t>(need));
                }
                for (int obj_index : active_candidates) {
                    reserved.insert(obj_index);
                }
            }
            std::vector<int> remaining;
            remaining.reserve(strict_objects.size());
            for (int i = 0; i < static_cast<int>(strict_objects.size()); ++i) {
                if (reserved.find(i) == reserved.end()) {
                    remaining.push_back(i);
                }
            }
            std::shuffle(remaining.begin(), remaining.end(), rng);
            std::vector<int> keep_indices;
            keep_indices.reserve(std::max(
                static_cast<std::size_t>(selection_limit),
                reserved.size()));
            for (int index : reserved) {
                keep_indices.push_back(index);
            }
            for (int index : remaining) {
                if (static_cast<int>(keep_indices.size()) >= selection_limit) {
                    break;
                }
                keep_indices.push_back(index);
            }
            std::vector<SeedPoolCandidate> limited;
            limited.reserve(keep_indices.size());
            for (int index : keep_indices) {
                limited.push_back(std::move(strict_objects[static_cast<std::size_t>(index)]));
            }
            strict_objects = std::move(limited);
        }
    }

    void normalize_candidates() {
        n = static_cast<int>(strict_objects.size());
        normalized = std::vector<double>(static_cast<std::size_t>(n) * dim, 0.0);
        active_counts = std::vector<int>(static_cast<std::size_t>(n), 0);
        active_curvature_counts = std::vector<int>(static_cast<std::size_t>(n), 0);
        active_indices = std::vector<std::vector<int>>(static_cast<std::size_t>(n));
        curvature_masks = std::vector<std::string>(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            const auto& row = strict_objects[static_cast<std::size_t>(i)].row;
            for (int j = 0; j < dim; ++j) {
                normalized[static_cast<std::size_t>(i) * dim + j] =
                    seed_axis_normalized_cpp(
                        row[static_cast<std::size_t>(j)],
                        variables[static_cast<std::size_t>(j)]);
            }
            active_indices[static_cast<std::size_t>(i)] = active_indices_cpp(row, variables);
            active_counts[static_cast<std::size_t>(i)] =
                static_cast<int>(active_indices[static_cast<std::size_t>(i)].size());
            int curvature_active = 0;
            for (int variable_index : active_indices[static_cast<std::size_t>(i)]) {
                if (variable_index >= 0 &&
                    variable_index < dim &&
                    is_curvature_variable[static_cast<std::size_t>(variable_index)]) {
                    ++curvature_active;
                }
            }
            active_curvature_counts[static_cast<std::size_t>(i)] = curvature_active;
            curvature_masks[static_cast<std::size_t>(i)] = row_curvature_mask_cpp(row, variables);
        }
    }

    void initialize_selection() {
        // selected is retained across the selection stages.
        selected.reserve(static_cast<std::size_t>(target));
        selected_mask = std::vector<char>(static_cast<std::size_t>(n), 0);
        // selected_keys is retained across the selection stages.
        min_distances = std::vector<double>(static_cast<std::size_t>(n), std::numeric_limits<double>::infinity());
    }

    void seed_axis_extremes() {
        for (int j = 0; j < dim && static_cast<int>(selected.size()) < target; ++j) {
            int min_index = 0;
            int max_index = 0;
            const VariableSpec& variable = variables[static_cast<std::size_t>(j)];
            for (int i = 1; i < n; ++i) {
                const double value = seed_axis_coordinate_cpp(
                    strict_objects[static_cast<std::size_t>(i)].row[static_cast<std::size_t>(j)],
                    variable);
                const double min_value = seed_axis_coordinate_cpp(
                    strict_objects[static_cast<std::size_t>(min_index)].row[static_cast<std::size_t>(j)],
                    variable);
                const double max_value = seed_axis_coordinate_cpp(
                    strict_objects[static_cast<std::size_t>(max_index)].row[static_cast<std::size_t>(j)],
                    variable);
                if (value < min_value) {
                    min_index = i;
                }
                if (value > max_value) {
                    max_index = i;
                }
            }
            add_index(min_index);
            if (static_cast<int>(selected.size()) >= target) {
                break;
            }
            add_index(max_index);
        }
        if (selected.empty()) {
            int best = 0;
            for (int i = 1; i < n; ++i) {
                if (strict_objects[static_cast<std::size_t>(i)].merit <
                    strict_objects[static_cast<std::size_t>(best)].merit) {
                    best = i;
                }
            }
            add_index(best);
        }
    }

    void prepare_greedy_scores() {
        auto& rng = *rng_storage;
        greedy_target = std::min(
                target,
                std::max({
                    256,
                    8 * std::max(1, dim),
                    static_cast<int>(std::sqrt(static_cast<double>(target)) * 8.0),
                }));
        std::uniform_real_distribution<double> uniform01(0.0, 1.0);
        jitter = std::vector<double>(static_cast<std::size_t>(n), 0.0);
        for (double& value : jitter) {
            value = uniform01(rng);
        }
    }

    void select_diverse_seeds() {
        while (static_cast<int>(selected.size()) < greedy_target) {
            std::unordered_set<std::string> selected_masks_set;
            for (int i = 0; i < n; ++i) {
                if (selected_mask[static_cast<std::size_t>(i)]) {
                    selected_masks_set.insert(curvature_masks[static_cast<std::size_t>(i)]);
                }
            }
            int best = -1;
            std::tuple<double, double, double> best_score{
                -std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity()};
            for (int i = 0; i < n; ++i) {
                if (selected_mask[static_cast<std::size_t>(i)]) {
                    continue;
                }
                const auto score = score_candidate(i, selected_masks_set, true);
                if (best < 0 || score > best_score) {
                    best = i;
                    best_score = score;
                }
            }
            if (best < 0) {
                break;
            }
            add_index(best);
        }
    }

    void finish_coverage() {
        ensure_variable_active_coverage();
        if (static_cast<int>(selected.size()) < target) {
            std::unordered_set<std::string> selected_masks_set;
            for (int i = 0; i < n; ++i) {
                if (selected_mask[static_cast<std::size_t>(i)]) {
                    selected_masks_set.insert(curvature_masks[static_cast<std::size_t>(i)]);
                }
            }
            std::unordered_map<std::string, int> mask_representatives;
            for (int i = 0; i < n; ++i) {
                if (selected_mask[static_cast<std::size_t>(i)] ||
                    selected_masks_set.find(curvature_masks[static_cast<std::size_t>(i)]) != selected_masks_set.end()) {
                    continue;
                }
                auto it = mask_representatives.find(curvature_masks[static_cast<std::size_t>(i)]);
                if (it == mask_representatives.end() ||
                    score_candidate(i, selected_masks_set, false) >
                        score_candidate(it->second, selected_masks_set, false)) {
                    mask_representatives[curvature_masks[static_cast<std::size_t>(i)]] = i;
                }
            }
            std::vector<int> reps;
            reps.reserve(mask_representatives.size());
            for (const auto& item : mask_representatives) {
                reps.push_back(item.second);
            }
            std::sort(reps.begin(), reps.end(), [&](int a, int b) {
                return score_candidate(a, selected_masks_set, false) >
                       score_candidate(b, selected_masks_set, false);
            });
            for (int index : reps) {
                if (append_without_distance_update(index)) {
                    selected_masks_set.insert(curvature_masks[static_cast<std::size_t>(index)]);
                }
            }
            std::vector<int> remaining;
            remaining.reserve(static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i) {
                if (!selected_mask[static_cast<std::size_t>(i)]) {
                    remaining.push_back(i);
                }
            }
            std::sort(remaining.begin(), remaining.end(), [&](int a, int b) {
                return score_candidate(a, selected_masks_set, true) >
                       score_candidate(b, selected_masks_set, true);
            });
            for (int index : remaining) {
                if (static_cast<int>(selected.size()) >= target) {
                    break;
                }
                if (append_without_distance_update(index)) {
                    selected_masks_set.insert(curvature_masks[static_cast<std::size_t>(index)]);
                }
            }
        }
    }

    std::vector<std::vector<double>> collect_rows() {
        std::vector<std::vector<double>> rows;
        rows.reserve(selected.size());
        for (SeedPoolCandidate* candidate : selected) {
            rows.push_back(candidate->row);
            if (static_cast<int>(rows.size()) >= target) {
                break;
            }
        }
        if (selection_seconds) {
            const auto finished = std::chrono::steady_clock::now();
            *selection_seconds =
                std::chrono::duration<double>(finished - started).count();
        }
        return rows;
    }
};
} // namespace

std::vector<std::vector<double>> select_seed_population_cpp(
    std::vector<SeedPoolCandidate> candidates,
    const std::vector<VariableSpec>& variables,
    int target,
    unsigned int seed,
    double min_active_fraction,
    double min_curvature_active_fraction,
    double* selection_seconds,
    int* deduped_count) {
    SeedPopulationSelection selection(std::move(candidates), variables, target, seed,
        min_active_fraction, min_curvature_active_fraction, selection_seconds, deduped_count);
    return selection.execute();
}

} } }
