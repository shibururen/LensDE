/*
 * Perturb local-funnel candidates in physical coordinates and reject duplicate scaled rows.
 * Adaptive seed/refill paths use run_.normal and run_.rng while retaining the caller variable layout.
 * Preserve per-variable draw order, coordinate projection and first-retained duplicate policy.
 */
#include "adaptive_session.hpp"

namespace optiland { namespace optimizer_detail {

double AdaptiveSession::local_funnel_perturb_physical(const LensSystem& system, const VariableSpec& var, double value, double scale) {
                    const double safe_value =
                        maybe_clip_continuous(
                            var,
                            std::isfinite(value) ? value : var.initial,
                            var.lower,
                            var.upper);
                    const double safe_scale = std::max(0.0, scale);
                    double span = 0.0;
                    switch (var.kind) {
                        case VariableKind::Curvature:
                            span = safe_scale *
                                   std::max(std::abs(safe_value) * 0.14, 0.0035);
                            break;
                        case VariableKind::Thickness:
                            span = safe_scale *
                                   (thickness_variable_is_glass_space(system, var)
                                        ? std::max(std::abs(safe_value) * 0.05, 0.18)
                                        : std::max(std::abs(safe_value) * 0.08, 1.0));
                            break;
                        case VariableKind::MaterialNd:
                            span = safe_scale * 0.025;
                            break;
                        case VariableKind::MaterialVd:
                            span = safe_scale * 8.0;
                            break;
                        case VariableKind::GlassChoice:
                            span = safe_scale *
                                   std::max(1.0, 0.05 * std::max(1.0, std::abs(safe_value)));
                            break;
                        case VariableKind::Conic:
                            span = safe_scale *
                                   std::max(std::abs(safe_value) * 0.20, 5.0);
                            break;
                        case VariableKind::AsphereCoeff:
                            span = safe_scale *
                                   std::max(
                                       {std::max(0.0, var.upper - var.lower) * 0.15,
                                        std::abs(safe_value) * 0.10,
                                        1.0e-12});
                            break;
                        default:
                            span = safe_scale *
                                   std::max(std::abs(safe_value) * 0.05, 0.01);
                            break;
                    }
                    double trial = safe_value;
                    if (span > 0.0) {
                        trial += run_.normal(run_.rng) * span;
                    }
                    if (var.kind == VariableKind::GlassChoice) {
                        trial = std::round(trial);
                    }
                    return maybe_clip_continuous(
                        var,
                        trial,
                        var.lower,
                        var.upper);
                }

bool AdaptiveSession::add_unique_scaled_row(std::vector<std::vector<double>>& rows, const std::vector<double>& scaled) {
                if (scaled.size() != static_cast<std::size_t>(run_.dim) ||
                    this->scaled_row_present(rows, scaled)) {
                    return false;
                }
                rows.push_back(scaled);
                return true;
            }

bool AdaptiveSession::contains_index(const std::vector<int>& values, int needle) {
                    return std::find(values.begin(), values.end(), needle) != values.end();
                }

bool AdaptiveSession::scaled_row_present(const std::vector<std::vector<double>>& rows, const std::vector<double>& scaled) {
                return std::any_of(
                    rows.begin(),
                    rows.end(),
                    [&](const std::vector<double>& existing) {
                        return this->scaled_rows_equivalent(existing, scaled);
                    });
            }

} } // namespace optiland::optimizer_detail
