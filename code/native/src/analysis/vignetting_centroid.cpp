/*
 * Locates a feasible connected pupil component and its sampled centroid for a field.
 * Automatic vignetting uses this center as the seed for its four boundary searches.
 * The stop center and supplied previous centers select a branch without changing the
 * measured hard-aperture domain; grid cell ordering and feasibility masks are retained.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
bool AutomaticVignettingFieldSolver::grid_centroid(double* centroid_x, double* centroid_y) {
        if (!centroid_x || !centroid_y) {
            return false;
        }
        const int grid_count = std::max(4, config_.centroid_grid_cells);
        std::vector<unsigned char> feasible(
            static_cast<std::size_t>(grid_count * grid_count), 0);
        std::vector<double> coordinates(static_cast<std::size_t>(grid_count), 0.0);
        for (int index = 0; index < grid_count; ++index) {
            coordinates[static_cast<std::size_t>(index)] =
                -1.0 +
                2.0 * (static_cast<double>(index) + 0.5) /
                    static_cast<double>(grid_count);
        }

        // The unvignetted stop center is the first seed. A previous
        // aperture-realized vignetting center is the second choice and is
        // particularly useful after a DLS geometry-only stage.  Neither seed
        // changes the measured domain; it only selects the connected pupil
        // component to which the four boundary searches belong.
        std::vector<std::pair<double, double>> seed_candidates{{0.0, 0.0}};
        for (const auto& seed : preferred_seeds_) {
            if (!std::isfinite(seed.first) || !std::isfinite(seed.second) ||
                std::hypot(seed.first, seed.second) >
                    1.0 + std::max(0.0, config_.pupil_tolerance)) {
                continue;
            }
            const bool duplicate = std::any_of(
                seed_candidates.begin(), seed_candidates.end(),
                [&](const auto& existing) {
                    return std::hypot(
                               existing.first - seed.first,
                               existing.second - seed.second) <= 1.0e-14;
                });
            if (!duplicate) {
                seed_candidates.push_back(seed);
            }
        }
        bool exact_seed_feasible = false;
        std::pair<double, double> selected_seed{0.0, 0.0};
        std::size_t selected_seed_position = 0;
        for (std::size_t position = 0; position < seed_candidates.size(); ++position) {
            const auto& seed = seed_candidates[position];
            Sample sample = evaluate(seed.first, seed.second);
            if (!sample.feasible &&
                config_.aiming_mode == RayAimingMode::AdaptivePilotGrid &&
                !branch_recovery_enabled_) {
                sample = recover_physical_seed(seed.first, seed.second);
                for (const auto& solver : additional_wave_solvers_) {
                    if (!sample.feasible) break;
                    sample.feasible = solver->recover_physical_seed(
                        seed.first, seed.second).feasible;
                }
                if (sample.feasible) {
                    branch_seeds_.push_back({seed.first, seed.second, sample.ray});
                    enable_branch_recovery();
                }
            }
            if (sample.feasible) {
                exact_seed_feasible = true;
                selected_seed = seed;
                selected_seed_position = position;
                break;
            }
        }

        std::vector<std::pair<double, double>> grid_pupils;
        std::vector<std::size_t> grid_flats;
        grid_pupils.reserve(static_cast<std::size_t>(grid_count * grid_count));
        grid_flats.reserve(static_cast<std::size_t>(grid_count * grid_count));
        for (int iy = 0; iy < grid_count; ++iy) {
            const double pupil_y = coordinates[static_cast<std::size_t>(iy)];
            for (int ix = 0; ix < grid_count; ++ix) {
                const double pupil_x = coordinates[static_cast<std::size_t>(ix)];
                if (std::hypot(pupil_x, pupil_y) > 1.0) {
                    continue;
                }
                grid_pupils.push_back({pupil_x, pupil_y});
                grid_flats.push_back(
                    static_cast<std::size_t>(iy * grid_count + ix));
            }
        }
        std::vector<Sample> grid_samples;
        evaluate_batch(grid_pupils, &grid_samples);
        std::size_t total_feasible = 0;
        for (std::size_t index = 0; index < grid_samples.size(); ++index) {
            if (grid_samples[index].feasible) {
                feasible[grid_flats[index]] = 1;
                ++total_feasible;
            }
        }
        if (total_feasible == 0) {
            if (exact_seed_feasible) {
                *centroid_x = selected_seed.first;
                *centroid_y = selected_seed.second;
                seed_source_ = selected_seed_position == 0
                                   ? "chief_ray"
                                   : "previous_vignetting_center";
                connected_sample_count_ = 1;
                return true;
            }
            return false;
        }

        const auto target = exact_seed_feasible
                                ? selected_seed
                                : seed_candidates.front();
        int seed_ix = -1;
        int seed_iy = -1;
        double best_distance = std::numeric_limits<double>::infinity();
        for (int iy = 0; iy < grid_count; ++iy) {
            for (int ix = 0; ix < grid_count; ++ix) {
                const std::size_t flat =
                    static_cast<std::size_t>(iy * grid_count + ix);
                if (!feasible[flat]) {
                    continue;
                }
                const double distance = std::hypot(
                    coordinates[static_cast<std::size_t>(ix)] - target.first,
                    coordinates[static_cast<std::size_t>(iy)] - target.second);
                if (distance < best_distance) {
                    best_distance = distance;
                    seed_ix = ix;
                    seed_iy = iy;
                }
            }
        }
        if (seed_ix < 0 || seed_iy < 0) {
            return false;
        }

        std::vector<unsigned char> visited(feasible.size(), 0);
        std::queue<std::pair<int, int>> frontier;
        frontier.push({seed_ix, seed_iy});
        visited[static_cast<std::size_t>(seed_iy * grid_count + seed_ix)] = 1;
        double sum_x = 0.0;
        double sum_y = 0.0;
        std::size_t connected_count = 0;
        while (!frontier.empty()) {
            const auto current = frontier.front();
            frontier.pop();
            const int ix = current.first;
            const int iy = current.second;
            sum_x += coordinates[static_cast<std::size_t>(ix)];
            sum_y += coordinates[static_cast<std::size_t>(iy)];
            ++connected_count;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    const int nx = ix + dx;
                    const int ny = iy + dy;
                    if (nx < 0 || ny < 0 || nx >= grid_count || ny >= grid_count) {
                        continue;
                    }
                    const std::size_t flat =
                        static_cast<std::size_t>(ny * grid_count + nx);
                    if (!feasible[flat] || visited[flat]) {
                        continue;
                    }
                    visited[flat] = 1;
                    frontier.push({nx, ny});
                }
            }
        }
        if (connected_count == 0) {
            return false;
        }
        if (config_.aiming_mode == RayAimingMode::AdaptivePilotGrid) {
            branch_seeds_.clear();
            branch_seeds_.reserve(connected_count);
            for (std::size_t i = 0; i < grid_samples.size(); ++i) {
                if (visited[grid_flats[i]] && grid_samples[i].feasible) {
                    branch_seeds_.push_back({grid_pupils[i].first,
                        grid_pupils[i].second, grid_samples[i].ray});
                }
            }
            // The coarse grid locates a component; it does not certify its
            // boundary. Continue from its physically verified rays even when
            // the original chord solve would have returned a small "success".
            enable_branch_recovery();
            if (branch_recovery_enabled_) {
                batch_apg_worker_pool_.clear();
                for (auto& solver : additional_wave_solvers_) {
                    solver->branch_seeds_ = branch_seeds_;
                }
            }
        }
        *centroid_x = sum_x / static_cast<double>(connected_count);
        *centroid_y = sum_y / static_cast<double>(connected_count);
        seed_source_ = exact_seed_feasible
                           ? (selected_seed_position == 0
                                  ? "chief_ray"
                                  : "previous_vignetting_center")
                           : "coarse_connected_component";
        connected_sample_count_ = connected_count;
        return std::isfinite(*centroid_x) && std::isfinite(*centroid_y);
    }
}  // namespace analysis_detail

}  // namespace optiland
