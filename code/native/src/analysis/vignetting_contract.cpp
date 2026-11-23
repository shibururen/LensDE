/*
 * Reconciles axis intervals into a shared pupil center and validates serialized factors.
 * Automatic-vignetting orchestration calls these contraction and field-batch helpers.
 * The four FVC/FVD edges share one center; unit-pupil containment and final aiming
 * checks must hold under the caller's aperture model before solved fields are committed.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
AutomaticVignettingCenteredSections
solve_automatic_vignetting_centered_sections(
    AutomaticVignettingFieldSolver& solver,
    double initial_center_x,
    double initial_center_y,
    double pupil_tolerance) {
    AutomaticVignettingCenteredSections result;
    if (!std::isfinite(initial_center_x) ||
        !std::isfinite(initial_center_y) ||
        std::hypot(initial_center_x, initial_center_y) > 1.0) {
        result.message = "initial pupil center is outside the unit stop";
        return result;
    }

    // The four serialized FVC/FVD edge pupils share one center.  Solve that
    // center as the fixed point of the horizontal and vertical hard-aperture
    // chord midpoints; otherwise endpoints measured through the grid centroid
    // are silently moved when the two independent intervals are serialized.
    constexpr int kMaximumIterations = 48;
    constexpr double kCenterDamping = 0.5;
    const double convergence_tolerance =
        std::max(1.0e-10, 8.0 * std::max(0.0, pupil_tolerance));
    double center_x = initial_center_x;
    double center_y = initial_center_y;
    for (int iteration = 0; iteration < kMaximumIterations; ++iteration) {
        const AutomaticVignettingFieldSolver::Interval x_interval =
            solver.solve_axis(true, center_y, center_x);
        if (!x_interval.valid) {
            result.message = "X boundary: " + x_interval.message;
            result.iterations = iteration + 1;
            return result;
        }
        const AutomaticVignettingFieldSolver::Interval y_interval =
            solver.solve_axis(false, center_x, center_y);
        if (!y_interval.valid) {
            result.message = "Y boundary: " + y_interval.message;
            result.iterations = iteration + 1;
            return result;
        }

        const double midpoint_x =
            0.5 * (x_interval.lower + x_interval.upper);
        const double midpoint_y =
            0.5 * (y_interval.lower + y_interval.upper);
        const double residual = std::max(
            std::abs(midpoint_x - center_x),
            std::abs(midpoint_y - center_y));
        result.x_interval = x_interval;
        result.y_interval = y_interval;
        result.center_x = midpoint_x;
        result.center_y = midpoint_y;
        result.center_residual = residual;
        result.iterations = iteration + 1;
        if (residual <= convergence_tolerance) {
            result.valid = true;
            return result;
        }

        center_x += kCenterDamping * (midpoint_x - center_x);
        center_y += kCenterDamping * (midpoint_y - center_y);
        if (!std::isfinite(center_x) || !std::isfinite(center_y) ||
            std::hypot(center_x, center_y) > 1.0) {
            result.message = "self-consistent pupil center left the unit stop";
            return result;
        }
    }

    result.message = "self-consistent pupil center did not converge";
    return result;
}
}  // namespace analysis_detail

namespace analysis_detail {
Field contract_automatic_vignetting_field(
    const Field& field,
    double contraction) {
    Field contracted = field;
    const double factor = std::clamp(contraction, 0.0, 1.0);
    contracted.fvdx = std::clamp(
        1.0 - (1.0 - field.fvdx) * factor,
        0.0,
        1.0);
    contracted.fvdy = std::clamp(
        1.0 - (1.0 - field.fvdy) * factor,
        0.0,
        1.0);
    contracted.vignette_x = contracted.fvdx;
    contracted.vignette_y = contracted.fvdy;
    return contracted;
}
}  // namespace analysis_detail

namespace analysis_detail {
double automatic_vignetting_max_affine_pupil_radius_squared(
    const Field& field) {
    constexpr int kAngularSamples = 8192;
    double maximum = 0.0;
    for (int index = 0; index < kAngularSamples; ++index) {
        const double angle =
            2.0 * kPi * static_cast<double>(index) /
            static_cast<double>(kAngularSamples);
        const double pupil_x =
            field.fvcx + (1.0 - field.fvdx) * std::cos(angle);
        const double pupil_y =
            field.fvcy + (1.0 - field.fvdy) * std::sin(angle);
        maximum = std::max(
            maximum,
            pupil_x * pupil_x + pupil_y * pupil_y);
    }
    return maximum;
}
}  // namespace analysis_detail

namespace analysis_detail {
bool project_automatic_vignetting_x_inside_unit_pupil(
    Field* field,
    double* accepted_x_contraction) {
    if (!field || !accepted_x_contraction) {
        return false;
    }
    const Field original = *field;
    auto candidate_at = [&](double contraction) {
        Field candidate = original;
        candidate.fvdx = std::clamp(
            1.0 - (1.0 - original.fvdx) * contraction,
            0.0,
            1.0);
        candidate.vignette_x = candidate.fvdx;
        return candidate;
    };
    auto inside = [&](double contraction) {
        return automatic_vignetting_max_affine_pupil_radius_squared(
                   candidate_at(contraction)) <= 1.0 + 1.0e-12;
    };
    if (inside(1.0)) {
        *accepted_x_contraction = 1.0;
        return true;
    }
    if (!inside(0.0)) {
        return false;
    }
    double passing = 0.0;
    double failing = 1.0;
    for (int iteration = 0; iteration < 48; ++iteration) {
        const double middle = 0.5 * (passing + failing);
        if (inside(middle)) {
            passing = middle;
        } else {
            failing = middle;
        }
    }
    const double final_contraction = passing * 0.9999;
    *field = candidate_at(final_contraction);
    *accepted_x_contraction = final_contraction;
    return automatic_vignetting_max_affine_pupil_radius_squared(*field) <= 1.0;
}
}  // namespace analysis_detail

namespace analysis_detail {
bool automatic_vignetting_final_pupil_valid(
    const LensSystem& system,
    std::size_t field_index,
    const std::vector<std::size_t>& wave_indices) {
    if (field_index >= system.fields.size()) {
        return false;
    }
    std::vector<PupilPoint> validation_pupils = make_pupil_distribution(
        6, "gaussian_ring_arm", 1, 8);
    const PupilPoint edge_pupils[] = {
        {1.0, 0.0, 1.0},
        {-1.0, 0.0, 1.0},
        {0.0, 1.0, 1.0},
        {0.0, -1.0, 1.0},
    };
    validation_pupils.insert(
        validation_pupils.end(),
        std::begin(edge_pupils),
        std::end(edge_pupils));
    AimingSession aiming(system);
    SequentialRayTracer tracer(system);
    // Reproduce the production field-major session history.  Calibrated
    // chief/branch continuation may use already-seen inner fields, so an
    // isolated outer-field probe can otherwise certify a branch that is not
    // selected during an actual merit evaluation.
    for (std::size_t current_field_index = 0;
         current_field_index <= field_index;
         ++current_field_index) {
        const Field& field = system.fields[current_field_index];
        for (const std::size_t wave_index : wave_indices) {
            if (wave_index >= system.wavelengths.size()) {
                return false;
            }
            const Wavelength& wave = system.wavelengths[wave_index];
            for (std::size_t pupil_index = 0;
                 pupil_index < validation_pupils.size();
                 ++pupil_index) {
                const PupilPoint& pupil = validation_pupils[pupil_index];
                const double mapped_x =
                    field.fvcx + (1.0 - field.fvdx) * pupil.px;
                const double mapped_y =
                    field.fvcy + (1.0 - field.fvdy) * pupil.py;
                if (mapped_x * mapped_x + mapped_y * mapped_y >
                    1.0 + 1.0e-12) {
                    return false;
                }
                Ray ray = aiming.generate(
                    current_field_index,
                    wave_index,
                    pupil_index,
                    field,
                    wave,
                    pupil);
                if (!ray.valid || !ray.aim_converged || ray.intensity <= 0.0 ||
                    !tracer.trace_ray(ray)) {
                    return false;
                }
            }
        }
    }
    return true;
}
}  // namespace analysis_detail

namespace analysis_detail {
bool contract_to_valid_automatic_vignetting_field(
    LensSystem& system,
    std::size_t field_index,
    const std::vector<std::size_t>& wave_indices,
    Field* solved,
    double* accepted_contraction) {
    if (!solved || !accepted_contraction || field_index >= system.fields.size()) {
        return false;
    }
    const Field unconstrained = *solved;
    auto valid_at = [&](double contraction) {
        system.fields[field_index] =
            contract_automatic_vignetting_field(unconstrained, contraction);
        return automatic_vignetting_final_pupil_valid(
            system, field_index, wave_indices);
    };

    if (valid_at(1.0)) {
        *solved = system.fields[field_index];
        *accepted_contraction = 1.0;
        return true;
    }

    // Find a valid interior bracket first.  These values are deterministic
    // and intentionally dense near one because most failures are boundary
    // tolerance effects rather than a materially smaller transmitted pupil.
    const double probes[] = {
        0.999, 0.997, 0.995, 0.99, 0.98, 0.96,
        0.92, 0.85, 0.75, 0.60, 0.40, 0.20,
    };
    double passing = -1.0;
    double failing = 1.0;
    for (const double probe : probes) {
        if (valid_at(probe)) {
            passing = probe;
            break;
        }
        failing = probe;
    }
    if (!(passing > 0.0)) {
        system.fields[field_index] = unconstrained;
        return false;
    }

    // Recover the widest strict pupil inside the passing/failing bracket.
    for (int iteration = 0; iteration < 18; ++iteration) {
        const double middle = 0.5 * (passing + failing);
        if (valid_at(middle)) {
            passing = middle;
        } else {
            failing = middle;
        }
    }
    // Leave a small deterministic interior margin instead of serializing a
    // coefficient exactly on the calibrated model's validity transition.
    const double final_contraction = std::max(0.0, passing * 0.9999);
    if (!valid_at(final_contraction)) {
        system.fields[field_index] = unconstrained;
        return false;
    }
    *solved = system.fields[field_index];
    *accepted_contraction = final_contraction;
    return true;
}
}  // namespace analysis_detail

namespace analysis_detail {
void run_automatic_vignetting_field_batch(
    AutomaticVignettingFieldBatchContext* context) {
    if (!context || !context->system || !context->config ||
        !context->field_runs || !context->solved_fields ||
        !context->next_field) {
        return;
    }
    while (true) {
        const std::size_t field_index = context->next_field->fetch_add(
            1, std::memory_order_relaxed);
        if (field_index >= context->system->fields.size()) {
            return;
        }

        // Exact APG Set Vignetting is a current-field chief/pilot problem.
        // Give each scheduled field an isolated system/session and retain the
        // single-worker independent-target path within that field. This makes
        // scheduling order invisible to the numerical problem while sharing
        // one bounded CPU worker budget across the complete operation.
        LensSystem isolated = automatic_vignetting_local_apg_worker_system(
            *context->system);
        isolated.fields.assign(
            1, context->system->fields[field_index]);
        AutomaticVignettingConfig field_config = *context->config;
        field_config.use_batch_apg = true;
        field_config.batch_apg_workers = 1;
        (*context->field_runs)[field_index] = set_automatic_vignetting(
            isolated, field_config);
        if ((*context->field_runs)[field_index].success &&
            !isolated.fields.empty()) {
            (*context->solved_fields)[field_index] = isolated.fields.front();
        }
    }
}
}  // namespace analysis_detail

namespace analysis_detail {
DWORD WINAPI automatic_vignetting_field_batch_thread_entry(
    void* raw_context) {
    run_automatic_vignetting_field_batch(
        static_cast<AutomaticVignettingFieldBatchContext*>(raw_context));
    return 0;
}
}  // namespace analysis_detail

}  // namespace optiland
