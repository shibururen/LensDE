#pragma once
#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace optiland {
enum class VariableKind {
    Radius,
    Thickness,
    MaterialNd,
    MaterialVd,
    VignetteX,
    VignetteY,
    Conic,
    AsphereCoeff,
    Curvature,
    GlassChoice,
    // Evaluator-only fields used to canonicalize heterogeneous island bases.
    // They are never exposed as optimizer dimensions or parsed from user JSON.
    EvaluatorSemiAperture,
    EvaluatorMechanicalAperture,
    EvaluatorApertureRadius,
    EvaluatorMaterialIndex,
    // Y pupil-center shift: P'y = fvcy + (1 - fvdy) * Py.
    // Kept distinct from VignetteY, which is the FVDY scale reduction.
    VignetteCenterY,
    // Feasible automatic meridional-vignetting parameterization.  The
    // magnitude stores FVDY and the asymmetry stores eta in [-1, 1]; the
    // physical center shift is reconstructed as FVCY = eta * FVDY.  Keeping
    // these distinct from the legacy direct coordinates preserves explicit
    // user-authored FVCY/FVDY contracts while preventing automatic searches
    // from generating |FVCY| > FVDY.
    VignetteMagnitudeY,
    VignetteAsymmetryY,
    // Additional evaluator-only field coordinates used by the batched CUDA
    // broker when island bases carry different field grids or X pupil shifts.
    // As with the evaluator-only surface fields above, these are appended to
    // packed scorer rows and are never optimizer dimensions or JSON inputs.
    EvaluatorFieldHx,
    EvaluatorFieldHy,
    EvaluatorVignetteCenterX,
};

struct VariableSpec {
    VariableKind kind = VariableKind::Radius;
    std::size_t index = 0;
    double lower = 0.0;
    double upper = 0.0;
    double initial = 0.0;
    std::string role;
    std::size_t term = 0;
    int glass_group = -1;
    // Optional frozen parameterization metadata for AsphereCoeff variables.
    // The physical value remains the conventional A_(2m) coefficient. When all
    // three values are valid, optimizers may use q=A*r_ref^order internally.
    double asphere_reference_radius = 0.0;
    double edge_sag_lower = std::numeric_limits<double>::quiet_NaN();
    double edge_sag_upper = std::numeric_limits<double>::quiet_NaN();
};

inline bool is_vignetting_variable_kind(VariableKind kind) {
    return kind == VariableKind::VignetteX ||
           kind == VariableKind::VignetteY ||
           kind == VariableKind::VignetteCenterY ||
           kind == VariableKind::VignetteMagnitudeY ||
           kind == VariableKind::VignetteAsymmetryY;
}

// Vignetting coordinates remain genuine optimizer dimensions, but they are
// auxiliary pupil-aiming controls and must not enlarge populations selected
// through a popsize multiplier or an adaptive minimum-population multiplier.
inline int population_sizing_dimension(
    const std::vector<VariableSpec>& variables) {
    int count = 0;
    for (const auto& variable : variables) {
        if (!is_vignetting_variable_kind(variable.kind)) {
            ++count;
        }
    }
    return std::max(1, count);
}

// Project only the small set of variables that need a hard numerical domain.
// Variable lower/upper values remain initialization/scaling metadata for every
// other continuous kind; in particular curvature is intentionally unbounded
// during optimization.
double project_variable_value(const VariableSpec& variable, double value);
void project_variable_values_in_place(
    const std::vector<VariableSpec>& variables,
    std::vector<double>* values);


}  // namespace optiland
