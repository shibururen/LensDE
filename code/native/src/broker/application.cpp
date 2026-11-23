// Assemble the multi-island application from loaded jobs, broker settings, execution, and output.
// The thin experiment main delegates here, including the explicit asphere-oracle fixture route.
// Keep process policy setup before island launch and publish results only after island completion.

#include "interfaces.hpp"

namespace lensde::broker {

std::vector<LoadedJob> load_islands(std::vector<Job>& jobs) {
std::vector<LoadedJob> loaded;
loaded.reserve(jobs.size());
for (Job& job : jobs) {
            fs::create_directories(job.snapshot_dir);
            fs::create_directories(job.output_dir);
            loaded.push_back(load_job(std::move(job)));
        }
    return loaded;
}

std::shared_ptr<optiland::CudaBatchedPopulationBroker> make_broker(int argc, char** argv, const std::vector<LoadedJob>& loaded, bool batched) {
std::shared_ptr<optiland::CudaBatchedPopulationBroker> broker;
if (batched) {
            optiland::BatchedPopulationBrokerOptions broker_options;
            const std::string coalesce = arg_value(argc, argv, "--coalesce-us");
            if (!coalesce.empty()) {
                broker_options.coalesce_microseconds = std::stoi(coalesce);
            }
            const std::string max_rows = arg_value(argc, argv, "--max-batch-rows");
            if (!max_rows.empty()) {
                broker_options.maximum_batch_candidates = std::stoi(max_rows);
            }
            std::vector<optiland::PopulationEvaluationIslandContext> contexts;
            contexts.reserve(loaded.size());
            for (const LoadedJob& item : loaded) {
                contexts.push_back({
                    item.job.island_id,
                    item.system,
                    item.variables,
                    item.glass});
            }
            broker = std::make_shared<optiland::CudaBatchedPopulationBroker>(
                std::move(contexts),
                loaded.front().merit,
                broker_options);
        }
    return broker;
}

int run_application(int argc, char** argv) {
    try {
        const std::string oracle_fixture =
            arg_value(argc, argv, "--asphere-oracle-fixture");
        if (!oracle_fixture.empty()) {
            return run_asphere_oracle_fixture(
                oracle_fixture,
                arg_value(argc, argv, "--asphere-oracle-output"));
        }
        const std::string jobs_path = arg_value(argc, argv, "--jobs");
        const std::string stats_path = arg_value(argc, argv, "--broker-stats");
        if (jobs_path.empty()) {
            std::cerr << "usage: optiland_multi_island_experiment --jobs jobs.tsv "
                         "[--batched] [--broker-stats path] [--coalesce-us 1000] "
                         "[--asphere-root-policy unified_certified (default)|production|confirmed_with_fallback|"
                         "fast_recover|certified_first_root|unified_certified] "
                         "[--calibrated-builder-schedule auto|fused|ordered] "
                         "[--transpose-ray-tasks auto|on|off] "
                         "[--evaluation-pruning off|trace|trace-rms|shadow-trace|shadow-rms]\n";
            return 2;
        }
        std::string requested_root_policy =
            arg_value(argc, argv, "--asphere-root-policy");
        if (requested_root_policy.empty()) {
            const char* environment_root_policy =
                std::getenv("OPTILAND_ASPHERE_ROOT_POLICY");
            if (environment_root_policy != nullptr) {
                requested_root_policy = environment_root_policy;
            }
        }
        const optiland::AsphereRootPolicy root_policy =
            parse_asphere_root_policy(requested_root_policy);
        optiland::set_asphere_root_policy(root_policy);
        const std::string builder_schedule =
            configure_calibrated_builder_schedule(
                arg_value(argc, argv, "--calibrated-builder-schedule"));
        const std::string transpose_ray_tasks =
            configure_transpose_ray_tasks(
                arg_value(argc, argv, "--transpose-ray-tasks"));
        std::cout << "Asphere multi-island root policy: "
                  << asphere_root_policy_name(root_policy) << "\n";
        std::cout << "Calibrated multi-island builder schedule: "
                  << builder_schedule << "\n";
        std::cout << "Multi-island transpose ray tasks: "
                  << transpose_ray_tasks << "\n";
        const std::string pruning=arg_value(argc,argv,"--evaluation-pruning");
        if(!pruning.empty()) {
            if(pruning!="off" && pruning!="trace" && pruning!="trace-rms" &&
               pruning!="shadow-trace" && pruning!="shadow-rms")
                throw std::invalid_argument("unknown --evaluation-pruning: "+pruning);
            set_process_environment("OPTILAND_DE_CUDA_EVALUATION_PRUNING",
                pruning=="off" ? "0" : pruning.find("shadow")==0 ? "1" : "2");
            set_process_environment("OPTILAND_DE_CUDA_OUTER_RMS_PRUNING",
                pruning.find("rms")!=std::string::npos ? "1" : "0");
        }
        std::vector<Job> jobs = load_jobs(jobs_path);
        std::vector<LoadedJob> loaded = load_islands(jobs);
        
        

        const bool batched = has_arg(argc, argv, "--batched");
        std::shared_ptr<optiland::CudaBatchedPopulationBroker> broker = make_broker(argc, argv, loaded, batched);
        

        std::vector<RunRecord> records(loaded.size());
        

        execute_islands(loaded, records, broker, batched);

        const bool failed = save_all_results(loaded, records);
        
        if (broker && !stats_path.empty()) {
            save_broker_stats(broker->stats(), stats_path);
        }
        return failed ? 3 : 0;
    } catch (const std::exception& exc) {
        std::cerr << exc.what() << '\n';
        return 2;
    }
}

} // namespace lensde::broker
