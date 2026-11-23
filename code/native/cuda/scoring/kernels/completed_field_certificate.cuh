#pragma once
#include "../prelude.cuh"
#include "../aiming/chromatic_anchor.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../geometry/asphere_slope.cuh"
#include "../host/certificate_context.cuh"
#include "../sampling/pupil_nodes.cuh"

namespace optiland {
namespace {
// Final-model failed-aim rays are neither sampling exclusions nor stop clipping.
// Their progress is zero; all other ray progress is <= 1. Each field enters once.
__global__ void evaluation_certify_completed_field_kernel(
    const DeviceField* fields, const DeviceWave* waves,
    const DeviceCalibratedModel* models, DeviceConfig cfg,
    int population_size, int field_index, double epsilon) {
    const int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= population_size || !g_evaluation_certificate) return;
    EvaluationCertificate& result = g_evaluation_certificate[row];
    if (!result.eligible || result.field >= 0) return;
    fields = d_candidate_row_fields(fields, cfg, row);
    const DeviceField field = fields[field_index];
    const int count = d_field_pupil_count(cfg, field);
    if (count <= 0 || count > 256) return;
    double pupil_weight = 0.0;
    for (int pi = 0; pi < count; ++pi) {
        double px = 0.0, py = 0.0, weight = 0.0;
        d_field_pupil_at(cfg, field, pi, &px, &py, &weight);
        if (!d_finite(weight) || weight < 0.0) return;
        pupil_weight += weight;
    }
    if (!d_finite(pupil_weight) || pupil_weight <= 0.0) return;
    double total = 0.0, failed = 0.0;
    const long long base = (static_cast<long long>(row) * cfg.field_count + field_index) * cfg.wavelength_count;
    for (int wi = 0; wi < cfg.wavelength_count; ++wi) {
        const double weight = waves[wi].weight;
        if (!d_finite(weight) || weight < 0.0) return;
        const double w = weight * pupil_weight;
        total += w;
        if (!models[base + wi].valid) failed += w;
    }
    if (!d_finite(total) || !d_finite(failed) || total <= 0.0 || failed <= 0.0) return;
    const double lower = failed * (1.0 - 1.0e-10);
    const double upper = total * (1.0 + 1.0e-10);
    result.coverage_upper = fmin(result.coverage_upper, 1.0 - lower / upper);
    result.failed_weight_lower = (result.failed_weight_lower + lower) * (1.0 - 1.0e-10);
    result.total_weight_upper = upper;
    if (result.coverage_upper + epsilon + 1.0e-10 >= cfg.min_field_coverage) return;
    if (result.parent_trace_feasible) {
        result.field = field_index;
        result.reason = 1;
        return;
    }
    // Only nonnegative terms from the exact host make_candidate_fitness formula.
    // Deliberately omit system coverage, chiefs, trace group error and other terms.
    if (!d_finite(cfg.unified_coverage_tol) || !d_finite(cfg.unified_progress_tol) ||
        !d_finite(cfg.unified_failed_weight_tol) || !d_finite(cfg.min_field_coverage)) return;
    const double c = fmax(0.0, (cfg.min_field_coverage - result.coverage_upper) /
        fmax(1.0e-12, cfg.unified_coverage_tol));
    const double p = fmax(0.0, (1.0 - result.coverage_upper) /
        fmax(1.0e-12, cfg.unified_progress_tol));
    const double f = result.failed_weight_lower / fmax(1.0e-12, cfg.unified_failed_weight_tol);
    if (!d_finite(c) || !d_finite(p) || !d_finite(f) ||
        c > 1.0e150 || p > 1.0e150 || f > 1.0e150) return;
    const double bound = (c*c + p*p + f*f) * (1.0 - 1.0e-10);
    if (!d_finite(bound)) return;
    result.trace_violation_lower = bound;
    const double margin = 1.0e-8 * fmax(1.0, fabs(result.parent_trace_violation));
    if (d_finite(result.parent_trace_violation) && bound > result.parent_trace_violation + margin) {
        result.field = field_index;
        result.reason = 2;
    }
}
} // namespace
} // namespace optiland
