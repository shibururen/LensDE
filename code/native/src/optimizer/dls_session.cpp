/*
 * Run a complete DlsSession and return the physical incumbent, score and completed-iteration statistics.
 * The public polish entry delegates initialization and iteration while this unit assembles the result.
 * Preserve the distinction between the residual-search iterate and the best-merit incumbent returned.
 */
#include "dls_session.hpp"

namespace optiland { namespace optimizer_detail {

OptimizationResult DlsSession::run() {
    initialize();
    iterate();
OptimizationResult result;
    result.best_values = scaled_to_physical(variables_, best_merit_x, internal_mode);
    result.best_score = best_merit_score;
    result.generations = completed_polish_iterations;
    result.used_cuda = polish_used_cuda;
    result.polish_seconds = seconds_since(polish_start);
    result.polish_runs = 1;
    result.timing = polish_timing;
    return result;
}

} }  // namespace optiland::optimizer_detail
