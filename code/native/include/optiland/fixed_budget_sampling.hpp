#pragma once
#include <cmath>
#include <string>

// Shared CPU/CUDA fixed-budget quadrature policy. This changes numerical
// integration nodes, never the physical pupil, STOP targets, or ray weights.
#ifdef __CUDACC__
#define OPTILAND_PUPIL_HD __host__ __device__
#else
#define OPTILAND_PUPIL_HD
#endif

namespace optiland {
inline bool is_fixed_budget_pupil_distribution(const std::string& value) {
    constexpr char expected[] = "gaussian_ring_arm_fixed_budget";
    if (value.size() != sizeof(expected)-1) return false;
    for (std::size_t i=0;i<value.size();++i) {
        char ch=value[i];
        if (ch=='-' || ch==' ') ch='_';
        else if (ch>='A' && ch<='Z') ch=static_cast<char>(ch-'A'+'a');
        if (ch!=expected[i]) return false;
    }
    return true;
}
// 0: unchanged; 1: 12x4 (axial circular); 2: 8x6 (weakly vignetted Y field).
// Explicit opt-in only, originally 6x8, and elevated-field overrides win.
// Near-axis support is intentionally conservative and limited to the tested
// angular domain. Do not remove internal meridional fold-check samples.
OPTILAND_PUPIL_HD inline int fixed_budget_pupil_shape(
    bool enabled, int rings, int arms, bool elevated,
    double max_field_deg, double hx, double hy,
    double cx, double cy, double dx, double dy,
    bool meridional_order_active) {
    if (!enabled || rings != 6 || arms != 8 || elevated) return 0;
    if (hx == 0.0 && hy == 0.0 && cx == 0.0 && cy == 0.0 &&
        dx == dy && dx < 1.0 && ::fabs(dx) < 1.0e20) return 1;
    if (!meridional_order_active && hx == 0.0 && cx == 0.0 &&
        ::fabs(hy) > 0.0 && ::fabs(hy) <= 0.3 &&
        ::fabs(max_field_deg * hy) <= 12.0 &&
        ::fabs(cy) <= 0.1 && ::fabs(dx) <= 0.1 && ::fabs(dy) <= 0.1)
        return 2;
    return 0;
}

// Gauss-Legendre in radial area u=r^2. The radial measure integrates to 1/2.
// Precomputed nodes avoid per-ray Newton root solving for the new shapes.
OPTILAND_PUPIL_HD inline bool fixed_budget_gaussian_node_weight(
    int rings, int i, double* radius, double* weight) {
    if (rings == 8 && i >= 0 && i < 8) {
        const double radii[8] = {0.14090802585811751, 0.31885225621467167, 0.48706652014056101, 0.63897001396949382, 0.76923164342597405, 0.87336487504259319, 0.94780443062206321, 0.99002269077469540};
        const double weights[8] = {0.025307134072594172, 0.055595258613343586, 0.078426661469471762, 0.090670945844590442, 0.090670945844590442, 0.078426661469471762, 0.055595258613343586, 0.025307134072594172};
        *radius = radii[i]; *weight = weights[i]; return true;
    }
    if (rings == 12 && i >= 0 && i < 12) {
        const double radii[12] = {0.096019179733219856, 0.21895518220577151, 0.33918824110344342, 0.45424775492751890, 0.56221370536559312, 0.66134960175709301, 0.75007779880205383, 0.82699198877564106, 0.89087539933669102, 0.94071852171473291, 0.97573491696527770, 0.99537948397752285};
        const double weights[12] = {0.011793834096628006, 0.026734831498829721, 0.040019582135836528, 0.050791856680766412, 0.058373134134588660, 0.062286761453350672, 0.062286761453350672, 0.058373134134588660, 0.050791856680766412, 0.040019582135836528, 0.026734831498829721, 0.011793834096628006};
        *radius = radii[i]; *weight = weights[i]; return true;
    }
    return false;
}
} // namespace optiland
#undef OPTILAND_PUPIL_HD
