/*
 * Owns the single host EvaluationCallerContext storage object for each thread.
 * Evaluation-certificate accessors in optimizer and analysis code use this translation
 * unit so requests/results share one lifetime and identity across compilation units;
 * the context rows and pruning policy remain defined in evaluation_certificate.hpp.
 */

#include "optiland/evaluation_certificate.hpp"

namespace optiland {
namespace {
thread_local EvaluationCallerContext context;
}

EvaluationCallerContext& evaluation_caller_context_storage() {
    return context;
}
} // namespace optiland
