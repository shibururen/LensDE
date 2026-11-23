#pragma once
#include "../prelude.cuh"

namespace optiland {
// Host request state is thread-local; the scorer entry serializes shared CUDA
// symbols across independent brokers and direct callers in this process.
static thread_local int evaluation_context_mode = 0;
} // namespace optiland

namespace optiland {
static thread_local int evaluation_device_mode = 0;
} // namespace optiland

namespace optiland {
static thread_local double evaluation_context_epsilon = 0.0;
} // namespace optiland

namespace optiland {
static thread_local std::vector<EvaluationCertificate> evaluation_context_records;
} // namespace optiland

namespace optiland {
void evaluation_set_certificate_context(int mode, const std::vector<EvaluationCertificate>& rows, double epsilon) {
    evaluation_context_records = rows; evaluation_context_epsilon = epsilon; evaluation_context_mode = mode;
}
} // namespace optiland

namespace optiland {
std::vector<EvaluationCertificate> evaluation_certificate_results() { return evaluation_context_records; }
} // namespace optiland

namespace optiland {
__device__ EvaluationCertificate* g_evaluation_certificate = nullptr;
} // namespace optiland

namespace optiland {
__device__ int g_evaluation_certificate_mode = 0;
} // namespace optiland

namespace optiland {
__device__ bool evaluation_skip_outer_row(int row) {
    return g_evaluation_certificate_mode==2 && g_evaluation_certificate &&
        g_evaluation_certificate[row].reason==3;
}
} // namespace optiland
