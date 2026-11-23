// Run each loaded island and coordinate serial or parallel island execution.
// All participating optimizers share the configured population broker when batching is enabled.
// Preserve per-island seed/options, result indexing, exception transport, and worker join boundaries.

#include "interfaces.hpp"

namespace lensde::broker {

void run_island(const LoadedJob& item, RunRecord& record, const std::shared_ptr<optiland::CudaBatchedPopulationBroker>& broker) {
            try {
                optiland::DifferentialEvolution optimizer(
                    item.system,
                    item.variables,
                    item.merit);
                optimizer.set_glass_search_context(item.glass);
                if (broker) {
                    optimizer.set_population_evaluation_broker(
                        broker, item.job.island_id);
                }
                optiland::AdaptiveHybridDeDlsOptions options =
                    checkpoint_options(item);
                const auto timeline_start = std::chrono::steady_clock::now();
                const fs::path timeline_path =
                    fs::path(item.job.output_dir) / "timeline.jsonl";
                std::ofstream timeline(timeline_path, std::ios::trunc);
                timeline << std::setprecision(17);
                options.trace = [&](const optiland::HybridTraceEvent& event) {
                    const double wall_seconds = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - timeline_start).count();
                    timeline
                        << "{\"event\":\"" << event.event << "\""
                        << ",\"wall_seconds\":" << wall_seconds
                        << ",\"epoch\":" << event.epoch
                        << ",\"population_size\":" << event.population_size
                        << ",\"de_generations\":" << event.de_generations
                        << ",\"phase_seconds\":" << event.phase_seconds
                        << ",\"cumulative_de_seconds\":" << event.cumulative_de_seconds
                        << ",\"cumulative_polish_seconds\":"
                        << event.cumulative_polish_seconds
                        << ",\"lpsr_target_population\":"
                        << event.lpsr_target_population
                        << ",\"lpsr_reduction_count\":"
                        << event.lpsr_reduction_count
                        << ",\"polish_runs\":" << event.polish_runs
                        << ",\"polish_improvements\":" << event.polish_improvements
                        << ",\"population_best_merit\":";
                    if (std::isfinite(event.population_best_score.merit)) {
                        timeline << event.population_best_score.merit;
                    } else {
                        timeline << "null";
                    }
                    timeline << ",\"population_best_mean_spot\":";
                    if (std::isfinite(event.population_best_score.mean_spot)) {
                        timeline << event.population_best_score.mean_spot;
                    } else {
                        timeline << "null";
                    }
                    timeline << "}\n";
                    timeline.flush();
                };
                record.result = optimizer.run_adaptive_hybrid_de_dls(options, {});
            } catch (const std::exception& exc) {
                record.error = exc.what();
            } catch (...) {
                record.error = "unknown optimizer exception";
            }
        }

void execute_islands(const std::vector<LoadedJob>& loaded, std::vector<RunRecord>& records, const std::shared_ptr<optiland::CudaBatchedPopulationBroker>& broker, bool batched) {
if (batched) {
            std::mutex start_mutex;
            std::condition_variable start_cv;
            bool start = false;
            std::vector<std::thread> threads;
            threads.reserve(loaded.size());
            for (std::size_t i = 0; i < loaded.size(); ++i) {
                threads.emplace_back([&, i] {
                    {
                        std::unique_lock<std::mutex> lock(start_mutex);
                        start_cv.wait(lock, [&] { return start; });
                    }
                    run_island(loaded[i], records[i], broker);
                });
            }
            {
                std::lock_guard<std::mutex> lock(start_mutex);
                start = true;
            }
            start_cv.notify_all();
            for (std::thread& thread : threads) {
                thread.join();
            }
        } else {
            for (std::size_t i = 0; i < loaded.size(); ++i) {
                run_island(loaded[i], records[i], broker);
            }
        }
}

} // namespace lensde::broker
