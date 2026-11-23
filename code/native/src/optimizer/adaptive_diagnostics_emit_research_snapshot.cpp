/*
 * Serialize ordered adaptive population and basin snapshots at requested generation boundaries.
 * Initialization and generation completion read run_ here to publish index and candidate-event files.
 * Preserve ranking, row order and numeric formatting; snapshot observation must not advance the RNG.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

void AdaptiveSession::emit_research_snapshot(int generation) {
        if (run_.research_snapshot_dir.empty()) {
            return;
        }
        std::ofstream index(
            research_snapshot_path(run_.research_snapshot_dir, "snapshot_index.jsonl"),
            std::ios::app);
        std::ofstream events(
            research_snapshot_path(run_.research_snapshot_dir, "candidate_events.jsonl"),
            std::ios::app);
        if (!index || !events) {
            return;
        }
        index << std::setprecision(17);
        events << std::setprecision(17);
        index << "{\"generation\":" << generation
              << ",\"population_size\":" << run_.pop_size
              << ",\"basin_count\":" << run_.basins.size()
              << ",\"best_merit\":";
        research_write_json_number(index, run_.best_fitness.score.merit);
        index << ",\"used_cuda\":" << (run_.used_cuda ? "true" : "false")
              << "}\n";

        std::vector<int> population_order(static_cast<std::size_t>(run_.pop_size));
        std::iota(population_order.begin(), population_order.end(), 0);
        std::stable_sort(
            population_order.begin(),
            population_order.end(),
            [&](int lhs, int rhs) {
                const double a = run_.fitness[static_cast<std::size_t>(lhs)].score.merit;
                const double b = run_.fitness[static_cast<std::size_t>(rhs)].score.merit;
                if (std::isfinite(a) != std::isfinite(b)) {
                    return std::isfinite(a);
                }
                return a < b;
            });
        std::vector<int> population_rank(static_cast<std::size_t>(run_.pop_size), 0);
        for (int rank = 0; rank < run_.pop_size; ++rank) {
            population_rank[static_cast<std::size_t>(
                population_order[static_cast<std::size_t>(rank)])] = rank + 1;
        }
        const int dim = static_cast<int>(owner.variables_.size());
        for (int i = 0; i < run_.pop_size; ++i) {
            const CandidateFitness& candidate = run_.fitness[static_cast<std::size_t>(i)];
            const auto begin = run_.population.begin() + static_cast<std::ptrdiff_t>(i) * dim;
            const std::vector<double> scaled(begin, begin + dim);
            const std::vector<double> physical =
                scaled_to_physical(owner.variables_, scaled, run_.internal_mode);
            events << "{\"record\":\"population\",\"snapshot_generation\":"
                   << generation << ",\"population_index\":" << i
                   << ",\"merit_rank\":"
                   << population_rank[static_cast<std::size_t>(i)]
                   << ",\"state_generation\":" << candidate.generation
                   << ",\"source\":\""
                   << research_json_escape(candidate.source) << "\""
                   << ",\"merit\":";
            research_write_json_number(events, candidate.score.merit);
            events << ",\"rank1_merit\":";
            research_write_json_number(events, run_.best_fitness.score.merit);
            events << ",\"objective\":";
            research_write_json_number(events, candidate.objective);
            events << ",\"total_violation\":";
            research_write_json_number(events, candidate.total_violation);
            events << ",\"trace_feasible\":"
                   << (candidate.trace_feasible ? "true" : "false")
                   << ",\"constraint_feasible\":"
                   << (candidate.constraint_feasible ? "true" : "false")
                   << ",\"first_order_feasible\":"
                   << (candidate.first_order_feasible ? "true" : "false")
                   << ",\"topology_key\":\""
                   << research_json_escape(candidate.topology_key) << "\""
                   << ",\"semantic_key\":\""
                   << research_json_escape(candidate.semantic_key) << "\""
                   << ",\"descriptor_key\":\""
                   << research_json_escape(candidate.descriptor_key) << "\""
                   << ",\"values\":[";
            for (std::size_t j = 0; j < physical.size(); ++j) {
                if (j > 0) {
                    events << ',';
                }
                research_write_json_number(events, physical[j]);
            }
            events << "]}\n";
        }

        std::vector<std::size_t> basin_order(run_.basins.size());
        std::iota(basin_order.begin(), basin_order.end(), std::size_t{0});
        std::stable_sort(
            basin_order.begin(),
            basin_order.end(),
            [&](std::size_t lhs, std::size_t rhs) {
                const double a = run_.basins[lhs].fitness.score.merit;
                const double b = run_.basins[rhs].fitness.score.merit;
                if (std::isfinite(a) != std::isfinite(b)) {
                    return std::isfinite(a);
                }
                return a < b;
            });
        std::vector<int> basin_rank(run_.basins.size(), 0);
        for (std::size_t rank = 0; rank < basin_order.size(); ++rank) {
            basin_rank[basin_order[rank]] = static_cast<int>(rank) + 1;
        }
        for (std::size_t i = 0; i < run_.basins.size(); ++i) {
            const BasinElite& basin = run_.basins[i];
            const CandidateFitness& candidate = basin.fitness;
            events << "{\"record\":\"basin\",\"snapshot_generation\":"
                   << generation << ",\"basin_index\":" << i
                   << ",\"basin_id\":" << basin.id
                   << ",\"merit_rank\":" << basin_rank[i]
                   << ",\"state_generation\":" << basin.generation
                   << ",\"source\":\""
                   << research_json_escape(
                          basin.source.empty() ? candidate.source : basin.source)
                   << "\",\"merit\":";
            research_write_json_number(events, candidate.score.merit);
            events << ",\"rank1_merit\":";
            research_write_json_number(events, run_.best_fitness.score.merit);
            events << ",\"objective\":";
            research_write_json_number(events, candidate.objective);
            events << ",\"total_violation\":";
            research_write_json_number(events, candidate.total_violation);
            events << ",\"trace_feasible\":"
                   << (candidate.trace_feasible ? "true" : "false")
                   << ",\"constraint_feasible\":"
                   << (candidate.constraint_feasible ? "true" : "false")
                   << ",\"first_order_feasible\":"
                   << (candidate.first_order_feasible ? "true" : "false")
                   << ",\"nearest_distance\":";
            research_write_json_number(events, basin.nearest_distance);
            events << ",\"active_eligible\":"
                   << (basin.active_eligible ? "true" : "false")
                   << ",\"similarity_family_id\":"
                   << basin.similarity_family_id
                   << ",\"similarity_family_key\":\""
                   << research_json_escape(basin.similarity_family_key) << "\""
                   << ",\"topology_key\":\""
                   << research_json_escape(
                          basin.topology_key.empty() ? candidate.topology_key : basin.topology_key)
                   << "\",\"semantic_key\":\""
                   << research_json_escape(
                          basin.semantic_key.empty() ? candidate.semantic_key : basin.semantic_key)
                   << "\",\"descriptor_key\":\""
                   << research_json_escape(
                          basin.descriptor_key.empty() ? candidate.descriptor_key : basin.descriptor_key)
                   << "\",\"values\":[";
            for (std::size_t j = 0; j < basin.physical_values.size(); ++j) {
                if (j > 0) {
                    events << ',';
                }
                research_write_json_number(events, basin.physical_values[j]);
            }
            events << "]}\n";
        }
    }

} } // namespace optiland::optimizer_detail
