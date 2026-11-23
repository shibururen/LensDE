/*
 * Evaluates batches of field-solver pupil targets, including the APG worker path.
 * Grid-centroid and boundary searches call these methods to obtain indexed samples.
 * Worker-local aiming/tracing state and result indices preserve the original target
 * ordering and keep parallel batch execution compatible with scalar feasibility tests.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
void AutomaticVignettingFieldSolver::run_batch_apg_thread(BatchApgThreadContext* context) {
        if (!context || !context->worker || !context->pupils ||
            !context->samples || !context->next_index) {
            return;
        }
        while (true) {
            const std::size_t index = context->next_index->fetch_add(
                1, std::memory_order_relaxed);
            if (index >= context->pupils->size()) {
                return;
            }
            (*context->samples)[index] = context->worker->evaluate(
                (*context->pupils)[index].first,
                (*context->pupils)[index].second);
        }
    }

void AutomaticVignettingFieldSolver::evaluate_batch(
        const std::vector<std::pair<double, double>>& pupils,
        std::vector<Sample>* samples) {
        if (!samples) {
            return;
        }
        samples->resize(pupils.size());
        const int requested_workers = std::max(
            std::max(1, config_.batch_apg_workers),
            automatic_vignetting_batch_apg_workers());
        const bool run_batch_apg =
            (config_.use_batch_apg ||
             automatic_vignetting_batch_apg_experiment_enabled()) &&
            config_.aiming_mode == RayAimingMode::AdaptivePilotGrid &&
            additional_wave_solvers_.empty() && pupils.size() >= 8;
        if (run_batch_apg) {
            const std::size_t worker_count = std::min<std::size_t>(
                pupils.size(), static_cast<std::size_t>(requested_workers));
            if (batch_apg_worker_pool_.size() != worker_count) {
                batch_apg_worker_pool_.clear();
                LensSystem worker_system =
                    automatic_vignetting_local_apg_worker_system(system_);
                batch_apg_worker_pool_.reserve(worker_count);
                for (std::size_t worker_index = 0;
                     worker_index < worker_count;
                     ++worker_index) {
                    auto worker =
                        std::make_unique<AutomaticVignettingFieldSolver>(
                            worker_system,
                            field_index_,
                            wave_index_,
                            config_,
                            nullptr,
                            preferred_seeds_);
                    worker->independent_apg_targets_ = true;
                    worker->branch_recovery_enabled_ = branch_recovery_enabled_;
                    worker->branch_seeds_ = branch_seeds_;
                    batch_apg_worker_pool_.push_back(std::move(worker));
                }
            }
            auto& workers = batch_apg_worker_pool_;
            std::vector<std::size_t> prior_pupil_evaluations(worker_count);
            std::vector<std::size_t> prior_envelope_traces(worker_count);
            std::vector<std::size_t> prior_aperture_tests(worker_count);
            std::vector<double> prior_aiming_seconds(worker_count);
            std::vector<double> prior_envelope_seconds(worker_count);
            for (std::size_t worker_index = 0;
                 worker_index < worker_count;
                 ++worker_index) {
                prior_pupil_evaluations[worker_index] =
                    workers[worker_index]->pupil_evaluations_;
                prior_envelope_traces[worker_index] =
                    workers[worker_index]->envelope_trace_calls_;
                prior_aperture_tests[worker_index] =
                    workers[worker_index]->hard_aperture_surface_tests_;
                prior_aiming_seconds[worker_index] =
                    workers[worker_index]->aiming_seconds_;
                prior_envelope_seconds[worker_index] =
                    workers[worker_index]->envelope_trace_seconds_;
            }

            std::atomic<std::size_t> next_index{0};
            std::vector<BatchApgThreadContext> contexts(worker_count);
#ifdef _WIN32
            std::vector<HANDLE> threads;
            threads.reserve(worker_count);
#else
            std::vector<std::thread> threads;
            threads.reserve(worker_count);
#endif
            for (std::size_t worker_index = 0;
                 worker_index < worker_count;
                 ++worker_index) {
                contexts[worker_index] = {
                    workers[worker_index].get(),
                    &pupils,
                    samples,
                    &next_index};
            }
            if (worker_count == 1) {
                // Independent fields may already occupy the configured CPU
                // workers. Keep the current-field APG contract here without
                // creating a short-lived nested thread for every coarse batch.
                run_batch_apg_thread(&contexts.front());
            } else {
                for (std::size_t worker_index = 0;
                     worker_index < worker_count;
                     ++worker_index) {
#ifdef _WIN32
                    HANDLE thread = CreateThread(
                        nullptr,
                        0,
                        &AutomaticVignettingFieldSolver::batch_apg_thread_entry,
                        &contexts[worker_index],
                        0,
                        nullptr);
                    if (thread) {
                        threads.push_back(thread);
                    } else {
                        run_batch_apg_thread(&contexts[worker_index]);
                    }
#else
                    threads.emplace_back(
                        &AutomaticVignettingFieldSolver::run_batch_apg_thread,
                        &contexts[worker_index]);
#endif
                }
#ifdef _WIN32
                for (HANDLE thread : threads) {
                    WaitForSingleObject(thread, INFINITE);
                    CloseHandle(thread);
                }
#else
                for (auto& thread : threads) {
                    thread.join();
                }
#endif
            }

            batch_apg_evaluations_ += pupils.size();
            batch_apg_workers_ = std::max(batch_apg_workers_, worker_count);
            for (std::size_t worker_index = 0;
                 worker_index < worker_count;
                 ++worker_index) {
                const auto& worker = workers[worker_index];
                pupil_evaluations_ +=
                    worker->pupil_evaluations_ -
                    prior_pupil_evaluations[worker_index];
                envelope_trace_calls_ +=
                    worker->envelope_trace_calls_ -
                    prior_envelope_traces[worker_index];
                hard_aperture_surface_tests_ +=
                    worker->hard_aperture_surface_tests_ -
                    prior_aperture_tests[worker_index];
                aiming_seconds_ +=
                    worker->aiming_seconds_ -
                    prior_aiming_seconds[worker_index];
                envelope_trace_seconds_ +=
                    worker->envelope_trace_seconds_ -
                    prior_envelope_seconds[worker_index];
            }
            for (const Sample& sample : *samples) {
                if (!sample.aimed) {
                    ++batch_apg_aim_failures_;
                }
            }

            if (config_.audit_batch_apg ||
                automatic_vignetting_batch_apg_audit_enabled()) {
                for (std::size_t index = 0; index < pupils.size(); ++index) {
                    const Sample parallel = (*samples)[index];
                    const Sample serial = evaluate(
                        pupils[index].first, pupils[index].second);
                    if (parallel.aimed != serial.aimed) {
                        ++batch_apg_aimed_mismatches_;
                    }
                    if (parallel.feasible != serial.feasible) {
                        ++batch_apg_feasible_mismatches_;
                    }
                    if (parallel.limiting_surface != serial.limiting_surface) {
                        ++batch_apg_limiting_surface_mismatches_;
                    }
                    batch_apg_max_boundary_ratio_delta_ = std::max(
                        batch_apg_max_boundary_ratio_delta_,
                        std::abs(
                            parallel.max_boundary_ratio -
                            serial.max_boundary_ratio));
                    if (parallel.aimed && serial.aimed) {
                        Vec3 parallel_seed;
                        Vec3 serial_seed;
                        const bool parallel_seed_valid =
                            reference_tracer_.first_surface_point(
                                parallel.ray, &parallel_seed);
                        const bool serial_seed_valid =
                            reference_tracer_.first_surface_point(
                                serial.ray, &serial_seed);
                        if (parallel_seed_valid != serial_seed_valid) {
                            ++batch_apg_seed_mismatches_;
                        } else if (parallel_seed_valid) {
                            const double delta = std::hypot(
                                parallel_seed.x - serial_seed.x,
                                parallel_seed.y - serial_seed.y);
                            batch_apg_max_seed_delta_ = std::max(
                                batch_apg_max_seed_delta_, delta);
                            if (delta > 1.0e-9) {
                                ++batch_apg_seed_mismatches_;
                            }
                        }
                    }
                    // Audit mode reports the independent result but continues
                    // with the serial sample, preserving the reference solve.
                    (*samples)[index] = serial;
                }
            } else {
                for (std::size_t index = 0; index < pupils.size(); ++index) {
                    if ((*samples)[index].aimed) {
                        continue;
                    }
                    ++batch_apg_serial_recoveries_;
                    (*samples)[index] = evaluate(
                        pupils[index].first, pupils[index].second);
                }
            }
            return;
        }
        for (std::size_t index = 0; index < pupils.size(); ++index) {
            (*samples)[index] =
                evaluate(pupils[index].first, pupils[index].second);
        }
    }
}  // namespace analysis_detail

}  // namespace optiland
