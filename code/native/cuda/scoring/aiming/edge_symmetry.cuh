#pragma once
#include "../prelude.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/constants.cuh"

namespace optiland {
namespace {
__device__ __forceinline__ void d_uc5_transform_xy(
    int transform_code,
    double source_x,
    double source_y,
    double* target_x,
    double* target_y) {
    double x = source_x;
    double y = source_y;
    if (transform_code == 1) {
        x = -source_x;
    } else if (transform_code == 2) {
        y = -source_y;
    } else if (transform_code == 3) {
        x = -source_x;
        y = -source_y;
    } else if (transform_code == 4) {
        x = source_y;
        y = source_x;
    } else if (transform_code == 5) {
        x = -source_y;
        y = source_x;
    } else if (transform_code == 6) {
        x = source_y;
        y = -source_x;
    } else if (transform_code == 7) {
        x = -source_y;
        y = -source_x;
    }
    *target_x = x;
    *target_y = y;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __forceinline__ int d_uc5_mirror_source_anchor(
    int symmetry,
    int target_anchor,
    int* transform_code) {
    if (!transform_code) {
        return -1;
    }
    if ((symmetry == kPupilSymmetryMirrorX ||
         symmetry == kPupilSymmetrySignXY ||
         symmetry == kPupilSymmetryDihedral4) &&
        target_anchor == 1) {
        *transform_code = 1;
        return 0;
    }
    if ((symmetry == kPupilSymmetryMirrorY ||
         symmetry == kPupilSymmetrySignXY) &&
        target_anchor == 3) {
        *transform_code = 2;
        return 2;
    }
    if (symmetry == kPupilSymmetryDihedral4 && target_anchor == 2) {
        *transform_code = 4;
        return 0;
    }
    if (symmetry == kPupilSymmetryDihedral4 && target_anchor == 3) {
        *transform_code = 6;
        return 0;
    }
    return -1;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ __forceinline__ DeviceCalibratedEdgeAnchor
d_uc5_transform_anchor(
    const DeviceCalibratedEdgeAnchor& source,
    int transform_code) {
    DeviceCalibratedEdgeAnchor target = source;
    d_uc5_transform_xy(
        transform_code, source.stop_x, source.stop_y, &target.stop_x, &target.stop_y);
    d_uc5_transform_xy(
        transform_code, source.px, source.py, &target.px, &target.py);
    d_uc5_transform_xy(
        transform_code,
        source.seed.x,
        source.seed.y,
        &target.seed.x,
        &target.seed.y);
    d_uc5_transform_xy(
        transform_code,
        source.correction_x,
        source.correction_y,
        &target.correction_x,
        &target.correction_y);
    return target;
}
} // namespace
} // namespace optiland
