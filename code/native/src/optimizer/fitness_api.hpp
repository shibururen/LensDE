#pragma once
#include "support_types.hpp"

namespace optiland { namespace optimizer_detail {

CandidateFitness make_candidate_fitness(
    const Score& score,
    const MeritConfig& config,
    const AdaptiveHybridDeDlsOptions& options);

Score score_from_cuda_summary(const CudaScoreSummary& summary, const MeritConfig& config);

bool finite_less(double a, double b, double tolerance = 1.0e-12);

void classify_polish_merit_delta(
    double before_merit,
    double after_merit,
    int& strict_improvements,
    int& neutral_results,
    int& worse_results);

bool unified_scalar_merit_score(const Score& score);

bool merit_less_with_tiebreaks(const CandidateFitness& a, const CandidateFitness& b);

bool polish_merit_not_worse(const CandidateFitness& polished, const CandidateFitness& source);

bool candidate_better(
    const CandidateFitness& a,
    const CandidateFitness& b,
    const AdaptiveHybridDeDlsOptions& options);

} }  // namespace optiland::optimizer_detail
