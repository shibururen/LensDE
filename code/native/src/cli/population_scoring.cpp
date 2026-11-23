// Evaluate explicit population rows on CPU and coordinate the score-output worker threads.
// Population CSV publication calls this path when complete cached fitness is unavailable.
// Workers must retain candidate indices and isolated evaluation context when writing ordered results.

#include "cli_internal.hpp"

namespace lensde { namespace cli { namespace detail {

int population_score_worker_count(int rows) {
    if (rows <= 1) {
        return 1;
    }
    int configured = 0;
    for (const char* name : {"OPTILAND_CPU_SCORE_WORKERS", "OPTILAND_CPU_WORKERS"}) {
        if (const char* env = std::getenv(name)) {
            try {
                configured = std::stoi(env);
            } catch (...) {
                configured = 0;
            }
            if (configured > 0) {
                break;
            }
        }
    }
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    const int detected = std::max(1, static_cast<int>(info.dwNumberOfProcessors));
#else
    const int detected = 1;
#endif
    const int requested = configured > 0 ? std::min(configured, detected) : detected;
#ifdef _WIN32
    const int default_cap = configured > 0 ? 64 : 16;
    return std::max(1, std::min({rows, requested, default_cap}));
#else
    return std::max(1, std::min(rows, requested));
#endif
}

Score evaluate_population_score_candidate(
    const OptimizationResult& result,
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const GlassSearchContext& glass_search,
    const MeritConfig& merit,
    int i) {
    const int dim = static_cast<int>(variables.size());
    std::vector<double> values;
    values.reserve(static_cast<std::size_t>(dim));
    for (int j = 0; j < dim; ++j) {
        const std::size_t offset = static_cast<std::size_t>(i) * dim + j;
        if (offset < result.final_population_values.size()) {
            values.push_back(result.final_population_values[offset]);
        }
    }
    Score score;
    if (static_cast<int>(values.size()) == dim) {
        LensSystem candidate = apply_mixed_variables(base, variables, values, glass_search);
        if (merit.dynamic_apertures) {
            update_clear_apertures_from_trace(
                candidate,
                clear_aperture_update_rings(merit),
                "gaussian_ring_arm",
                merit.sampling_seed,
                candidate.ray_aiming.mode,
                merit.effective_clear_aperture_margin(),
                true,
                merit.clear_aperture_arms,
                false,
                false,
                merit.vignetting_boundary_support_tolerance,
                merit.vignetting_boundary_support_selectivity_slope,
                merit.vignetting_boundary_support_contract ==
                    VignettingBoundarySupportContract::GlobalTrimRealizability,
                merit.vignetting_physical_cut_match_enabled,
            merit.vignetting_optimization_enabled());
        } else {
            sync_stop_aperture_to_system(candidate);
        }
        score = evaluate_merit(candidate, merit);
    } else if (i < static_cast<int>(result.final_population_merits.size())) {
        score.merit = result.final_population_merits[static_cast<std::size_t>(i)];
    }
    return score;
}

#ifdef _WIN32
struct PopulationScoreThreadContext {
    const OptimizationResult* result = nullptr;
    const LensSystem* base = nullptr;
    const std::vector<VariableSpec>* variables = nullptr;
    const GlassSearchContext* glass_search = nullptr;
    const MeritConfig* merit = nullptr;
    std::vector<Score>* scores = nullptr;
    int rows = 0;
    volatile LONG next_index = 0;
    CRITICAL_SECTION error_lock;
    std::exception_ptr first_error;
};

DWORD WINAPI run_population_score_thread(LPVOID parameter) {
    auto* context = static_cast<PopulationScoreThreadContext*>(parameter);
    while (true) {
        const int index = static_cast<int>(InterlockedIncrement(&context->next_index)) - 1;
        if (index >= context->rows) {
            break;
        }
        EnterCriticalSection(&context->error_lock);
        const bool has_error = static_cast<bool>(context->first_error);
        LeaveCriticalSection(&context->error_lock);
        if (has_error) {
            break;
        }
        try {
            (*context->scores)[static_cast<std::size_t>(index)] =
                evaluate_population_score_candidate(
                    *context->result,
                    *context->base,
                    *context->variables,
                    *context->glass_search,
                    *context->merit,
                    index);
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

std::vector<Score> evaluate_population_scores_cpu(
    const OptimizationResult& result,
    const LensSystem& base,
    const std::vector<VariableSpec>& variables,
    const GlassSearchContext& glass_search,
    const MeritConfig& merit) {
    const int rows = result.final_population_size;
    const int dim = static_cast<int>(variables.size());
    std::vector<Score> scores(static_cast<std::size_t>(std::max(0, rows)));
    if (rows <= 0) {
        return scores;
    }
    const int workers = population_score_worker_count(rows);
    if (workers <= 1) {
        for (int i = 0; i < rows; ++i) {
            scores[static_cast<std::size_t>(i)] =
                evaluate_population_score_candidate(
                    result,
                    base,
                    variables,
                    glass_search,
                    merit,
                    i);
        }
        return scores;
    }

#ifdef _WIN32
    PopulationScoreThreadContext context;
    context.result = &result;
    context.base = &base;
    context.variables = &variables;
    context.glass_search = &glass_search;
    context.merit = &merit;
    context.scores = &scores;
    context.rows = rows;
    InitializeCriticalSection(&context.error_lock);
    std::vector<HANDLE> handles;
    handles.reserve(static_cast<std::size_t>(workers));
    for (int worker = 0; worker < workers; ++worker) {
        HANDLE handle = CreateThread(
            nullptr,
            0,
            run_population_score_thread,
            &context,
            0,
            nullptr);
        if (handle) {
            handles.push_back(handle);
        }
    }
    if (!handles.empty()) {
        WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), TRUE, INFINITE);
        for (HANDLE handle : handles) {
            CloseHandle(handle);
        }
    }
    std::exception_ptr first_error = context.first_error;
    DeleteCriticalSection(&context.error_lock);
    if (first_error) {
        std::rethrow_exception(first_error);
    }
#else
    for (int i = 0; i < rows; ++i) {
        scores[static_cast<std::size_t>(i)] =
            evaluate_population_score_candidate(
                result,
                base,
                variables,
                glass_search,
                merit,
                i);
    }
#endif
    return scores;
}

} } }
