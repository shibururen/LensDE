#pragma once
#include "support_types.hpp"

namespace optiland { namespace optimizer_detail {

PolishWorkResult run_polish_context(const PolishThreadContext& context);

#ifdef _WIN32
DWORD WINAPI run_polish_thread(LPVOID parameter);
#endif

#ifdef _WIN32
DWORD WINAPI run_function_thread(LPVOID parameter);
#endif

int available_worker_count();

int cuda_adaptive_min_batch_size(int override_min_batch_size = 0);

void evaluate_population_candidate(
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const GlassSearchContext& glass_search,
    const MeritConfig& merit_config,
    const std::vector<double>& flat_population,
    int dim,
    int i,
    std::vector<double>& merits);

#ifdef _WIN32
DWORD WINAPI run_cpu_population_eval_thread(LPVOID parameter);
#endif

} }  // namespace optiland::optimizer_detail
