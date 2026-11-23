#pragma once
#include "support.hpp"

namespace optiland { namespace optimizer_detail {
// Hybrid epochs retain the original random stream and stage ordering.
class HybridSession {
public:
    HybridSession(DifferentialEvolution& owner, const HybridDeDlsOptions& options,
                  ProgressCallback progress)
        : owner(owner), base_(owner.base_), variables_(owner.variables_),
          merit_config_(owner.merit_config_), glass_search_(owner.glass_search_),
          options(options), progress(std::move(progress)) {}
    OptimizationResult run();
private:
    DifferentialEvolution& owner;
    const LensSystem& base_;
    const std::vector<VariableSpec>& variables_;
    const MeritConfig& merit_config_;
    const GlassSearchContext& glass_search_;
    const HybridDeDlsOptions& options;
    ProgressCallback progress;
    Clock::time_point run_start{};
    int dim{};
    int sizing_dim{};
    InternalScalingMode internal_mode{};
    DifferentialEvolutionOptions de_options{};
    PolishOptions polish_options{};
    int pop_size{};
    int outer_epochs{};
    int inner_generations{};
    int parent_count{};
    int requested_refresh_count{};
    int refresh_count{};
    DeTimingStats timing{};
    Clock::time_point phase_start{};
    std::mt19937 rng{};
    std::uniform_real_distribution<double> uni{};
    std::uniform_real_distribution<double> f_dist{};
    std::string strategy{};
    std::vector<double> population{};
    std::vector<double> initial_values{};
    std::vector<double> initial_scaled{};
    bool used_cuda{};
    bool eval_used_cuda{};
    std::vector<double> physical_population{};
    std::vector<double> merits{};
    Score initial_score{};
    std::vector<double> best_scaled{};
    Score best_score{};
    int total_de_generations{};
    double de_seconds{};
    double polish_seconds{};
    int polish_runs{};
    int polish_improvements{};
    std::vector<int> parent_indices{};
    int epoch_polish_improvements{};
    int epoch_polish_merit_strict_improvements{};
    int epoch_polish_merit_neutral{};
    int epoch_polish_merit_worse{};
    bool epoch_polish_used_cuda{};
    int refreshed_count{};
    Score evaluate_score_for_scaled(const std::vector<double>& scaled);
    std::pair<std::vector<double>, Score> update_best_from_population();
    void emit_hybrid_trace(
        const std::string& event,
        int epoch,
        double phase_seconds,
        double epoch_start_merit,
        double before_merit,
        const Score& score,
        const std::vector<double>& scaled,
        int event_de_generations,
        int event_polish_runs,
        int event_polish_improvements,
        int event_refreshed_count,
        bool event_used_cuda,
        int event_polish_merit_strict_improvements = 0,
        int event_polish_merit_neutral = 0,
        int event_polish_merit_worse = 0);
    void initialize();
    PolishThreadContext make_polish_context(int idx);
    void run_de(int outer, double epoch_start_merit);
    void run_polish(int outer, double epoch_start_merit);
    void run_refresh(int outer, double epoch_start_merit);
    void run_epoch(int outer);
    std::vector<double> evaluate_population(const std::vector<double>& rows, int count,
        bool cuda, bool* used_cuda, DeTimingStats* timing = nullptr) const {
        return owner.evaluate_population(rows, count, cuda, used_cuda, timing);
    }
    std::vector<Score> evaluate_population_scores_scaled(const std::vector<double>& rows,
        int count, InternalScalingMode mode, bool cuda, bool* used_cuda,
        DeTimingStats* timing = nullptr) const {
        return owner.evaluate_population_scores_scaled(rows, count, mode, cuda, used_cuda, timing);
    }
};
} }  // namespace optiland::optimizer_detail
