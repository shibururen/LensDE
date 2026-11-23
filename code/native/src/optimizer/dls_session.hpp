#pragma once
#include "support.hpp"

namespace optiland { namespace optimizer_detail {
// A single DLS invocation owns its residual cache, damping and trust-region state.
class DlsSession {
public:
    DlsSession(const DifferentialEvolution& owner, const std::vector<double>& initial_values,
               const PolishOptions& options, ProgressCallback progress)
        : owner(owner), base_(owner.base_), variables_(owner.variables_),
          merit_config_(owner.merit_config_), glass_search_(owner.glass_search_),
          initial_values(initial_values), options(options), progress(std::move(progress)) {}
    OptimizationResult run();
private:
    const DifferentialEvolution& owner;
    const LensSystem& base_;
    const std::vector<VariableSpec>& variables_;
    const MeritConfig& merit_config_;
    const GlassSearchContext& glass_search_;
    const std::vector<double>& initial_values;
    const PolishOptions& options;
    ProgressCallback progress;
    Clock::time_point polish_start{};
    int dim{};
    std::vector<double> initial{};
    InternalScalingMode internal_mode{};
    std::vector<double> x{};
    bool batch_merit_polish{};
    bool polish_used_cuda{};
    DeTimingStats polish_timing{};
    bool trust_region_polish{};
    double trust_min_radius{};
    double trust_max_radius{};
    double trust_radius{};
    double trust_acceptance_ratio{};
    Score best{};
    std::vector<double> best_merit_x{};
    Score best_merit_score{};
    std::vector<double> best_residuals{};
    double best_sum{};
    double auto_constraint_gate_floor{};
    double auto_first_order_gate_floor{};
    double auto_spot_ratio_ceiling{};
    double auto_wavefront_ratio_ceiling{};
    double auto_spot_absolute_ceiling{};
    double auto_wavefront_absolute_ceiling{};
    double lambda{};
    int consecutive_rejections{};
    int max_rejected_steps{};
    std::vector<double> cached_jacobian{};
    std::vector<double> cached_jtj{};
    std::vector<double> cached_gradient{};
    int cached_residual_count{};
    bool cached_linear_model_valid{};
    int consecutive_small_accepted_steps{};
    std::vector<double> plateau_best_history{};
    int plateau_window{};
    int plateau_min_iterations{};
    double plateau_min_abs_improvement{};
    double plateau_min_rel_improvement{};
    bool plateau_early_stop_enabled{};
    int completed_polish_iterations{};
struct TrialEvaluation {
            std::vector<double> trial;
            std::vector<double> actual_step;
            std::vector<double> residuals;
            Score score;
            double actual_step_norm = 0.0;
            double trial_sum = std::numeric_limits<double>::infinity();
            double actual_reduction = -std::numeric_limits<double>::infinity();
            double predicted = std::numeric_limits<double>::infinity();
            double rho = -std::numeric_limits<double>::infinity();
            bool residual_accept_step = false;
            bool merit_accept_step = false;
            bool trial_merit_improved = false;
        };
    Score score_for_physical(const std::vector<double>& values);
    Score score_for_scaled(const std::vector<double>& scaled_values);
    double scalar_merit_residual(double merit);
    std::vector<double> residuals_for_score(const Score& score, std::size_t expected);
    std::vector<double> residuals_for_scaled(const std::vector<double>& scaled_values, std::size_t expected);
    double vector_norm(const std::vector<double>& values);
    double predicted_reduction(const std::vector<double>& actual_step,
                                   const std::vector<double>& gradient,
                                   const std::vector<double>& jtj);
    double auto_ratio_ceiling(double initial_ratio);
    double auto_absolute_ceiling(double initial_value);
    void initialize();
    void update_linear_model(int iter, int residual_count);
    void write_linear_model_diagnostics(int iter, int residual_count);
    double project_trial(double alpha, const std::vector<double>& step,
                                        std::vector<double>* trial_out,
                                        std::vector<double>* actual_step_out);
    bool select_trial(const std::vector<double>& step, const std::vector<double>& trial, const std::vector<double>& actual_step, double actual_step_norm, double previous_best_merit, double merit_tolerance, double previous_sum, TrialEvaluation& chosen_trial);
    void iterate();
    std::vector<double> evaluate_population(const std::vector<double>& rows, int count,
        bool cuda, bool* used_cuda, DeTimingStats* timing) const {
        return owner.evaluate_population(rows, count, cuda, used_cuda, timing);
    }
    std::vector<Score> evaluate_population_scores_scaled(const std::vector<double>& rows,
        int count, InternalScalingMode mode, bool cuda, bool* used_cuda, DeTimingStats* timing) const {
        return owner.evaluate_population_scores_scaled(rows, count, mode, cuda, used_cuda, timing);
    }
};
} }  // namespace optiland::optimizer_detail
