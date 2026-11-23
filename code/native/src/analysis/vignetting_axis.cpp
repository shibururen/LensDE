/*
 * Solves one horizontal/vertical feasible pupil interval from coarse and preferred samples.
 * Centered-section contraction calls this method for each axis of a field solution.
 * The unit-stop line limit, connected feasible branch and boundary-refinement tolerance
 * must keep valid-inside and invalid-outside endpoints associated with the same pupil.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
AutomaticVignettingFieldSolver::Interval AutomaticVignettingFieldSolver::solve_axis(bool x_axis, double transverse, double preferred) {
        Interval result;
        if (!std::isfinite(transverse) || std::abs(transverse) > 1.0) {
            result.message = "transverse pupil coordinate is outside the unit stop";
            return result;
        }
        const double limit = std::sqrt(std::max(0.0, 1.0 - transverse * transverse));
        if (!(limit > 0.0)) {
            result.message = "unit-stop line has zero width";
            return result;
        }
        preferred = std::clamp(preferred, -limit, limit);
        const int uniform_sample_count = std::max(5, config_.coarse_samples | 1);
        std::vector<double> coordinates;
        coordinates.reserve(static_cast<std::size_t>(uniform_sample_count + 1));
        for (int i = 0; i < uniform_sample_count; ++i) {
            const double coordinate =
                -limit + 2.0 * limit * static_cast<double>(i) /
                             static_cast<double>(uniform_sample_count - 1);
            coordinates.push_back(coordinate);
        }
        if (std::none_of(
                coordinates.begin(), coordinates.end(),
                [&](double coordinate) {
                    return std::abs(coordinate - preferred) <= 1.0e-14;
                })) {
            coordinates.push_back(preferred);
            std::sort(coordinates.begin(), coordinates.end());
        }
        const int sample_count = static_cast<int>(coordinates.size());
        std::vector<std::pair<double, double>> coarse_pupils;
        coarse_pupils.reserve(coordinates.size());
        for (int i = 0; i < sample_count; ++i) {
            const double coordinate = coordinates[static_cast<std::size_t>(i)];
            coarse_pupils.push_back({
                x_axis ? coordinate : transverse,
                x_axis ? transverse : coordinate});
        }
        std::vector<Sample> samples;
        evaluate_batch(coarse_pupils, &samples);

        int best_begin = -1;
        int best_end = -1;
        double best_score = -1.0;
        auto select_best_interval = [&]() {
            best_begin = -1;
            best_end = -1;
            best_score = -1.0;
            for (int begin = 0; begin < sample_count;) {
                while (begin < sample_count &&
                       !samples[static_cast<std::size_t>(begin)].feasible) {
                    ++begin;
                }
                if (begin >= sample_count) break;
                int end = begin;
                while (end + 1 < sample_count &&
                       samples[static_cast<std::size_t>(end + 1)].feasible) {
                    ++end;
                }
                const bool contains_preferred =
                    coordinates[static_cast<std::size_t>(begin)] <= preferred &&
                    preferred <= coordinates[static_cast<std::size_t>(end)];
                const double width =
                    coordinates[static_cast<std::size_t>(end)] -
                    coordinates[static_cast<std::size_t>(begin)];
                const double score =
                    width + (contains_preferred ? 4.0 * limit : 0.0);
                if (score > best_score) {
                    best_score = score;
                    best_begin = begin;
                    best_end = end;
                }
                begin = end + 1;
            }
        };
        select_best_interval();
        if (best_begin < 0 &&
            config_.aiming_mode == RayAimingMode::AdaptivePilotGrid &&
            recover_axis_seed(
                x_axis ? preferred : transverse,
                x_axis ? transverse : preferred)) {
            // A connected pupil can cross an inverse-ray branch fold. The
            // frozen grid bank may then miss every point on a later centered
            // chord even though the chord is physically reachable. Reacquire
            // one strict seed at the requested centre and replay the same
            // deterministic interval scan; ordinary residual/aperture checks
            // still decide whether every returned sample is feasible.
            evaluate_batch(coarse_pupils, &samples);
            select_best_interval();
        }
        if (best_begin < 0) {
            result.message = "no traceable interval was found on the pupil axis";
            return result;
        }

        double lower = coordinates[static_cast<std::size_t>(best_begin)];
        Sample lower_sample = samples[static_cast<std::size_t>(best_begin)];
        if (best_begin > 0) {
            double outside = coordinates[static_cast<std::size_t>(best_begin - 1)];
            double inside = lower;
            // A failed coarse aim does not establish an aperture boundary.
            // Recheck it with the same continuation used below, walking outward
            // until that predicate fails or the unit-stop edge is reached.
            for (int index = best_begin - 1;; --index) {
                const Sample sample = evaluate_from_inside(
                    x_axis ? outside : transverse,
                    x_axis ? transverse : outside,
                    {x_axis ? inside : transverse,
                     x_axis ? transverse : inside, lower_sample.ray});
                if (!sample.feasible) {
                    break;
                }
                inside = outside;
                lower_sample = sample;
                if (index == 0) {
                    break;
                }
                outside = coordinates[static_cast<std::size_t>(index - 1)];
            }
            for (int iteration = 0;
                 outside != inside &&
                 iteration < std::max(1, config_.boundary_iterations);
                 ++iteration) {
                const double middle = 0.5 * (outside + inside);
                const Sample sample = evaluate_from_inside(
                    x_axis ? middle : transverse,
                    x_axis ? transverse : middle,
                    {x_axis ? inside : transverse,
                     x_axis ? transverse : inside, lower_sample.ray});
                if (sample.feasible) {
                    inside = middle;
                    lower_sample = sample;
                } else {
                    outside = middle;
                }
            }
            lower = inside;
        }

        double upper = coordinates[static_cast<std::size_t>(best_end)];
        Sample upper_sample = samples[static_cast<std::size_t>(best_end)];
        if (best_end + 1 < sample_count) {
            double inside = upper;
            double outside = coordinates[static_cast<std::size_t>(best_end + 1)];
            // Establish the upper bracket using continuation as well. Coarse
            // failures can be recovered on either side of the pupil axis.
            for (int index = best_end + 1;; ++index) {
                const Sample sample = evaluate_from_inside(
                    x_axis ? outside : transverse,
                    x_axis ? transverse : outside,
                    {x_axis ? inside : transverse,
                     x_axis ? transverse : inside, upper_sample.ray});
                if (!sample.feasible) {
                    break;
                }
                inside = outside;
                upper_sample = sample;
                if (index + 1 == sample_count) {
                    break;
                }
                outside = coordinates[static_cast<std::size_t>(index + 1)];
            }
            for (int iteration = 0;
                 outside != inside &&
                 iteration < std::max(1, config_.boundary_iterations);
                 ++iteration) {
                const double middle = 0.5 * (inside + outside);
                const Sample sample = evaluate_from_inside(
                    x_axis ? middle : transverse,
                    x_axis ? transverse : middle,
                    {x_axis ? inside : transverse,
                     x_axis ? transverse : inside, upper_sample.ray});
                if (sample.feasible) {
                    inside = middle;
                    upper_sample = sample;
                } else {
                    outside = middle;
                }
            }
            upper = inside;
        }
        if (!(upper > lower)) {
            result.message = "traceable pupil interval collapsed";
            return result;
        }

        // Keep the final feasible samples found during refinement.  Session
        // modes can update or invalidate a calibrated model while scanning a
        // different part of the pupil; re-aiming an already accepted endpoint
        // here made the boundary result depend on evaluation history and could
        // turn a converged calibrated-paraxial interval into a false failure.
        result.valid = lower_sample.feasible && upper_sample.feasible;
        result.lower = lower;
        result.upper = upper;
        result.lower_surface = lower_sample.limiting_surface;
        result.upper_surface = upper_sample.limiting_surface;
        if (!result.valid) {
            result.message = "refined pupil boundary was not traceable";
        }
        return result;
    }
}  // namespace analysis_detail

}  // namespace optiland
