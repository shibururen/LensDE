#pragma once
#include "../prelude.cuh"

namespace optiland {
namespace {
constexpr int kKindStandard = 0;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kKindPlane = 1;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kKindEvenAsphere = 2;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kKindImage = 3;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kKindObject = 4;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr double kDeviceInf = 1.0e100;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr double kDeviceEps = 1.0e-12;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kMaxDeviceFields = 16;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kMaxDeviceWaves = 8;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kMaxDeviceGlassProximity = 64;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kMaxDeviceGlassProximityPoints = 4096;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kMaxDeviceSpotSamples = 128;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kPupilSamplingGaussianRingArm = 0;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kPupilSamplingCodevRectangular = 1;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kPupilSymmetryNone = 0;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kPupilSymmetryMirrorX = 1;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kPupilSymmetryMirrorY = 2;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kPupilSymmetrySignXY = 3;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kPupilSymmetryDihedral4 = 4;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kSpotTailExtraEdgeCount = 8;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kAsphereDeepRoleChief = 0;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kAsphereDeepRoleEdge = 1;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kAsphereDeepRoleAuxiliary = 2;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kAsphereDeepSiteOther = 0;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kAsphereDeepSiteFarParaxial = 1;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kAsphereDeepSiteExactFirstSurfaceSeed = 2;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
constexpr int kAsphereDeepSiteTransverseChiefShift = 3;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
using de_cuda_detail::DeviceSurface;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
using de_cuda_detail::DeviceAsphereRootProfile;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
using de_cuda_detail::DeviceAspherePrefixDiagnostic;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
using de_cuda_detail::kMaxDeviceAsphereCoefficients;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
using de_cuda_detail::kMaxAsphereRootProfileSurfaces;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
using de_cuda_detail::kMaxDeviceMaterialIndices;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
// Diagnostic-only generated copy. No DeviceConfig ABI or optical decisions change.
__device__ int g_edge_path_mode = 0;
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ int g_edge_path_shadow = 0;
} // namespace
} // namespace optiland
