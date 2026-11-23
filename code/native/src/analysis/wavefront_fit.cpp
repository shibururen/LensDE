/*
 * Fits centroid direction-cosine wavefront models and estimates projected pupil area.
 * Wavefront sampling and illumination-proxy calculations use these numerical helpers.
 * Non-finite or rank-deficient normal equations preserve failure results; sample
 * weights, pivot tolerances and piston/reference terms define the fitted OPD model.
 */

#include "detail.hpp"

namespace optiland {
using namespace analysis_detail;

namespace analysis_detail {
bool solve_normal_equations(
    const std::vector<std::vector<double>>& normal,
    const std::vector<double>& rhs,
    std::vector<double>& solution) {
    const std::size_t n = rhs.size();
    if (n == 0 || normal.size() != n) {
        return false;
    }
    std::vector<std::vector<double>> a(n, std::vector<double>(n + 1, 0.0));
    double scale = 0.0;
    for (std::size_t r = 0; r < n; ++r) {
        if (normal[r].size() != n || !std::isfinite(rhs[r])) {
            return false;
        }
        for (std::size_t c = 0; c < n; ++c) {
            if (!std::isfinite(normal[r][c])) {
                return false;
            }
            a[r][c] = normal[r][c];
            scale = std::max(scale, std::abs(normal[r][c]));
        }
        a[r][n] = rhs[r];
    }
    const double pivot_tolerance = std::max(1.0e-14, scale * 1.0e-12);
    for (std::size_t col = 0; col < n; ++col) {
        std::size_t pivot_row = col;
        double pivot_abs = std::abs(a[col][col]);
        for (std::size_t r = col + 1; r < n; ++r) {
            const double candidate = std::abs(a[r][col]);
            if (candidate > pivot_abs) {
                pivot_abs = candidate;
                pivot_row = r;
            }
        }
        if (!std::isfinite(pivot_abs) || pivot_abs <= pivot_tolerance) {
            return false;
        }
        if (pivot_row != col) {
            std::swap(a[pivot_row], a[col]);
        }
        const double pivot = a[col][col];
        for (std::size_t c = col; c <= n; ++c) {
            a[col][c] /= pivot;
        }
        a[col][col] = 1.0;
        for (std::size_t r = 0; r < n; ++r) {
            if (r == col) {
                continue;
            }
            const double factor = a[r][col];
            if (std::abs(factor) <= pivot_tolerance) {
                a[r][col] = 0.0;
                continue;
            }
            for (std::size_t c = col; c <= n; ++c) {
                a[r][c] -= factor * a[col][c];
            }
            a[r][col] = 0.0;
        }
    }
    solution.assign(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        if (!std::isfinite(a[i][n])) {
            return false;
        }
        solution[i] = a[i][n];
    }
    return true;
}
}  // namespace analysis_detail

namespace analysis_detail {
double sparse_projected_illumination_area(
    const Field& field,
    const std::vector<PupilPoint>& pupils,
    const std::vector<Ray>& rays,
    RayAimingMode aiming_mode,
    int stop_surface_index) {
    if (pupils.size() != rays.size() || pupils.size() < 3) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    constexpr std::size_t kColumns = 3;
    std::vector<std::vector<double>> normal(
        kColumns, std::vector<double>(kColumns, 0.0));
    std::vector<double> rhs_l(kColumns, 0.0);
    std::vector<double> rhs_m(kColumns, 0.0);
    double total_weight = 0.0;
    double valid_intensity = 0.0;
    std::size_t fit_count = 0;
    for (std::size_t i = 0; i < rays.size(); ++i) {
        const Ray& ray = rays[i];
        const double nominal = ray_nominal_weight(ray);
        // A calibrated-paraxial ray rejected only because the interpolated
        // sample lands outside the designated STOP is not a physical pupil
        // sample.  Exclude it from both the illumination fit and its
        // throughput denominator, matching coverage and the CUDA scorer.
        if (calibrated_paraxial_stop_sampling_rejection(
                ray, aiming_mode, stop_surface_index)) {
            continue;
        }
        if (std::isfinite(nominal) && nominal > 0.0) {
            total_weight += nominal;
        }
        if (!ray.valid || !(nominal > 0.0) ||
            !std::isfinite(ray.d.x) || !std::isfinite(ray.d.y)) {
            continue;
        }
        const double qx = field.fvcx + (1.0 - field.fvdx) * pupils[i].px;
        const double qy = field.fvcy + (1.0 - field.fvdy) * pupils[i].py;
        if (!std::isfinite(qx) || !std::isfinite(qy)) {
            continue;
        }
        const double features[kColumns] = {1.0, qx, qy};
        for (std::size_t row = 0; row < kColumns; ++row) {
            rhs_l[row] += nominal * features[row] * ray.d.x;
            rhs_m[row] += nominal * features[row] * ray.d.y;
            for (std::size_t column = 0; column < kColumns; ++column) {
                normal[row][column] +=
                    nominal * features[row] * features[column];
            }
        }
        if (std::isfinite(ray.intensity) && ray.intensity > 0.0) {
            valid_intensity += ray.intensity;
        }
        ++fit_count;
    }
    if (!(total_weight > 0.0) || fit_count < kColumns) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    double diagonal_scale = 0.0;
    for (std::size_t i = 0; i < kColumns; ++i) {
        diagonal_scale = std::max(diagonal_scale, std::abs(normal[i][i]));
    }
    const double ridge = std::max(1.0e-15, diagonal_scale * 1.0e-12);
    for (std::size_t i = 0; i < kColumns; ++i) {
        normal[i][i] += ridge;
    }
    std::vector<double> fit_l;
    std::vector<double> fit_m;
    if (!solve_normal_equations(normal, rhs_l, fit_l) ||
        !solve_normal_equations(normal, rhs_m, fit_m) ||
        fit_l.size() != kColumns || fit_m.size() != kColumns) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double direction_jacobian =
        std::abs(fit_l[1] * fit_m[2] - fit_l[2] * fit_m[1]);
    const double pupil_jacobian =
        std::max(0.0, (1.0 - field.fvdx) * (1.0 - field.fvdy));
    const double throughput = valid_intensity / total_weight;
    const double area = pupil_jacobian * direction_jacobian * throughput;
    return std::isfinite(area) && area >= 0.0
               ? area
               : std::numeric_limits<double>::quiet_NaN();
}
}  // namespace analysis_detail

namespace analysis_detail {
bool fit_centroid_dircos_wavefront_model(
    const std::vector<CentroidDircosWavefrontSample>& samples,
    std::size_t wave_count,
    bool use_dir_x,
    bool use_dir_y,
    CentroidDircosWavefrontFit& fit) {
    if (samples.empty() || wave_count == 0) {
        return false;
    }
    fit = CentroidDircosWavefrontFit{};
    fit.piston_columns.assign(wave_count, -1);
    std::size_t column_count = 0;
    for (const CentroidDircosWavefrontSample& sample : samples) {
        if (sample.wave_index >= wave_count ||
            sample.weight <= 0.0 ||
            !std::isfinite(sample.weight)) {
            continue;
        }
        if (fit.piston_columns[sample.wave_index] < 0) {
            fit.piston_columns[sample.wave_index] = static_cast<int>(column_count++);
        }
    }
    if (column_count == 0) {
        return false;
    }
    if (use_dir_x) {
        fit.dir_x_column = static_cast<int>(column_count++);
    }
    if (use_dir_y) {
        fit.dir_y_column = static_cast<int>(column_count++);
    }
    std::vector<std::vector<double>> normal(
        column_count,
        std::vector<double>(column_count, 0.0));
    std::vector<double> rhs(column_count, 0.0);
    std::size_t fit_sample_count = 0;
    for (const CentroidDircosWavefrontSample& sample : samples) {
        if (sample.wave_index >= wave_count ||
            sample.weight <= 0.0 ||
            !std::isfinite(sample.weight) ||
            !std::isfinite(sample.opd_waves) ||
            !std::isfinite(sample.dir_x) ||
            !std::isfinite(sample.dir_y)) {
            continue;
        }
        const int piston_column = fit.piston_columns[sample.wave_index];
        if (piston_column < 0) {
            continue;
        }
        int columns[3] = {piston_column, fit.dir_x_column, fit.dir_y_column};
        double values[3] = {1.0, sample.dir_x, sample.dir_y};
        for (int i = 0; i < 3; ++i) {
            if (columns[i] < 0) {
                continue;
            }
            const std::size_t ci = static_cast<std::size_t>(columns[i]);
            rhs[ci] += sample.weight * values[i] * sample.opd_waves;
            for (int j = 0; j < 3; ++j) {
                if (columns[j] < 0) {
                    continue;
                }
                const std::size_t cj = static_cast<std::size_t>(columns[j]);
                normal[ci][cj] += sample.weight * values[i] * values[j];
            }
        }
        ++fit_sample_count;
    }
    if (fit_sample_count < column_count) {
        return false;
    }
    return solve_normal_equations(normal, rhs, fit.coefficients);
}
}  // namespace analysis_detail

namespace analysis_detail {
bool build_centroid_piston_only_wavefront_model(
    const std::vector<CentroidDircosWavefrontSample>& samples,
    std::size_t wave_count,
    CentroidDircosWavefrontFit& fit) {
    if (samples.empty() || wave_count == 0) {
        return false;
    }
    fit = CentroidDircosWavefrontFit{};
    fit.piston_columns.assign(wave_count, -1);
    std::size_t column_count = 0;
    for (const CentroidDircosWavefrontSample& sample : samples) {
        if (sample.wave_index >= wave_count ||
            sample.weight <= 0.0 ||
            !std::isfinite(sample.weight) ||
            !std::isfinite(sample.opd_waves)) {
            continue;
        }
        if (fit.piston_columns[sample.wave_index] < 0) {
            fit.piston_columns[sample.wave_index] = static_cast<int>(column_count++);
        }
    }
    if (column_count == 0) {
        return false;
    }
    fit.coefficients.assign(column_count, 0.0);
    std::vector<double> weight_sum(column_count, 0.0);
    for (const CentroidDircosWavefrontSample& sample : samples) {
        if (sample.wave_index >= wave_count ||
            sample.weight <= 0.0 ||
            !std::isfinite(sample.weight) ||
            !std::isfinite(sample.opd_waves)) {
            continue;
        }
        const int column = fit.piston_columns[sample.wave_index];
        if (column < 0) {
            continue;
        }
        const std::size_t ci = static_cast<std::size_t>(column);
        fit.coefficients[ci] += sample.weight * sample.opd_waves;
        weight_sum[ci] += sample.weight;
    }
    for (std::size_t i = 0; i < fit.coefficients.size(); ++i) {
        if (weight_sum[i] <= 0.0) {
            return false;
        }
        fit.coefficients[i] /= weight_sum[i];
    }
    return true;
}
}  // namespace analysis_detail

namespace analysis_detail {
double centroid_dircos_wavefront_model_value(
    const CentroidDircosWavefrontSample& sample,
    const CentroidDircosWavefrontFit& fit) {
    if (sample.wave_index >= fit.piston_columns.size()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const int piston_column = fit.piston_columns[sample.wave_index];
    if (piston_column < 0 ||
        static_cast<std::size_t>(piston_column) >= fit.coefficients.size()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    double value = fit.coefficients[static_cast<std::size_t>(piston_column)];
    if (fit.dir_x_column >= 0) {
        const std::size_t column = static_cast<std::size_t>(fit.dir_x_column);
        if (column >= fit.coefficients.size()) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        value += fit.coefficients[column] * sample.dir_x;
    }
    if (fit.dir_y_column >= 0) {
        const std::size_t column = static_cast<std::size_t>(fit.dir_y_column);
        if (column >= fit.coefficients.size()) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        value += fit.coefficients[column] * sample.dir_y;
    }
    return value;
}
}  // namespace analysis_detail

}  // namespace optiland
