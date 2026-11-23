#pragma once

#include "optiland/optimizer.hpp"

#include <memory>

namespace optiland {

struct BatchedPopulationBrokerOptions {
    int coalesce_microseconds = 1000;
    int maximum_batch_candidates = 4096;
};

struct BatchedPopulationBrokerStats {
    long long logical_requests = 0;
    long long physical_launches = 0;
    long long merged_requests = 0;
    long long candidate_rows = 0;
    int maximum_batch_rows = 0;
    double queue_wait_seconds = 0.0;
    double wall_seconds = 0.0;
    CudaEvaluationTiming cuda;
    long long certificate_eligible = 0;
    long long merit_certificate_eligible = 0;
    long long trace_feasible_parent_proofs = 0;
    long long trace_infeasible_parent_proofs = 0;
    long long rms_proofs = 0;
    long long certified_rejections = 0;
};

struct PopulationEvaluationIslandContext {
    int island_index = -1;
    LensSystem base;
    std::vector<VariableSpec> variables;
    GlassSearchContext glass_search;
};

class CudaBatchedPopulationBroker final : public PopulationEvaluationBroker {
public:
    CudaBatchedPopulationBroker(
        LensSystem base,
        std::vector<VariableSpec> variables,
        MeritConfig merit_config,
        GlassSearchContext glass_search,
        BatchedPopulationBrokerOptions options = {});
    CudaBatchedPopulationBroker(
        std::vector<PopulationEvaluationIslandContext> island_contexts,
        MeritConfig merit_config,
        BatchedPopulationBrokerOptions options = {});
    ~CudaBatchedPopulationBroker() override;

    CudaBatchedPopulationBroker(const CudaBatchedPopulationBroker&) = delete;
    CudaBatchedPopulationBroker& operator=(const CudaBatchedPopulationBroker&) = delete;

    PopulationEvaluationResult evaluate(
        int island_index,
        std::uint64_t logical_call_index,
        const std::vector<double>& physical_population,
        int population_size,
        bool need_score_summaries) override;

    bool supports_evaluation_certificates() const override { return true; }
    BatchedPopulationBrokerStats stats() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace optiland
