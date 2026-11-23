#pragma once
#include "optiland/core.hpp"
#include "optiland/optimizer/session_fwd.hpp"
#include "optiland/optimizer/de_options.hpp"
#include "optiland/optimizer/dls_options.hpp"
#include "optiland/optimizer/hybrid_options.hpp"
#include "optiland/optimizer/adaptive_options.hpp"
#include "optiland/optimizer/progress.hpp"
#include "optiland/optimizer/results.hpp"
#include "optiland/optimizer/cuda_evaluation.hpp"
#include <memory>
#include <cstdint>
#include <vector>

namespace optiland {
class DifferentialEvolution {
    friend class optimizer_detail::AdaptiveSession;
    friend class optimizer_detail::DlsSession;
    friend class optimizer_detail::HybridSession;
public:
    DifferentialEvolution(
        LensSystem base,
        std::vector<VariableSpec> variables,
        MeritConfig merit_config);

    void set_glass_search_context(GlassSearchContext context);

    void set_population_evaluation_broker(
        std::shared_ptr<PopulationEvaluationBroker> broker,
        int island_index);

    OptimizationResult run(
        const DifferentialEvolutionOptions& options,
        ProgressCallback progress = {});

    OptimizationResult polish_damped_least_squares(
        const std::vector<double>& initial_values,
        const PolishOptions& options,
        ProgressCallback progress = {}) const;

    

    OptimizationResult run_hybrid_de_dls(
        const HybridDeDlsOptions& options,
        ProgressCallback progress = {});

    OptimizationResult run_adaptive_hybrid_de_dls(
        const AdaptiveHybridDeDlsOptions& options,
        ProgressCallback progress = {});

private:
    std::vector<double> evaluate_population_cpu(
        const std::vector<double>& flat_population,
        int population_size) const;

    std::vector<double> evaluate_population(
        const std::vector<double>& flat_population,
        int population_size,
        bool prefer_cuda,
        bool* used_cuda,
        DeTimingStats* timing = nullptr) const;

    std::vector<Score> evaluate_population_scores_scaled(
        const std::vector<double>& scaled_population,
        int population_size,
        InternalScalingMode internal_mode,
        bool prefer_cuda,
        bool* used_cuda,
        DeTimingStats* timing = nullptr) const;

    LensSystem base_;
    std::vector<VariableSpec> variables_;
    MeritConfig merit_config_;
    GlassSearchContext glass_search_;
    std::shared_ptr<PopulationEvaluationBroker> population_evaluation_broker_;
    int population_evaluation_island_index_ = -1;
    mutable std::uint64_t population_evaluation_logical_call_index_ = 0;
};


}  // namespace optiland
