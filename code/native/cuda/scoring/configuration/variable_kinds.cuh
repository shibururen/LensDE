#pragma once
#include "../prelude.cuh"
#include "../contracts/constants.cuh"

namespace optiland {
namespace {
int surface_kind_to_device(SurfaceKind kind) {
    switch (kind) {
        case SurfaceKind::Standard:
            return kKindStandard;
        case SurfaceKind::Plane:
            return kKindPlane;
        case SurfaceKind::EvenAsphere:
            return kKindEvenAsphere;
        case SurfaceKind::Image:
            return kKindImage;
        case SurfaceKind::Object:
            return kKindObject;
    }
    return kKindStandard;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
int variable_kind_to_device(VariableKind kind) {
    switch (kind) {
        case VariableKind::Radius:
            return 0;
        case VariableKind::Thickness:
            return 1;
        case VariableKind::MaterialNd:
            return 2;
        case VariableKind::MaterialVd:
            return 3;
        case VariableKind::VignetteX:
            return 4;
        case VariableKind::VignetteY:
            return 5;
        case VariableKind::Conic:
            return 6;
        case VariableKind::AsphereCoeff:
            return 7;
        case VariableKind::Curvature:
            return 8;
        case VariableKind::GlassChoice:
            return 9;
        case VariableKind::EvaluatorSemiAperture:
            return 10;
        case VariableKind::EvaluatorMechanicalAperture:
            return 11;
        case VariableKind::EvaluatorApertureRadius:
            return 12;
        case VariableKind::EvaluatorMaterialIndex:
            return 13;
        case VariableKind::VignetteCenterY:
            return 14;
        case VariableKind::VignetteMagnitudeY:
            return 15;
        case VariableKind::VignetteAsymmetryY:
            return 16;
        case VariableKind::EvaluatorFieldHx:
            return 17;
        case VariableKind::EvaluatorFieldHy:
            return 18;
        case VariableKind::EvaluatorVignetteCenterX:
            return 19;
    }
    return 0;
}
} // namespace
} // namespace optiland
