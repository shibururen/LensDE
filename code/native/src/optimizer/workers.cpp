/*
 * Dispatch population scoring and polish work through batch-owned worker contexts.
 * Evaluation and session polish phases retain the context/result arrays until their workers have joined.
 * Preserve worker partitioning, input-row/result mapping and join-before-destruction lifetimes.
 */
#include "support.hpp"

namespace optiland {
using namespace optimizer_detail;

namespace optimizer_detail {

PolishWorkResult run_polish_context(const PolishThreadContext& context) {
    PolishWorkResult work;
    work.index = context.index;
    work.before_merit = context.before_merit;
    OptimizationResult polished =
        context.optimizer->polish_damped_least_squares(context.before_values, *context.options);
    work.score = polished.best_score;
    work.improved =
        std::isfinite(polished.best_score.merit) &&
        (!context.rollback || polished.best_score.merit <= context.before_merit);
    work.used_cuda = polished.used_cuda;
    if (work.improved) {
        work.polished_scaled = physical_to_scaled(
            *context.variables,
            polished.best_values,
            context.options ? context.options->internal_scaling : InternalScalingMode::LegacyOptical);
    }
    return work;
}

#ifdef _WIN32
DWORD WINAPI run_polish_thread(LPVOID parameter) {
    auto* context = static_cast<PolishThreadContext*>(parameter);
    try {
        context->result = run_polish_context(*context);
    } catch (...) {
        context->error = std::current_exception();
    }
    return 0;
}
#endif

#ifdef _WIN32
DWORD WINAPI run_function_thread(LPVOID parameter) {
    auto* context = static_cast<FunctionThreadContext*>(parameter);
    try {
        context->fn();
    } catch (...) {
        context->error = std::current_exception();
    }
    return 0;
}
#endif

int available_worker_count() {
    int configured = 0;
    if (const char* env = std::getenv("OPTILAND_CPU_WORKERS")) {
        try {
            configured = std::stoi(env);
        } catch (...) {
            configured = 0;
        }
    }
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    const int detected = std::max(1, static_cast<int>(info.dwNumberOfProcessors));
#else
    const int detected = 1;
#endif
    if (configured > 0) {
        return std::max(1, std::min(configured, detected));
    }
    return detected;
}

int cuda_adaptive_min_batch_size(int override_min_batch_size ) {
    if (override_min_batch_size > 0) {
        return override_min_batch_size;
    }
    constexpr int kDefaultMinBatchSize = 4096;
    if (const char* env = std::getenv("OPTILAND_CUDA_ADAPTIVE_MIN_BATCH")) {
        try {
            const int configured = std::stoi(env);
            if (configured > 0) {
                return configured;
            }
        } catch (...) {
        }
    }
    return kDefaultMinBatchSize;
}

void evaluate_population_candidate(
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const GlassSearchContext& glass_search,
    const MeritConfig& merit_config,
    const std::vector<double>& flat_population,
    int dim,
    int i,
    std::vector<double>& merits) {
    const double* row =
        flat_population.data() + static_cast<std::size_t>(i) * dim;
    const LensSystem lens =
        prepare_for_merit(
            apply_mixed_variables_row(
                base,
                variables,
                row,
                static_cast<std::size_t>(dim),
                glass_search),
            merit_config);
    merits[static_cast<std::size_t>(i)] = evaluate_merit(lens, merit_config).merit;
}

#ifdef _WIN32
DWORD WINAPI run_cpu_population_eval_thread(LPVOID parameter) {
    auto* context = static_cast<CpuPopulationEvalThreadContext*>(parameter);
    while (true) {
        const int index = static_cast<int>(InterlockedIncrement(&context->next_index)) - 1;
        if (index >= context->population_size) {
            break;
        }
        try {
            evaluate_population_candidate(
                *context->base,
                *context->variables,
                *context->glass_search,
                *context->merit_config,
                *context->flat_population,
                context->dim,
                index,
                *context->merits);
        } catch (...) {
            EnterCriticalSection(&context->error_lock);
            if (!context->first_error) {
                context->first_error = std::current_exception();
            }
            LeaveCriticalSection(&context->error_lock);
            break;
        }
    }
    return 0;
}
#endif

}  // namespace optimizer_detail

}  // namespace optiland
