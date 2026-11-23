#pragma once
#include "../prelude.cuh"
#include "../contracts/calibrated_model.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "../geometry/asphere_slope.cuh"

namespace optiland {
namespace {
// Experimental predictor only: it never marks a seed or optical model accepted.
// Callers must perform the original real STOP solve and first-surface check.
__device__ DeviceSeed d_vignetted_past_field_edge_seed(
    const DeviceConfig& cfg, const DeviceField& field,
    const DeviceCalibratedModel& model, int edge, double rs, double limit,
    const DeviceCalibratedModel* p1, const DeviceField* f1,
    const DeviceCalibratedModel* p2, const DeviceField* f2,
    double* initial_scale) {
    DeviceSeed seed;
    if (!initial_scale || !p1 || !f1 || !p2 || !f2 ||
        !p1->valid || !p2->valid || !p1->chief_seed.valid ||
        !p2->chief_seed.valid || !model.chief_seed.valid ||
        (edge != 0 && edge != 2 && edge != 3) ||
        !d_finite(rs) || rs <= 1.0e-8 || !d_finite(limit) || limit < 0.0 ||
        field.hx != 0.0 || f1->hx != 0.0 || f2->hx != 0.0 ||
        field.fvcx != 0.0 || f1->fvcx != 0.0 || f2->fvcx != 0.0 ||
        model.chief_seed.x != 0.0 || p1->chief_seed.x != 0.0 ||
        p2->chief_seed.x != 0.0 || f2->hy < 0.0 ||
        !(field.hy > f1->hy && f1->hy > f2->hy) ||
        !(field.fvdx >= 0.0 && field.fvdx < 1.0 &&
          field.fvdy >= 0.0 && field.fvdy < 1.0) ||
        (field.fvcy == 0.0 && field.fvdx == 0.0 && field.fvdy == 0.0) ||
        !d_finite(model.chief_seed.y) || !d_finite(model.chief_stop_y)) return seed;

    const double theta = cfg.max_field_deg * 3.14159265358979323846 / 180.0;
    if (!d_finite(theta) || theta <= 0.0 || theta * field.hy >= 1.4) return seed;
    const double q = tan(theta * field.hy);
    const double q1 = tan(theta * f1->hy), q2 = tan(theta * f2->hy);
    if (!(q1 - q2 > 1.0e-9)) return seed;
    const double ay = (q - q1) / (q1 - q2);
    if (!(ay >= 0.0 && ay <= 1.5)) return seed;

    const auto& a1 = p1->edge_anchors[edge];
    const auto& a2 = p2->edge_anchors[edge];
    const double px = edge == 0 ? 1.0 : 0.0;
    const double py = edge == 2 ? 1.0 : edge == 3 ? -1.0 : 0.0;
    const double tx = px * (1.0 - field.fvdx) * rs;
    const double ty = (field.fvcy + py * (1.0 - field.fvdy)) * rs;
    const DeviceCalibratedModel* past[2] = {p1, p2};
    const DeviceField* fields[2] = {f1, f2};
    // Finite measured STOP positions exclude legacy geometric completions.
    for (int j = 0; j < 2; ++j) {
        const auto& a = past[j]->edge_anchors[edge];
        const auto& f = *fields[j];
        const double source_residual = hypot(a.stop_x - px * (1.0 - f.fvdx) * rs,
            a.stop_y - (f.fvcy + py * (1.0 - f.fvdy)) * rs);
        if (!a.valid || !a.seed.valid || a.px != px || a.py != py ||
            !d_finite(a.seed.x) || !d_finite(a.seed.y) ||
            !d_finite(a.stop_x) || !d_finite(a.stop_y) ||
            !d_finite(past[j]->chief_seed.y) ||
            !d_finite(past[j]->chief_stop_y) ||
            !d_finite(source_residual) || source_residual > limit)
            return seed;
    }
    if (edge == 0) {
        if (!(a1.stop_x > 1.0e-8 && a2.stop_x > 1.0e-8 && tx > 1.0e-8)) return seed;
        const double ax = (q*q - q1*q1) / (q1*q1 - q2*q2);
        if (!(ax >= 0.0 && ax <= 3.5)) return seed;
        const double s1 = hypot(a1.seed.x, a1.seed.y-p1->chief_seed.y) / a1.stop_x;
        const double s2 = hypot(a2.seed.x, a2.seed.y-p2->chief_seed.y) / a2.stop_x;
        const double kx1 = a1.seed.x / a1.stop_x, kx2 = a2.seed.x / a2.stop_x;
        // Separate the measured chief STOP error from the even-X pupil bend.
        const double ky1 = (a1.seed.y-p1->chief_seed.y +
            s1*(p1->chief_stop_y-f1->fvcy*rs)) / (a1.stop_x*a1.stop_x);
        const double ky2 = (a2.seed.y-p2->chief_seed.y +
            s2*(p2->chief_stop_y-f2->fvcy*rs)) / (a2.stop_x*a2.stop_x);
        seed.x = (kx1 + ax*(kx1-kx2)) * tx;
        seed.y = model.chief_seed.y + (ky1 + ay*(ky1-ky2))*tx*tx -
            s1*(model.chief_stop_y-ty);
        const double carry_x = kx1*tx;
        const double carry_y = model.chief_seed.y + ky1*tx*tx;
        if (!(carry_x > 1.0e-8 && seed.x > 0.0) ||
            hypot(seed.x-carry_x,seed.y-carry_y) > 0.35*carry_x) return {};
        *initial_scale = hypot(seed.x,seed.y-model.chief_seed.y) / fabs(tx);
        if (!(*initial_scale >= 0.2 && *initial_scale <= 5.0)) return {};
    } else {
        const double d1 = a1.stop_y-p1->chief_stop_y;
        const double d2 = a2.stop_y-p2->chief_stop_y;
        if (a1.seed.x != 0.0 || a2.seed.x != 0.0 ||
            fabs(d1) < 1.0e-8 || fabs(d2) < 1.0e-8) return seed;
        const double k1 = (a1.seed.y-p1->chief_seed.y)/d1;
        const double k2 = (a2.seed.y-p2->chief_seed.y)/d2;
        if (!(k1 > 0.05 && k1 < 10.0 && k2 > 0.05 && k2 < 10.0)) return seed;
        const double carry_y = model.chief_seed.y + k1*(ty-model.chief_stop_y);
        seed.x = 0.0;
        seed.y = a1.seed.y + ay*(a1.seed.y-a2.seed.y) +
            k1*(ty-(a1.stop_y+ay*(a1.stop_y-a2.stop_y)));
        const double half_width = fabs(py*(1.0-field.fvdy)*rs);
        if (half_width <= 1.0e-8 ||
            fabs(seed.y-carry_y) > 0.35*fmax(fabs(carry_y-model.chief_seed.y),1.0e-8)) return {};
        *initial_scale = fabs(seed.y-model.chief_seed.y)/half_width;
        if (!(*initial_scale > 0.05 && *initial_scale < 10.0)) return {};
    }
    seed.valid = d_finite(seed.x) && d_finite(seed.y) ? 1 : 0;
    return seed;
}

} // namespace
} // namespace optiland
