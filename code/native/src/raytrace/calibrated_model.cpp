// Prepare the center footprint and per-field calibrated-paraxial models for an aiming session.
// Named calibration phases consume references to the original session state and model records.
// Cache readiness, chief acceptance, edge construction, and interpolation validation are ordered.

#include "aiming_geometry_inline.hpp"
#include "diagnostics_inline.hpp"
#include "prepared_geometry_inline.hpp"
#include "detail.hpp"
#include "calibration/calibration_fallback_edge_seed.hpp"
#include "calibration/calibration_accept_chief_ray.hpp"
#include "calibration/calibration_probe_scale.hpp"
#include "calibration/calibration_try_grid_fit.hpp"
#include "calibration/calibration_solve_secant_edge.hpp"
#include "calibration/calibration_accept_edge_stop_sample.hpp"
#include "calibration/calibration_cached_same_field_edge_seed.hpp"
#include "calibration/calibration_try_scaled_edge_anchor.hpp"
#include "calibration/calibration_build_edge_calibration.hpp"
#include "calibration/calibration_solve_seed_space_edge.hpp"
#include "calibration/calibration_sweep_interpolation_edges.hpp"
#include "calibration/build_calibrated_interpolation.hpp"

namespace optiland {
using namespace raytrace_detail;

AimingSession::CalibratedCenterBeamFootprint&
AimingSession::ensure_calibrated_center_beam_footprint(
    std::size_t wave_index,
    const Wavelength& wave) {
    auto& footprint = calibrated_center_beam_footprints_[wave_index];
    if (footprint.built &&
        std::abs(footprint.wavelength_um - wave.value_um) < 1.0e-12) {
        return footprint;
    }

    footprint = {};
    footprint.built = true;
    footprint.wavelength_um = wave.value_um;
    if (!system_.fields.empty()) {
        std::size_t center_field_index = 0;
        auto field_center_score = [](const Field& candidate) {
            double score = std::hypot(candidate.hx, candidate.hy);
            if (!std::isfinite(score)) {
                score = std::numeric_limits<double>::infinity();
            }
            return score;
        };
        auto best_score = field_center_score(system_.fields.front());
        for (std::size_t i = 1; i < system_.fields.size(); ++i) {
            const auto score = field_center_score(system_.fields[i]);
            if (score < best_score) {
                best_score = score;
                center_field_index = i;
            }
        }
        (void)ensure_calibrated_paraxial_model(
            center_field_index,
            wave_index,
            system_.fields[center_field_index],
            wave);
    }
    return footprint;
}

AimingSession::CalibratedParaxialModel& AimingSession::ensure_calibrated_paraxial_model(
    std::size_t field_index,
    std::size_t wave_index,
    const Field& sampled_field,
    const Wavelength& wave) {
    const FieldWaveKey key{field_index, wave_index};
    auto& diag = diagnostics_state();
    add_diag(diag.calibrated_model_requests);
    auto existing = calibrated_models_.find(key);
    if (existing != calibrated_models_.end() && existing->second.built) {
        add_diag(diag.calibrated_model_cache_hits);
        return existing->second;
    }
    // Completed cached models need no continuation environment or repair setup.
    const auto& runtime_options = calibrated_runtime_options();
    const int continuation_mode = runtime_options.continuation_mode;
    using RepairKey=std::pair<const AimingSession*,FieldWaveKey>;
    static thread_local std::vector<RepairKey> ongoing_continuation_repairs;
    bool continuation_repair_build=false;
    for (const auto& active:ongoing_continuation_repairs)
        if (active.first==this && active.second==key) continuation_repair_build=true;
    // Prime primary before marking a secondary model built. A primary repair
    // may build this secondary recursively, so recheck its completed cache entry.
    if ((continuation_mode&512) && system_.primary_wavelength_apertures && !wave.primary) {
        for (std::size_t wi=0;wi<system_.wavelengths.size();++wi) {
            if (system_.wavelengths[wi].primary) {
                (void)ensure_calibrated_paraxial_model(field_index,wi,sampled_field,system_.wavelengths[wi]);
                break;
            }
        }
        auto primed=calibrated_models_.find(key);
        if (primed!=calibrated_models_.end() && primed->second.built) return primed->second;
    }
    CalibratedParaxialModel& model = calibrated_models_[key];
    model.built = true;
    add_diag(diag.calibrated_model_builds);
    ScopedDiagTimer model_timer(diag.calibrated_model_build_ns);
    auto finish_model = [&]() -> CalibratedParaxialModel& {
        if (runtime_options.model_dump) {
            // This optional diagnostic must also build with MinGW's Win32
            // thread model, where std::mutex is unavailable. No lock is touched
            // unless the model-dump environment switch is explicitly enabled.
            static std::atomic_flag dump_mutex = ATOMIC_FLAG_INIT;
            while (dump_mutex.test_and_set(std::memory_order_acquire)) {}
            struct DumpLock {
                std::atomic_flag& mutex;
                ~DumpLock() { mutex.clear(std::memory_order_release); }
            } lock{dump_mutex};
            std::fprintf(stderr,"CPUMODEL,%p,%zu,%zu,%d,%.17g,%.17g,%.17g,%.17g,%.17g",
                static_cast<const void*>(this),field_index,wave_index,model.valid,
                model.chief_seed.x,model.chief_seed.y,model.chief_stop_x,model.chief_stop_y,model.fit_rms);
            for (const auto& a:model.edge_anchors) {
                std::fprintf(stderr,",%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g",
                    a.valid,a.px,a.py,a.seed.x,a.seed.y,a.stop_x,a.stop_y);
            }
            std::fprintf(stderr,"\n");
        }
        if ((continuation_mode&512) && !continuation_repair_build &&
            field_index>0 && wave.primary && system_.primary_wavelength_apertures &&
            model.chief_seed.valid && !model.valid) {
            // Exactly one repair, after positive-weight secondaries have had
            // access to the original partial primary model (as in CUDA).
            ongoing_continuation_repairs.push_back({this,key});
            struct PopRepair {
                std::vector<RepairKey>& stack;
                ~PopRepair() { stack.pop_back(); }
            } pop{ongoing_continuation_repairs};
            for (std::size_t wi=0;wi<system_.wavelengths.size();++wi) {
                const auto& secondary=system_.wavelengths[wi];
                if (!secondary.primary && secondary.weight>0.0)
                    (void)ensure_calibrated_paraxial_model(field_index,wi,sampled_field,secondary);
            }
            model=CalibratedParaxialModel{};
            // The original candidate search, traced STOP checks, E3, and
            // geometric-completion prohibition for primary are all retained.
            return ensure_calibrated_paraxial_model(field_index,wave_index,sampled_field,wave);
        }
        add_diag(model.valid ? diag.calibrated_model_valid : diag.calibrated_model_invalid);
        return model;
    };
    const auto& cfg = system_.ray_aiming.calibrated_paraxial;
    if (!cfg.enabled) {
        return finish_model();
    }

    // Make the cross-wavelength calibrated seed contract independent of the
    // order in which merit operands happen to request rays.  The primary
    // wavelength is the physical anchor; build it before any secondary wave
    // for this field so blue/red models can reuse the same chief/edge seed
    // family on CPU and CUDA.
    if (!system_.wavelengths.empty()) {
        std::size_t primary_wave_index = 0;
        for (std::size_t i = 0; i < system_.wavelengths.size(); ++i) {
            if (system_.wavelengths[i].primary) {
                primary_wave_index = i;
                break;
            }
        }
        if (primary_wave_index != wave_index) {
            const FieldWaveKey primary_key{field_index, primary_wave_index};
            const auto primary_model = calibrated_models_.find(primary_key);
            if (primary_model == calibrated_models_.end() ||
                !primary_model->second.built) {
                (void)ensure_calibrated_paraxial_model(
                    field_index,
                    primary_wave_index,
                    sampled_field,
                    system_.wavelengths[primary_wave_index]);
            }
        }
    }

    const Field field = sampled_field;

    const double rs =
        tracer_.stop_target_radius(wave.value_um, RayAimingMode::CalibratedParaxial);
    const double stop_scale = std::max(1.0e-9, std::abs(rs));
    const double chief_target_x = field.fvcx * rs;
    const double chief_target_y = field.fvcy * rs;
    const PupilPoint chief{0.0, 0.0, 1.0};
    const bool chief_debug = runtime_options.chief_debug_enabled &&
        (runtime_options.chief_debug_all ||
         runtime_options.chief_debug_field == static_cast<int>(field_index));

    auto accept_chief_ray = [&](const Ray& ray,
                                bool require_small_residual,
                                const ChiefAnchor* cached_anchor = nullptr) {
        return ::optiland::raytrace_detail::CalibrationAcceptChiefRay::run(*this, 
            field_index,
            wave_index,
            diag,
            model,
            cfg,
            stop_scale,
            chief_target_x,
            chief_target_y,
            chief_debug,
            ray,
            require_small_residual,
            cached_anchor
        );
    };

    add_diag(diag.calibrated_paraxial_chief_attempts);
    Ray paraxial_chief =
        tracer_.generate_ray_with_mode(field, chief, wave.value_um, RayAimingMode::Paraxial);
    if (accept_chief_ray(paraxial_chief, true)) {
        add_diag(diag.calibrated_paraxial_chief_accepted);
        ChiefAnchor cached;
        cached.seed = model.chief_seed;
        cached.ray = paraxial_chief;
        cached.valid = cached.seed.valid;
        if (cached.valid) {
            chief_anchors_[key] = cached;
        }
    } else {
        ChiefAnchor anchor;
        const std::string& chief_mode = cfg.chief_anchor_mode;
        const bool try_abcd_budget =
            chief_mode == "abcd_budget" ||
            chief_mode == "budget_abcd" ||
            chief_mode == "budgeted_abcd" ||
            chief_mode == "abcd_budget_inward" ||
            chief_mode == "budget_abcd_inward" ||
            chief_mode == "abcd_budget_center_inward" ||
            chief_mode == "budget_abcd_center_inward" ||
            chief_mode == "abcd_budget_inward_wide" ||
            chief_mode == "budget_abcd_inward_wide" ||
            chief_mode == "abcd_budget_center_inward_wide" ||
            chief_mode == "budget_abcd_center_inward_wide" ||
            chief_mode == "abcd_budget_field_continuation" ||
            chief_mode == "budget_abcd_field_continuation" ||
            chief_mode == "abcd_budget_field_extrapolate" ||
            chief_mode == "budget_abcd_field_extrapolate" ||
            chief_mode == "abcd_budget_angle_bridge" ||
            chief_mode == "budget_abcd_angle_bridge" ||
            chief_mode == "abcd_budget_angle_bridge_fast" ||
            chief_mode == "budget_abcd_angle_bridge_fast" ||
            chief_mode == "abcd_budget_field_continuation_then_full" ||
            chief_mode == "budget_abcd_field_continuation_then_full" ||
            chief_mode == "abcd_budget_angle_bridge_then_full" ||
            chief_mode == "budget_abcd_angle_bridge_then_full" ||
            chief_mode == "abcd_budget_angle_bridge_fast_then_full" ||
            chief_mode == "budget_abcd_angle_bridge_fast_then_full" ||
            chief_mode == "abcd_budget_then_full" ||
            chief_mode == "budget_abcd_then_full";
        const bool budget_then_full =
            chief_mode == "abcd_budget_then_full" ||
            chief_mode == "budget_abcd_then_full" ||
            chief_mode == "abcd_budget_field_continuation_then_full" ||
            chief_mode == "budget_abcd_field_continuation_then_full" ||
            chief_mode == "abcd_budget_angle_bridge_then_full" ||
            chief_mode == "budget_abcd_angle_bridge_then_full" ||
            chief_mode == "abcd_budget_angle_bridge_fast_then_full" ||
            chief_mode == "budget_abcd_angle_bridge_fast_then_full";
        const bool pure_abcd_budget =
            chief_mode == "abcd_budget" ||
            chief_mode == "budget_abcd" ||
            chief_mode == "budgeted_abcd" ||
            chief_mode == "abcd_budget_inward" ||
            chief_mode == "budget_abcd_inward" ||
            chief_mode == "abcd_budget_center_inward" ||
            chief_mode == "budget_abcd_center_inward" ||
            chief_mode == "abcd_budget_inward_wide" ||
            chief_mode == "budget_abcd_inward_wide" ||
            chief_mode == "abcd_budget_center_inward_wide" ||
            chief_mode == "budget_abcd_center_inward_wide" ||
            chief_mode == "abcd_budget_field_continuation" ||
            chief_mode == "budget_abcd_field_continuation" ||
            chief_mode == "abcd_budget_field_extrapolate" ||
            chief_mode == "budget_abcd_field_extrapolate" ||
            chief_mode == "abcd_budget_angle_bridge" ||
            chief_mode == "budget_abcd_angle_bridge" ||
            chief_mode == "abcd_budget_angle_bridge_fast" ||
            chief_mode == "budget_abcd_angle_bridge_fast";
        const bool allow_full_search =
            (budget_then_full || !try_abcd_budget) &&
            !pure_abcd_budget &&
            chief_mode != "none" &&
            chief_mode != "off" &&
            chief_mode != "disabled";
        if (try_abcd_budget) {
            anchor = try_abcd_budget_chief_anchor(field_index, wave_index, field, wave);
        }
        if (chief_debug) {
            std::fprintf(
                stderr,
                "CPU calibrated chief field=%zu wave=%zu budget_anchor_valid=%d "
                "stop_valid=%d residual=%.17g seed=(%.17g,%.17g)\n",
                field_index,
                wave_index,
                anchor.valid ? 1 : 0,
                anchor.stop_valid ? 1 : 0,
                anchor.stop_residual,
                anchor.seed.x,
                anchor.seed.y);
        }
        if (!anchor.valid && allow_full_search) {
            add_diag(diag.calibrated_chief_anchor_requests);
            const bool chief_anchor_cached = chief_anchors_.find(key) != chief_anchors_.end();
            add_diag(chief_anchor_cached ? diag.calibrated_chief_anchor_cache_hits
                                         : diag.calibrated_chief_anchor_builds);
            {
                ScopedDiagTimer chief_timer(diag.calibrated_chief_anchor_ns);
                anchor = ensure_chief_anchor(field_index, wave_index, field, wave);
            }
            add_diag(anchor.valid ? diag.calibrated_chief_anchor_valid
                                  : diag.calibrated_chief_anchor_invalid);
        }
        if (!anchor.valid || !accept_chief_ray(anchor.ray, false, &anchor)) {
            return finish_model();
        }
    }

    const double radius = first_surface_search_radius(wave.value_um);
    const std::string mapping_mode = normalized_mapping_mode(cfg.sampling_mode, "central_fit");
    auto seed_in_first_surface_bounds = [&](const FirstSurfaceSeed& seed) {
        if (!seed.valid || !std::isfinite(seed.x) || !std::isfinite(seed.y)) {
            return false;
        }
        const double hard_radius = first_surface_hard_radius(wave.value_um);
        return hard_radius <= 0.0 ||
               !aperture_clip_exceeded(std::hypot(seed.x, seed.y), hard_radius);
    };

    if (mapping_mode == "chief_edge_interpolation") {
        return ::optiland::raytrace_detail::BuildCalibratedInterpolation::run(*this, 
            field_index,
            wave_index,
            wave,
            diag,
            existing,
            continuation_mode,
            continuation_repair_build,
            model,
            finish_model,
            cfg,
            field,
            rs,
            stop_scale,
            chief_target_x,
            chief_target_y,
            radius,
            seed_in_first_surface_bounds
        );
    }

    const std::vector<double> probe_steps = aperture_aware_probe_steps(
        radius,
        stop_scale,
        cfg.jacobian_probe_fraction,
        cfg.min_probe_step_mm,
        cfg.max_probe_step_mm);

    auto linear_seed_for_target = [&](double target_x, double target_y) {
        const double dx = target_x - model.chief_stop_x;
        const double dy = target_y - model.chief_stop_y;
        FirstSurfaceSeed seed{
            model.chief_seed.x + model.b11 * dx + model.b12 * dy,
            model.chief_seed.y + model.b21 * dx + model.b22 * dy,
            true};
        seed.valid = std::isfinite(seed.x) && std::isfinite(seed.y);
        return seed;
    };
    auto linear_seed_for_pupil = [&](const PupilPoint& pupil) {
        const double target_x = (field.fvcx + pupil.px * (1.0 - field.fvdx)) * rs;
        const double target_y = (field.fvcy + pupil.py * (1.0 - field.fvdy)) * rs;
        return linear_seed_for_target(target_x, target_y);
    };
    auto build_edge_calibration = [&]() {
        return ::optiland::raytrace_detail::CalibrationBuildEdgeCalibration::run(*this, 
            wave,
            diag,
            model,
            cfg,
            field,
            rs,
            stop_scale,
            radius,
            seed_in_first_surface_bounds,
            linear_seed_for_pupil
        );
    };

    auto finish_from_jacobian = [&](double j11,
                                    double j12,
                                    double j21,
                                    double j22,
                                    const std::vector<PilotSample>& samples) {
        const double det = j11 * j22 - j12 * j21;
        if (!std::isfinite(det) || std::abs(det) <= 1.0e-14) {
            return false;
        }
        const double condition = condition_number_2x2(j11, j12, j21, j22);
        if (!std::isfinite(condition) || condition > cfg.max_condition_number) {
            return false;
        }
        double sum_r2 = 0.0;
        int count = 0;
        for (const auto& sample : samples) {
            if (!sample.valid) {
                continue;
            }
            const double dx = sample.seed.x - model.chief_seed.x;
            const double dy = sample.seed.y - model.chief_seed.y;
            const double pred_x = model.chief_stop_x + j11 * dx + j12 * dy;
            const double pred_y = model.chief_stop_y + j21 * dx + j22 * dy;
            const double ex = pred_x - sample.stop_x;
            const double ey = pred_y - sample.stop_y;
            sum_r2 += ex * ex + ey * ey;
            ++count;
        }
        if (count <= 0) {
            return false;
        }
        const double rms = std::sqrt(sum_r2 / static_cast<double>(count));
        const double fit_limit = std::max(
            system_.ray_aiming.tolerance,
            cfg.fit_rms_tolerance_stop_fraction * stop_scale);
        if (!std::isfinite(rms) || rms > fit_limit) {
            return false;
        }
        model.b11 = j22 / det;
        model.b12 = -j12 / det;
        model.b21 = -j21 / det;
        model.b22 = j11 / det;
        model.fit_rms = rms;
        model.condition_number = condition;
        model.valid = true;
        build_edge_calibration();
        return true;
    };

    std::vector<PilotSample> samples;
    samples.push_back({model.chief_seed, model.chief_stop_x, model.chief_stop_y, true});
    auto add_sample = [&](FirstSurfaceSeed seed) {
        add_diag(diag.calibrated_probe_sample_calls);
        double sx = 0.0;
        double sy = 0.0;
        bool stop_ok = false;
        {
            ScopedDiagTimer probe_timer(diag.calibrated_probe_sample_ns);
            stop_ok = sample_stop_from_seed(field, wave, seed, &sx, &sy);
        }
        if (stop_ok) {
            add_diag(diag.calibrated_probe_sample_successes);
            samples.push_back({seed, sx, sy, true});
            return true;
        }
        return false;
    };

    auto try_central_fit = [&](double h) {
        add_diag(diag.calibrated_central_fit_attempts);
        ScopedDiagTimer central_timer(diag.calibrated_central_fit_ns);
        samples.clear();
        samples.push_back({model.chief_seed, model.chief_stop_x, model.chief_stop_y, true});
        const FirstSurfaceSeed xp{model.chief_seed.x + h, model.chief_seed.y, true};
        const FirstSurfaceSeed xm{model.chief_seed.x - h, model.chief_seed.y, true};
        const FirstSurfaceSeed yp{model.chief_seed.x, model.chief_seed.y + h, true};
        const FirstSurfaceSeed ym{model.chief_seed.x, model.chief_seed.y - h, true};
        const bool central_ok =
            add_sample(xp) && add_sample(xm) && add_sample(yp) && add_sample(ym);
        if (!central_ok || samples.size() < 5) {
            return false;
        }
        const auto& sxp = samples[1];
        const auto& sxm = samples[2];
        const auto& syp = samples[3];
        const auto& sym = samples[4];
        const double j11 = (sxp.stop_x - sxm.stop_x) / (2.0 * h);
        const double j21 = (sxp.stop_y - sxm.stop_y) / (2.0 * h);
        const double j12 = (syp.stop_x - sym.stop_x) / (2.0 * h);
        const double j22 = (syp.stop_y - sym.stop_y) / (2.0 * h);
        const bool ok = finish_from_jacobian(j11, j12, j21, j22, samples);
        if (ok) {
            add_diag(diag.calibrated_central_fit_successes);
        }
        return ok;
    };

    for (double h : probe_steps) {
        if (try_central_fit(h)) {
            return finish_model();
        }
    }

    if (!cfg.allow_grid_upgrade) {
        return finish_model();
    }

    auto try_grid_fit = [&](double h) {
        return ::optiland::raytrace_detail::CalibrationTryGridFit::run(*this, diag, model, cfg, finish_from_jacobian, samples, add_sample, h);
    };

    for (double h : probe_steps) {
        if (try_grid_fit(h)) {
            return finish_model();
        }
    }
    return finish_model();
}

} // namespace optiland
