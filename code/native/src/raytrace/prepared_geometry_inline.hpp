#pragma once
#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

namespace raytrace_detail {
inline bool aperture_clip_exceeded(double radial, double clip_radius) {
    if (!(clip_radius > 0.0)) {
        return false;
    }
    // Allow a small engineering margin before declaring clip.
    const double tolerance = 0.01 * clip_radius;
    return radial > clip_radius + tolerance;
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline double aperture_clip_limit_sq(double clip_radius) {
    if (!(clip_radius > 0.0) || !std::isfinite(clip_radius)) {
        return clip_radius;
    }
    // Same 1% margin as aperture_clip_exceeded(), without the hot-path sqrt.
    const double limit = 1.01 * clip_radius;
    return limit * limit;
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline bool stop_surface_forward_root_interval(
    const Surface& surface,
    const Ray& ray,
    double* lower,
    double* upper) {
    if (!lower || !upper || !surface.is_stop ||
        !(surface.aperture_radius > 0.0) ||
        !std::isfinite(surface.aperture_radius)) {
        return false;
    }

    // Root selection and the subsequent aperture check must use one physical
    // boundary.  Retain the existing 1% engineering tolerance so a root is not
    // accepted by one stage and rejected by the other because of round-off.
    const double radius = 1.01 * surface.aperture_radius;
    const double a = ray.d.x * ray.d.x + ray.d.y * ray.d.y;
    const double b = 2.0 * (ray.p.x * ray.d.x + ray.p.y * ray.d.y);
    const double c = ray.p.x * ray.p.x + ray.p.y * ray.p.y - radius * radius;
    const double radial_scale = std::max(
        1.0,
        std::max(radius * radius, ray.p.x * ray.p.x + ray.p.y * ray.p.y));
    const double radial_tol =
        128.0 * std::numeric_limits<double>::epsilon() * radial_scale;

    if (a <= kEps * kEps) {
        if (c > radial_tol) {
            return false;
        }
        *lower = 0.0;
        *upper = std::numeric_limits<double>::infinity();
        return true;
    }

    double discriminant = b * b - 4.0 * a * c;
    const double discriminant_scale =
        std::max(1.0, std::max(b * b, std::abs(4.0 * a * c)));
    const double discriminant_tol =
        128.0 * std::numeric_limits<double>::epsilon() * discriminant_scale;
    if (!std::isfinite(discriminant) || discriminant < -discriminant_tol) {
        return false;
    }
    discriminant = std::max(0.0, discriminant);
    const double root = std::sqrt(discriminant);
    const double q = -0.5 * (b + std::copysign(root, b));
    const double q_tol =
        128.0 * std::numeric_limits<double>::epsilon() *
        std::max(1.0, std::abs(b) + root);
    double domain_lo = 0.0;
    double domain_hi = 0.0;
    if (std::abs(q) <= q_tol) {
        domain_lo = domain_hi = -b / (2.0 * a);
    } else {
        domain_lo = q / a;
        domain_hi = c / q;
        if (domain_hi < domain_lo) {
            std::swap(domain_lo, domain_hi);
        }
    }
    if (domain_hi < -1.0e-10) {
        return false;
    }
    *lower = std::max(0.0, domain_lo);
    *upper = std::max(*lower, domain_hi);
    return std::isfinite(*lower) && std::isfinite(*upper);
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline bool aperture_clip_exceeded_sq(double radial_sq, double clip_limit_sq) {
    if (!(clip_limit_sq > 0.0)) {
        return false;
    }
    return radial_sq > clip_limit_sq;
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline bool stable_standard_sphere_intersect(
    double curvature,
    double px,
    double py,
    double pz,
    double dx,
    double dy,
    double dz,
    double* t) {
    if (!t || !std::isfinite(curvature) ||
        !std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz) ||
        !std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz)) {
        return false;
    }

    // Curvature-normalized implicit sphere equation:
    //
    //   curvature * (x^2 + y^2 + z^2) - 2 z = 0.
    //
    // Unlike the radius-centered expansion, its coefficients remain local
    // as curvature approaches zero.  The stable q-formula then recovers the
    // near root without subtracting two radius-sized values.
    const double direction_norm2 = dx * dx + dy * dy + dz * dz;
    const double position_dot_direction = px * dx + py * dy + pz * dz;
    const double position_norm2 = px * px + py * py + pz * pz;
    const double a = curvature * direction_norm2;
    const double b = 2.0 * curvature * position_dot_direction - 2.0 * dz;
    const double c = curvature * position_norm2 - 2.0 * pz;
    if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c)) {
        return false;
    }

    constexpr double kMachineEps = std::numeric_limits<double>::epsilon();
    constexpr double kRootTEps = 1.0e-10;
    const double coefficient_scale =
        std::max(1.0, std::max(std::abs(b), std::abs(c)));
    const double linear_tolerance = 64.0 * kMachineEps * coefficient_scale;
    double roots[2] = {
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::quiet_NaN(),
    };
    int root_count = 0;
    if (std::abs(a) <= linear_tolerance) {
        if (std::abs(b) <= linear_tolerance) {
            return false;
        }
        roots[root_count++] = -c / b;
    } else {
        const double four_ac = 4.0 * a * c;
        double discriminant = b * b - four_ac;
        const double discriminant_scale =
            std::max(1.0, b * b + std::abs(four_ac));
        const double discriminant_tolerance =
            64.0 * kMachineEps * discriminant_scale;
        if (!std::isfinite(discriminant) ||
            discriminant < -discriminant_tolerance) {
            return false;
        }
        discriminant = std::max(0.0, discriminant);
        const double root = std::sqrt(discriminant);
        const double q = -0.5 * (b + std::copysign(root, b));
        const double q_tolerance =
            64.0 * kMachineEps * std::max(1.0, std::abs(b) + root);
        if (std::abs(q) <= q_tolerance) {
            roots[root_count++] = -b / (2.0 * a);
        } else {
            roots[root_count++] = q / a;
            roots[root_count++] = c / q;
        }
    }

    double best_t = std::numeric_limits<double>::quiet_NaN();
    for (int i = 0; i < root_count; ++i) {
        const double candidate = roots[i];
        if (!std::isfinite(candidate) || candidate < -kRootTEps) {
            continue;
        }
        const double zi = pz + candidate * dz;
        if (!std::isfinite(zi)) {
            continue;
        }
        // Select the vertex-connected sag branch.  Written in curvature
        // form, this remains well-conditioned for both signs of radius.
        const double curvature_z = curvature * zi;
        const double branch = curvature_z - 1.0;
        const double branch_tolerance =
            1.0e-10 * std::max(1.0, std::abs(curvature_z));
        if (branch > branch_tolerance) {
            continue;
        }
        const double candidate_score = std::max(0.0, candidate);
        const double best_score = std::isfinite(best_t)
                                      ? std::max(0.0, best_t)
                                      : std::numeric_limits<double>::infinity();
        if (!std::isfinite(best_t) || candidate_score < best_score - kRootTEps) {
            best_t = candidate;
        }
    }
    if (!std::isfinite(best_t)) {
        return false;
    }
    *t = std::abs(best_t) <= kRootTEps ? 0.0 : best_t;
    return std::isfinite(*t);
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline bool fast_standard_sphere_or_plane_intersect(
    const Surface& surface,
    double px,
    double py,
    double pz,
    double dx,
    double dy,
    double dz,
    double* t) {
    if (!t) {
        return false;
    }
    if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz) ||
        !std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz)) {
        return false;
    }
    if (surface.is_plane()) {
        if (std::abs(dz) <= kEps) {
            return false;
        }
        *t = -pz / dz;
        return std::isfinite(*t);
    }
    if (surface.kind != SurfaceKind::Standard ||
        std::abs(surface.conic) > 1.0e-14 ||
        !std::isfinite(surface.radius) ||
        std::abs(surface.radius) < kEps) {
        Ray local_ray;
        local_ray.p = {px, py, pz};
        local_ray.d = {dx, dy, dz};
        return intersect_surface(surface, local_ray, t);
    }
    return stable_standard_sphere_intersect(
        1.0 / surface.radius, px, py, pz, dx, dy, dz, t);
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline bool prepared_geometry_intersect(
    unsigned char geometry,
    double radius,
    double inv_radius,
    double px,
    double py,
    double pz,
    double dx,
    double dy,
    double dz,
    double* t) {
    if (!t) {
        return false;
    }
    if (geometry == kPreparedGeometryPlane) {
        if (std::abs(dz) <= kEps) {
            return false;
        }
        *t = -pz / dz;
        return std::isfinite(*t);
    }
    if (geometry != kPreparedGeometrySphere) {
        return false;
    }

    (void)radius;
    return stable_standard_sphere_intersect(
        inv_radius, px, py, pz, dx, dy, dz, t);
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline bool fast_standard_sphere_or_plane_intersect(const Surface& surface, const Ray& local_ray, double* t) {
    return fast_standard_sphere_or_plane_intersect(
        surface,
        local_ray.p.x,
        local_ray.p.y,
        local_ray.p.z,
        local_ray.d.x,
        local_ray.d.y,
        local_ray.d.z,
        t);
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline Vec3 prepared_geometry_normal(
    unsigned char geometry,
    double radius,
    double inv_radius,
    double px,
    double py,
    double pz) {
    if (geometry == kPreparedGeometryPlane) {
        return {0.0, 0.0, 1.0};
    }
    return {
        px * inv_radius,
        py * inv_radius,
        (pz - radius) * inv_radius,
    };
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline bool refract_assume_unit(Vec3 incident, Vec3 normal, double n1, double n2, Vec3* transmitted) {
    if (!transmitted || std::abs(n2) <= kEps) {
        return false;
    }
    double d = dot(incident, normal);
    const double sgn = (d >= 0.0) ? 1.0 : -1.0;
    normal = normal * sgn;
    d = std::abs(d);
    const double u = n1 / n2;
    const double root_arg = 1.0 - u * u * (1.0 - d * d);
    if (root_arg < 0.0) {
        return false;
    }
    const double root = std::sqrt(root_arg);
    *transmitted = {
        u * incident.x + normal.x * root - u * normal.x * d,
        u * incident.y + normal.y * root - u * normal.y * d,
        u * incident.z + normal.z * root - u * normal.z * d,
    };
    return true;
}
} // namespace raytrace_detail

} // namespace optiland
