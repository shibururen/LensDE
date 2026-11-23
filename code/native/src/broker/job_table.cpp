// Decode the tab-separated island job table and its optional stage/trace-contract fields.
// The application reads jobs in this order before loading their scientific input documents.
// Preserve column interpretation, defaults, island identifiers, seeds, and per-job budgets.

#include "interfaces.hpp"

namespace lensde::broker {

std::vector<std::string> split_tab(const std::string& line) {
    std::vector<std::string> cells;
    std::string cell;
    for (char ch : line) {
        if (ch == '\t') {
            cells.push_back(cell);
            cell.clear();
        } else if (ch != '\r') {
            cell.push_back(ch);
        }
    }
    cells.push_back(cell);
    return cells;
}

std::vector<Job> load_jobs(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open job list: " + path);
    }
    std::string line;
    std::getline(in, line);
    const std::vector<std::string> header = split_tab(line);
    const std::vector<std::string> expected = {
        "island_id", "system", "variables", "merit", "population_file",
        "snapshot_dir", "output_dir", "seed", "population", "generations"};
    const std::vector<std::string> expected_with_profile = {
        "island_id", "system", "variables", "merit", "population_file",
        "snapshot_dir", "output_dir", "seed", "population", "generations",
        "profile"};
    const std::vector<std::string> expected_with_material_contract = {
        "island_id", "system", "variables", "merit", "population_file",
        "snapshot_dir", "output_dir", "seed", "population", "generations",
        "profile", "internal_scaling"};
    const std::vector<std::string> expected_with_glass_contract = {
        "island_id", "system", "variables", "merit", "population_file",
        "snapshot_dir", "output_dir", "seed", "population", "generations",
        "profile", "internal_scaling", "glass_search"};
    const std::vector<std::string> expected_with_trace_contract = {
        "island_id", "system", "variables", "merit", "population_file",
        "snapshot_dir", "output_dir", "seed", "population", "generations",
        "profile", "constraint_handling", "allow_near_trace_infeasible",
        "trace_coverage_epsilon"};
    const bool has_profile =
        header == expected_with_profile || header == expected_with_material_contract ||
        header == expected_with_glass_contract ||
        header == expected_with_trace_contract;
    const bool has_internal_scaling =
        header == expected_with_material_contract || header == expected_with_glass_contract;
    const bool has_glass_search = header == expected_with_glass_contract;
    const bool has_trace_contract =
        header == expected_with_trace_contract;
    if (header != expected && !has_profile) {
        throw std::runtime_error("unexpected job-list header");
    }
    std::vector<Job> jobs;
    int line_no = 1;
    while (std::getline(in, line)) {
        ++line_no;
        if (line.empty()) {
            continue;
        }
        const std::vector<std::string> cells = split_tab(line);
        if (cells.size() != header.size()) {
            throw std::runtime_error(
                "job-list line " + std::to_string(line_no) + " has wrong column count");
        }
        Job job;
        job.island_id = std::stoi(cells[0]);
        job.system_path = cells[1];
        job.variables_path = cells[2];
        job.merit_path = cells[3];
        job.population_path = cells[4];
        job.snapshot_dir = cells[5];
        job.output_dir = cells[6];
        job.seed = static_cast<unsigned int>(std::stoul(cells[7]));
        job.population = std::stoi(cells[8]);
        job.generations = std::stoi(cells[9]);
        if (has_profile) {
            job.profile = cells[10];
        }
        if (has_internal_scaling) {
            job.internal_scaling = cells[11];
        }
        if (has_glass_search) {
            job.glass_search_path = cells[12];
        }
        if (has_trace_contract) {
            job.has_trace_contract = true;
            job.constraint_handling = cells[11];
            const std::string allow_value = cells[12];
            job.allow_near_trace_infeasible =
                allow_value == "1" || allow_value == "true" || allow_value == "yes";
            job.trace_coverage_epsilon = std::clamp(
                std::stod(cells[13]), 0.0, 1.0);
        }
        jobs.push_back(std::move(job));
    }
    if (jobs.empty()) {
        throw std::runtime_error("job list is empty");
    }
    return jobs;
}

} // namespace lensde::broker
