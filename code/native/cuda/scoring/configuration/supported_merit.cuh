#pragma once
#include "../prelude.cuh"
#include "runtime_options.cuh"
#include "../contracts/constants.cuh"

namespace optiland {
namespace {
bool de_cuda_merit_supported(const LensSystem& base, const MeritConfig& merit) {
    const bool debug_reject =
        std::getenv("OPTILAND_DE_CUDA_SUPPORT_DEBUG") != nullptr;
#ifdef OPTILAND_DE_CUDA_ONLY
    const bool emit_reject = true;
#else
    const bool emit_reject = debug_reject;
#endif
    auto reject = [&](const char* reason) {
        if (emit_reject) {
            std::fprintf(stderr, "DE-CUDA scorer unsupported: %s\n", reason);
        }
        return false;
    };
    bool aiming_supported = false;
    if (base.ray_aiming.mode == RayAimingMode::Paraxial) {
        aiming_supported = true;
    } else if (base.ray_aiming.mode == RayAimingMode::CalibratedParaxial) {
        const auto& cp = base.ray_aiming.calibrated_paraxial;
        const std::string sampling = normalized_token(cp.sampling_mode);
        const std::string fallback = normalized_token(cp.fallback);
        const std::string edge_solver = normalized_token(cp.edge_calibration_solver);
        const std::string budget_solver = normalized_token(cp.budget_chief_solver);
        const std::string chief_mode = normalized_token(cp.chief_anchor_mode);
        const bool fallback_disabled =
            fallback == "none" || fallback == "off" ||
            fallback == "disabled" || fallback == "fail" ||
            fallback == "failure";
        const bool edge_solver_supported =
            edge_solver == "secant" || edge_solver == "broyden" ||
            edge_solver == "secant_only" || edge_solver == "broyden_only";
        const bool budget_solver_supported =
            budget_solver.empty() || budget_solver == "auto" ||
            budget_solver == "hybrid" ||
            budget_solver == "iterative_secant" ||
            budget_solver == "iterative_secant_fallback" ||
            budget_solver == "secant" || budget_solver == "broyden" ||
            budget_solver == "secant_only" || budget_solver == "broyden_only";
        const bool chief_mode_supported =
            chief_mode == "abcd_budget_angle_bridge_fast" ||
            chief_mode == "budget_abcd_angle_bridge_fast" ||
            chief_mode == "abcd_budget" ||
            chief_mode == "budget_abcd" ||
            chief_mode == "budgeted_abcd";
        if (!cp.enabled) {
            return reject("calibrated-paraxial aiming is disabled");
        }
        if (sampling != "chief_edge_interpolation") {
            return reject(
                "calibrated-paraxial sampling mode must be chief_edge_interpolation");
        }
        if (!fallback_disabled) {
            return reject("calibrated-paraxial CPU/fallback aiming must be disabled");
        }
        if (cp.correction_iterations != 0) {
            return reject("calibrated-paraxial correction iterations must be zero");
        }
        if (!cp.edge_calibration_enabled) {
            return reject("calibrated-paraxial edge calibration must be enabled");
        }
        if (!edge_solver_supported) {
            return reject("calibrated-paraxial edge solver is unsupported");
        }
        if (!budget_solver_supported) {
            return reject("calibrated-paraxial budget-chief solver is unsupported");
        }
        if (!chief_mode_supported) {
            return reject("calibrated-paraxial chief anchor mode is unsupported");
        }
        // Field angle is intentionally not a support gate.  CUDA-only routing is
        // decided by the calibrated algorithm/configuration checks above; wide
        // fields are covered by the same scorer and CPU/CUDA parity contract.
        aiming_supported = true;
    }
    if (!unified_trace_gate_enabled(merit)) {
        return reject("merit model is not unified_trace_gate");
    }
    if (!aiming_supported) {
        return reject("ray aiming mode/config is outside the CUDA-DE subset");
    }
    if (!merit.bounds.empty()) {
        if (base.surfaces.size() > 64) {
            return reject("conic merit bounds need at most 64 surfaces");
        }
        bool have_range = false;
        double common_lower = 0.0;
        double common_upper = 0.0;
        unsigned long long mask = 0ULL;
        for (const BoundConstraint& bound : merit.bounds) {
            if (normalized_token(bound.kind) != "conic") {
                return reject("only conic merit bounds are supported");
            }
            if (bound.index >= base.surfaces.size() || bound.index >= 64 ||
                bound.term != 0) {
                return reject("conic merit bound index/term is invalid");
            }
            if (!std::isfinite(bound.lower) || !std::isfinite(bound.upper) ||
                !(bound.lower < bound.upper) || !std::isfinite(bound.weight) ||
                !(bound.weight > 0.0)) {
                return reject("conic merit bound range/weight is invalid");
            }
            if (!have_range) {
                common_lower = bound.lower;
                common_upper = bound.upper;
                have_range = true;
            } else if (std::abs(bound.lower - common_lower) > 1.0e-12 ||
                       std::abs(bound.upper - common_upper) > 1.0e-12) {
                return reject("conic merit bounds must share one range");
            }
            const unsigned long long bit = 1ULL << bound.index;
            if ((mask & bit) != 0ULL) {
                return reject("duplicate conic merit bound");
            }
            mask |= bit;
        }
    }
    const std::string distribution = normalized_token(merit.pupil_distribution);
    if (distribution != "gaussian_ring_arm" &&
        distribution != "gaussian_ring_arm_fixed_budget" &&
        !is_codev_rectangular_distribution(distribution)) {
        return reject("pupil distribution is neither gaussian_ring_arm nor codev_rectangular");
    }
    if (is_codev_rectangular_distribution(distribution)) {
        const std::size_t pupil_count = make_codev_rectangular_distribution(
            effective_codev_ray_interval(base, merit),
            merit.codev_obscuration,
            merit.codev_spot_aperture_weight).size();
        if (pupil_count == 0 || pupil_count > 512) {
            return reject("codev_rectangular pupil count is outside the CUDA-DE range 1..512");
        }
    }
    bool needs_wavefront = false;
    bool has_stop_air_edge = false;
    for (const MeritOperand& operand : merit.operands) {
        const std::string name = normalized_token(operand.name);
        const std::string mode = normalized_token(operand.mode);
        if (name == "rms_spot" || name == "spot" || name == "spot_rms") {
            if (mode != "minimize" && mode != "minimum") {
                return reject("rms_spot operand mode is unsupported");
            }
        } else if (name == "max_spot" || name == "max_rms_spot") {
            if (mode != "minimize" && mode != "minimum") {
                return reject("max_spot operand mode is unsupported");
            }
        } else if (name == "worst_wavelength_spot" ||
                   name == "worst_wavelength_rms_spot" ||
                   name == "max_monochromatic_spot" ||
                   name == "max_monochromatic_rms_spot" ||
                   name == "spectral_spot_spread" ||
                   name == "spectral_rms_spread" ||
                   name == "wavelength_spot_spread" ||
                   name == "chromatic_spot_spread") {
            if (mode != "less_than" && mode != "less" &&
                mode != "upper" && mode != "max" &&
                mode != "le" && mode != "lte") {
                return reject("spectral spot operand mode is unsupported");
            }
        } else if (name == "single_ray_spot_tail" ||
                   name == "absolute_single_ray_spot_tail" ||
                   name == "max_single_ray_spot_absolute") {
            if (mode != "minimize" && mode != "minimum") {
                return reject("single-ray spot tail operand mode is unsupported");
            }
        } else if (name == "single_ray_spot_guard" ||
                   name == "single_ray_spot" ||
                   name == "max_single_ray_spot" ||
                   name == "max_ray_spot" ||
                   name == "ray_spot_guard" ||
                   name == "spot_ray_guard") {
            if (mode != "less_than" && mode != "less" &&
                mode != "upper" && mode != "max" &&
                mode != "le" && mode != "lte") {
                return reject("single-ray spot guard mode is unsupported");
            }
        } else if (name == "rms_wavefront" || name == "wavefront" ||
                   name == "wavefront_rms" || name == "mean_wavefront" ||
                   name == "mean_rms_wavefront" || name == "opd" ||
                   name == "rms_opd" || name == "opd_rms" ||
                   name == "mean_opd_rms") {
            needs_wavefront = true;
            if (mode != "minimize" && mode != "minimum") {
                return reject("rms_wavefront operand mode is unsupported");
            }
        } else if (name == "single_ray_wavefront_guard" ||
                   name == "single_ray_wavefront" ||
                   name == "max_single_ray_wavefront" ||
                   name == "max_ray_wavefront" ||
                   name == "single_ray_opd_guard" ||
                   name == "max_single_ray_opd" ||
                   name == "max_ray_opd" ||
                   name == "ray_wavefront_guard" ||
                   name == "wavefront_ray_guard") {
            needs_wavefront = true;
            if (mode != "less_than" && mode != "less" &&
                mode != "upper" && mode != "max" &&
                mode != "le" && mode != "lte") {
                return reject("single-ray wavefront guard mode is unsupported");
            }
        } else if (name == "efl" || name == "effective_focal_length") {
            if (mode != "target") {
                return reject("efl operand mode is unsupported");
            }
        } else if (name == "working_f_number" || name == "working_fno" ||
                   name == "working_f_num" || name == "wfno" ||
                   name == "image_space_working_f_number") {
            if (mode != "target" && mode != "less_than" && mode != "less" &&
                mode != "upper" && mode != "max" &&
                mode != "le" && mode != "lte") {
                return reject("working-f-number operand mode is unsupported");
            }
        } else if (name == "ttl" || name == "total_track" || name == "track" ||
                   name == "max_distortion" || name == "distortion" ||
                   name == "max_chief_ray_angle" ||
                   name == "chief_ray_angle" ||
                   name == "max_cra" || name == "cra" ||
                   name == "lateral_color" || name == "tca" || name == "lca" ||
                   name == "axial_color" ||
                   name == "paraxial_axial_color" ||
                   name == "axial_color_focus_shift" ||
                   name == "paraxial_axial_color_focus_shift") {
            if (mode != "less_than" && mode != "less" &&
                mode != "upper" && mode != "max" &&
                mode != "le" && mode != "lte") {
                return reject("upper-bound operand mode is unsupported");
            }
        } else if (name == "bfl" || name == "back_focal_length") {
            if (mode != "greater_than" && mode != "greater" &&
                mode != "lower" && mode != "min" &&
                mode != "ge" && mode != "gte") {
                return reject("bfl operand mode is unsupported");
            }
        } else if (name == "stop_air_edge_thickness") {
            if (has_stop_air_edge || mode != "greater_than" ||
                operand.index == 0 || operand.index + 1 >= base.surfaces.size() ||
                !base.surfaces[operand.index].is_stop ||
                !base.surfaces[operand.index].is_plane() ||
                !std::isfinite(operand.lower) ||
                !std::isfinite(operand.weight) || operand.weight < 0.0) {
                return reject("stop-air edge operand requires one valid independent stop and lower bound");
            }
            has_stop_air_edge = true;
        } else if (name == "relative_illumination_proxy" ||
                   name == "relative_illumination" ||
                   name == "ri_proxy") {
            if (mode != "greater_than" && mode != "greater" &&
                mode != "lower" && mode != "min" &&
                mode != "ge" && mode != "gte") {
                return reject("relative-illumination operand mode is unsupported");
            }
            const double lower = std::isfinite(operand.lower)
                                     ? operand.lower
                                     : operand.target;
            if (operand.index >= base.fields.size() ||
                operand.index >= kMaxDeviceFields ||
                !std::isfinite(lower) || lower < 0.0 || lower > 1.0 ||
                !std::isfinite(operand.weight) || operand.weight < 0.0) {
                return reject("relative-illumination operand is invalid");
            }
        } else {
            return reject("operand name is unsupported");
        }
    }
    if (needs_wavefront) {
        const std::string reference = normalized_token(merit.wavefront_reference);
        const bool centroid_reference =
            reference == "centroid" || reference == "centroid_sphere" ||
            reference == "centroid_reference" ||
            reference == "centroid_reference_sphere";
        const bool chief_reference =
            reference.empty() || reference == "chief" ||
            reference == "chief_ray" || reference == "chief_reference" ||
            reference == "chief_ray_reference" ||
            reference == "chief_reference_sphere";
        if (!centroid_reference && !chief_reference) {
            return reject("wavefront reference is outside the CUDA-DE subset");
        }
    }
    return true;
}
} // namespace
} // namespace optiland
