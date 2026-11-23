/*
 * Score adaptive rows and batches through CPU, CUDA or the configured evaluation broker.
 * Generation and archive phases share coordinate conversion, certificates and run_.fitness-related timing.
 * Keep result-to-row mapping, certificate lifetimes and worker completion aligned with the input batch.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

optiland::CandidateFitness AdaptiveSession::score_fitness_for_scaled(const std::vector<double>& scaled, int generation, int population_index, const std::string& source, bool polished) {
        const std::vector<double> values = scaled_to_physical(owner.variables_, scaled, run_.internal_mode);
        LensSystem candidate_system = apply_mixed_variables(owner.base_, owner.variables_, values, owner.glass_search_);
        CandidateFitness topology_summary;
        this->assign_topology_summary_for_system(topology_summary, candidate_system);
        const Score score = evaluate_merit(
            prepare_for_merit(std::move(candidate_system), owner.merit_config_),
            owner.merit_config_);
        CandidateFitness fitness = make_candidate_fitness(score, owner.merit_config_, options);
        fitness.generation = generation;
        fitness.population_index = population_index;
        fitness.source = source;
        fitness.polished = polished;
        fitness.topology_key = std::move(topology_summary.topology_key);
        fitness.semantic_key = std::move(topology_summary.semantic_key);
        fitness.descriptor_key = std::move(topology_summary.descriptor_key);
        fitness.continuous_topology_signature =
            std::move(topology_summary.continuous_topology_signature);
        fitness.descriptor_diagnostic = std::move(topology_summary.descriptor_diagnostic);
        fitness.descriptor_axis_values =
            std::move(topology_summary.descriptor_axis_values);
        this->apply_topology_avoidance(fitness, generation);
        return fitness;
    }

optiland::CandidateFitness AdaptiveSession::score_fitness_for_scaled_row(const double* scaled_row, int generation, int population_index, const std::string& source, bool polished) {
        std::vector<double> values;
        values.reserve(static_cast<std::size_t>(run_.dim));
        physical_values_from_scaled_row(
            owner.variables_,
            scaled_row,
            static_cast<std::size_t>(run_.dim),
            run_.internal_mode,
            &values);
        LensSystem candidate_system =
            apply_mixed_variables(owner.base_, owner.variables_, values, owner.glass_search_);
        CandidateFitness topology_summary;
        this->assign_topology_summary_for_system(topology_summary, candidate_system);
        const Score score = evaluate_merit(
            prepare_for_merit(std::move(candidate_system), owner.merit_config_),
            owner.merit_config_);
        CandidateFitness fitness = make_candidate_fitness(score, owner.merit_config_, options);
        fitness.generation = generation;
        fitness.population_index = population_index;
        fitness.source = source;
        fitness.polished = polished;
        fitness.topology_key = std::move(topology_summary.topology_key);
        fitness.semantic_key = std::move(topology_summary.semantic_key);
        fitness.descriptor_key = std::move(topology_summary.descriptor_key);
        fitness.continuous_topology_signature =
            std::move(topology_summary.continuous_topology_signature);
        fitness.descriptor_diagnostic = std::move(topology_summary.descriptor_diagnostic);
        fitness.descriptor_axis_values =
            std::move(topology_summary.descriptor_axis_values);
        this->apply_topology_avoidance(fitness, generation);
        return fitness;
    }

optiland::CandidateFitness AdaptiveSession::evaluate_fitness_for_scaled(const std::vector<double>& scaled, int generation, int population_index, const std::string& source, bool polished) {
        const auto eval_start = Clock::now();
        CandidateFitness fitness =
            this->score_fitness_for_scaled(scaled, generation, population_index, source, polished);
        ++run_.timing.evaluation_calls;
        ++run_.timing.evaluated_candidates;
        run_.timing.evaluation_seconds += seconds_since(eval_start);
        return fitness;
    }

optiland::CandidateFitness AdaptiveSession::scalar_fitness_from_merit(double merit_value, int generation, int population_index, const std::string& source) {
        CandidateFitness fitness;
        Score& score = fitness.score;
        score.merit_model = owner.merit_config_.merit_model;
        score.distortion_model = owner.merit_config_.distortion_model;
        score.merit = finite_or(merit_value, owner.merit_config_.fail_merit);
        score.uncapped_merit = score.merit;
        score.trace_ok = std::isfinite(score.merit) && score.merit < owner.merit_config_.fail_merit;
        fitness.objective = finite_or(score.merit, owner.merit_config_.fail_merit);
        fitness.image_objective = fitness.objective;
        fitness.trace_feasible = true;
        fitness.constraint_feasible = true;
        fitness.first_order_feasible = true;
        fitness.feasible = true;
        fitness.generation = generation;
        fitness.population_index = population_index;
        fitness.source = source;
        return fitness;
    }

std::vector<optiland::CandidateFitness> AdaptiveSession::evaluate_population_fitness(const std::vector<double>& rows, int rows_count, int generation, const std::string& source) {
        std::vector<CandidateFitness> out(static_cast<std::size_t>(rows_count));
        const auto batch_start = Clock::now();
        const int cuda_min_batch_size =
            cuda_adaptive_min_batch_size(run_.de_options.cuda_min_batch_size);
        const bool use_cuda_for_batch =
            run_.de_options.use_cuda &&
            (rows_count >= cuda_min_batch_size || run_.population_cuda_locked);
        const bool refresh_gate_source =
            source.find("refresh") != std::string::npos &&
            (options.refresh_generation.trace_feasible_gate ||
             options.allow_near_trace_infeasible);
        if (run_.scalar_merit_fast_path && !refresh_gate_source) {
            run_.phase_start = Clock::now();
            const std::vector<double> physical_rows =
                scaled_population_to_physical_flat(owner.variables_, rows, rows_count, run_.internal_mode);
            run_.timing.physical_conversion_seconds += seconds_since(run_.phase_start);
            bool batch_used_cuda = false;
            const std::vector<double> batch_merits = owner.evaluate_population(
                physical_rows,
                rows_count,
                use_cuda_for_batch,
                &batch_used_cuda,
                &run_.timing);
            run_.used_cuda = run_.used_cuda || batch_used_cuda;
            run_.population_cuda_locked = run_.population_cuda_locked || batch_used_cuda;
            for (int i = 0; i < rows_count; ++i) {
                const double merit_value =
                    i < static_cast<int>(batch_merits.size())
                        ? batch_merits[static_cast<std::size_t>(i)]
                        : owner.merit_config_.fail_merit;
                CandidateFitness fitness =
                    this->scalar_fitness_from_merit(merit_value, generation, i, source);
                const double* values = physical_rows.data() +
                                       static_cast<std::size_t>(i) * run_.dim;
                this->assign_topology_keys_for_physical_row(
                    fitness,
                    values,
                    static_cast<std::size_t>(run_.dim));
                this->apply_topology_avoidance(fitness, generation);
                out[static_cast<std::size_t>(i)] = std::move(fitness);
            }
            if (source == "initial") {
                run_.timing.initial_evaluation_seconds += seconds_since(batch_start);
            }
            return out;
        }
        if (run_.cuda_score_summary_fast_path && use_cuda_for_batch &&
            distortion_model_allows_cuda(owner.merit_config_) &&
            (owner.population_evaluation_broker_ || evaluate_population_cuda_available())) {
            const auto summary_start = Clock::now();
            run_.phase_start = Clock::now();
            const std::vector<double> physical_rows =
                scaled_population_to_physical_flat(owner.variables_, rows, rows_count, run_.internal_mode);
            run_.timing.physical_conversion_seconds += seconds_since(run_.phase_start);
            std::vector<double> batch_merits;
            std::vector<CudaScoreSummary> summaries;
            bool evaluation_ok = false;
            std::string broker_error;
            if (owner.population_evaluation_broker_) {
                PopulationEvaluationResult broker_result =
                    owner.population_evaluation_broker_->evaluate(
                        owner.population_evaluation_island_index_,
                        owner.population_evaluation_logical_call_index_++,
                        physical_rows,
                        rows_count,
                        true);
                evaluation_ok = broker_result.success;
                broker_error = std::move(broker_result.error);
                batch_merits = std::move(broker_result.merits);
                summaries = std::move(broker_result.summaries);
            } else {
#ifdef OPTILAND_WITH_CUDA
                 DeviceCertificateReset device_certificate_reset;
                const auto direct_context = evaluation_local_context();
                const bool direct_certificate = source == "trial" && run_.evaluation_certificate_allowed;
                evaluation_set_certificate_context(direct_certificate ? direct_context.mode : 0,
                    direct_certificate ? direct_context.rows : std::vector<EvaluationCertificate>{},
                    direct_certificate ? direct_context.epsilon : 0.0);
#endif
                evaluation_ok = evaluate_population_cuda_scores(
                    owner.base_,
                    owner.variables_,
                    owner.merit_config_,
                    physical_rows,
                    rows_count,
                    &batch_merits,
                    &summaries,
                    &run_.timing.cuda,
                    &owner.glass_search_);
#ifdef OPTILAND_WITH_CUDA
                if (evaluation_ok && direct_certificate) {
                    const auto proofs = evaluation_certificate_results();
                    if (proofs.size() != direct_context.rows.size())
                        throw std::runtime_error("Direct certificate result shape mismatch");
                    int counts[4] = {};
                    for (std::size_t row=0;row<proofs.size();++row) {
                        const auto& proof=proofs[row];
                        if (!evaluation_parent_identity_equal(direct_context.rows[row],proof) ||
                            ((!proof.eligible && !proof.merit_eligible) && proof.reason) ||
                            proof.reason<0 || proof.reason>3)
                            throw std::runtime_error("Direct certificate row identity invariant violated");
                        ++counts[proof.reason];
                    }
                    evaluation_local_complete(direct_context,proofs);
                    const char* stats=std::getenv("OPTILAND_DE_CUDA_EVALUATION_PRUNING_STATS");
                    if(stats && std::strcmp(stats,"1")==0)
                        std::cout << "DE evaluation pruning: mode=" << direct_context.mode
                            << " rows=" << proofs.size() << " reason1=" << counts[1]
                            << " reason2=" << counts[2] << " reason3=" << counts[3] << "\n";
                }
#endif
            }
            if (source == "trial" && run_.evaluation_certificate_allowed) {
                if (!evaluation_ok)
                    throw std::runtime_error("Evaluation certificate batch failed; CPU fallback cannot consume partial certificates");
                run_.evaluation_batch_certificates = evaluation_local_results();
                if (run_.evaluation_batch_certificates.size() != static_cast<std::size_t>(rows_count))
                    throw std::runtime_error("Evaluation certificate row transport mismatch");
            }
            if (evaluation_ok &&
                static_cast<int>(summaries.size()) == rows_count) {
                run_.used_cuda = true;
                run_.population_cuda_locked = true;
                auto build_cuda_summary_fitness =
                    [&](int i, std::vector<double>& values) {
                    // A partial device summary must never become a full trial Score.
                    // The explicit side-channel status is consumed before any selection use.
                    if (source == "trial" && run_.evaluation_certificate_mode == 2 &&
                        run_.evaluation_certificate_allowed && run_.evaluation_batch_certificates[i].reason > 0) return;
                    Score score = score_from_cuda_summary(
                        summaries[static_cast<std::size_t>(i)],
                        owner.merit_config_);
                    if (i < static_cast<int>(batch_merits.size())) {
                        score.merit = finite_or(
                            batch_merits[static_cast<std::size_t>(i)],
                            score.merit);
                    }
                    CandidateFitness fitness =
                        make_candidate_fitness(score, owner.merit_config_, options);
                    fitness.generation = generation;
                    fitness.population_index = i;
                    fitness.source = source;
                    fitness.polished = false;
                    for (int j = 0; j < run_.dim; ++j) {
                        values[static_cast<std::size_t>(j)] =
                            physical_rows[static_cast<std::size_t>(i) * run_.dim + j];
                    }
                    this->assign_topology_keys_for_physical(fitness, values);
                    this->apply_topology_avoidance(fitness, generation);
                    out[static_cast<std::size_t>(i)] = std::move(fitness);
                };
                const int summary_workers = std::min(
                    rows_count,
                    available_worker_count());
                if (summary_workers <= 1 || rows_count < 2) {
                    std::vector<double> values(static_cast<std::size_t>(run_.dim), 0.0);
                    for (int i = 0; i < rows_count; ++i) {
                        build_cuda_summary_fitness(i, values);
                    }
                } else {
#ifdef _WIN32
                    volatile LONG next_index = 0;
                    std::vector<FunctionThreadContext> contexts(
                        static_cast<std::size_t>(summary_workers));
                    std::vector<HANDLE> handles;
                    handles.reserve(static_cast<std::size_t>(summary_workers));
                    for (int worker = 0; worker < summary_workers; ++worker) {
                        contexts[static_cast<std::size_t>(worker)].fn = [&]() {
                            std::vector<double> values(
                                static_cast<std::size_t>(run_.dim), 0.0);
                            while (true) {
                                const int i = static_cast<int>(
                                    InterlockedIncrement(&next_index)) - 1;
                                if (i >= rows_count) {
                                    break;
                                }
                                build_cuda_summary_fitness(i, values);
                            }
                        };
                        HANDLE handle = CreateThread(
                            nullptr,
                            0,
                            run_function_thread,
                            &contexts[static_cast<std::size_t>(worker)],
                            0,
                            nullptr);
                        if (handle) {
                            handles.push_back(handle);
                        } else {
                            contexts[static_cast<std::size_t>(worker)].fn();
                        }
                    }
                    if (!handles.empty()) {
                        WaitForMultipleObjects(
                            static_cast<DWORD>(handles.size()),
                            handles.data(),
                            TRUE,
                            INFINITE);
                        for (HANDLE handle : handles) {
                            CloseHandle(handle);
                        }
                    }
                    for (const auto& context : contexts) {
                        if (context.error) {
                            std::rethrow_exception(context.error);
                        }
                    }
#else
                    std::vector<double> values(static_cast<std::size_t>(run_.dim), 0.0);
                    for (int i = 0; i < rows_count; ++i) {
                        build_cuda_summary_fitness(i, values);
                    }
#endif
                }
                ++run_.timing.evaluation_calls;
                run_.timing.evaluated_candidates += rows_count;
                run_.timing.evaluation_seconds += seconds_since(summary_start);
                if (source == "initial") {
                    run_.timing.initial_evaluation_seconds += seconds_since(batch_start);
                }
                return out;
            }
            if (de_cuda_required()) {
                throw std::runtime_error(
                    "DE-CUDA required, but CUDA score summary evaluation rejected this batch" +
                    (broker_error.empty() ? std::string{} : ": " + broker_error));
            }
        } else if (use_cuda_for_batch && de_cuda_required()) {
            throw std::runtime_error(
                "DE-CUDA required, but the score summary CUDA path is unavailable");
        }
        const int workers = std::min(rows_count, available_worker_count());
        if (workers <= 1 || rows_count < 2) {
            for (int i = 0; i < rows_count; ++i) {
                out[static_cast<std::size_t>(i)] =
                    this->score_fitness_for_scaled_row(
                        rows.data() + static_cast<std::size_t>(i) * run_.dim,
                        generation,
                        i,
                        source,
                        false);
            }
        } else {
#ifdef _WIN32
            volatile LONG next_index = 0;
            std::vector<FunctionThreadContext> contexts(static_cast<std::size_t>(workers));
            std::vector<HANDLE> handles;
            handles.reserve(static_cast<std::size_t>(workers));
            for (int worker = 0; worker < workers; ++worker) {
                contexts[static_cast<std::size_t>(worker)].fn = [&]() {
                    while (true) {
                        const int i = static_cast<int>(InterlockedIncrement(&next_index)) - 1;
                        if (i >= rows_count) {
                            break;
                        }
                        out[static_cast<std::size_t>(i)] =
                            this->score_fitness_for_scaled_row(
                                rows.data() + static_cast<std::size_t>(i) * run_.dim,
                                generation,
                                i,
                                source,
                                false);
                    }
                };
                HANDLE handle = CreateThread(
                    nullptr,
                    0,
                    run_function_thread,
                    &contexts[static_cast<std::size_t>(worker)],
                    0,
                    nullptr);
                if (handle) {
                    handles.push_back(handle);
                } else {
                    contexts[static_cast<std::size_t>(worker)].fn();
                }
            }
            if (!handles.empty()) {
                WaitForMultipleObjects(
                    static_cast<DWORD>(handles.size()),
                    handles.data(),
                    TRUE,
                    INFINITE);
                for (HANDLE handle : handles) {
                    CloseHandle(handle);
                }
            }
            for (const auto& context : contexts) {
                if (context.error) {
                    std::rethrow_exception(context.error);
                }
            }
#else
            for (int i = 0; i < rows_count; ++i) {
                out[static_cast<std::size_t>(i)] =
                    this->score_fitness_for_scaled_row(
                        rows.data() + static_cast<std::size_t>(i) * run_.dim,
                        generation,
                        i,
                        source,
                        false);
            }
#endif
        }
        const double elapsed = seconds_since(batch_start);
        ++run_.timing.evaluation_calls;
        run_.timing.evaluated_candidates += rows_count;
        run_.timing.evaluation_seconds += elapsed;
        if (source == "initial") {
            run_.timing.initial_evaluation_seconds += elapsed;
        }
        return out;
    }

void AdaptiveSession::sync_merits() {
        run_.merits.assign(static_cast<std::size_t>(run_.pop_size), owner.merit_config_.fail_merit);
        for (int i = 0; i < run_.pop_size; ++i) {
            run_.merits[static_cast<std::size_t>(i)] =
                run_.fitness[static_cast<std::size_t>(i)].score.merit;
            run_.fitness[static_cast<std::size_t>(i)].population_index = i;
        }
    }

const std::string& AdaptiveSession::fitness_descriptor_key(const CandidateFitness& candidate) {
        return candidate.descriptor_key;
    }

bool AdaptiveSession::global_refill_blocked_fitness(const CandidateFitness& candidate) {
            return this->global_refill_blocked_source(candidate.source);
        }

void AdaptiveSession::rescore_cached_fitness(TraceableRefreshArchiveEntry& entry, const std::string& fallback_source) {
        if (entry.scaled_values.size() != static_cast<std::size_t>(run_.dim)) {
            return;
        }
        const int generation = entry.fitness.generation;
        const int population_index = entry.fitness.population_index;
        const std::string source =
            entry.fitness.source.empty() ? fallback_source : entry.fitness.source;
        const bool polished = entry.fitness.polished;
        entry.fitness = this->score_fitness_for_scaled(
            entry.scaled_values,
            generation,
            population_index,
            source,
            polished);
    }

MeritStats AdaptiveSession::merit_stats(std::vector<double> values) {
        values.erase(
            std::remove_if(values.begin(), values.end(), [](double value) {
                return !std::isfinite(value);
            }),
            values.end());
        MeritStats stats;
        stats.count = static_cast<int>(values.size());
        if (values.empty()) {
            return stats;
        }
        std::sort(values.begin(), values.end());
        const double sum = std::accumulate(values.begin(), values.end(), 0.0);
        stats.mean = sum / static_cast<double>(values.size());
        const std::size_t mid = values.size() / 2;
        stats.median = (values.size() % 2 == 0)
                           ? 0.5 * (values[mid - 1] + values[mid])
                           : values[mid];
        stats.worst = values.back();
        return stats;
    }

int AdaptiveSession::raw_merit_best_index() {
            int selected = -1;
            for (int index = 0; index < run_.pop_size; ++index) {
                const CandidateFitness& candidate =
                    run_.fitness[static_cast<std::size_t>(index)];
                if (selected < 0) {
                    selected = index;
                    continue;
                }
                const CandidateFitness& incumbent =
                    run_.fitness[static_cast<std::size_t>(selected)];
                const bool candidate_trace =
                    candidate.trace_feasible && candidate.score.trace_ok;
                const bool incumbent_trace =
                    incumbent.trace_feasible && incumbent.score.trace_ok;
                if (candidate_trace != incumbent_trace) {
                    if (candidate_trace) {
                        selected = index;
                    }
                    continue;
                }
                if (finite_less(candidate.score.merit, incumbent.score.merit) ||
                    (!finite_less(incumbent.score.merit, candidate.score.merit) &&
                     finite_less(candidate.total_violation,
                                 incumbent.total_violation))) {
                    selected = index;
                }
            }
            return selected;
        }

} } // namespace optiland::optimizer_detail
