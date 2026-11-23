// Evaluate physical surface sag, valid profile radii, and surface normals.
// Intersection, tracing, aperture analysis, and layout code depend on the same shape conventions.
// Keep conic/asphere domain checks and coefficient accumulation order consistent with root solving.

#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

double surface_sag(const Surface& surface, double x, double y) {
    if (surface.is_plane()) {
        return 0.0;
    }
    const double r2 = x * x + y * y;
    const double radius = surface.radius;
    double z = 0.0;
    if (std::isfinite(radius) && std::abs(radius) >= kEps) {
        const double root_arg = 1.0 - (1.0 + surface.conic) * r2 / (radius * radius);
        if (root_arg < 0.0) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        const double root = std::sqrt(root_arg);
        const double denom = radius * (1.0 + root);
        if (std::abs(denom) <= kEps) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        z = r2 / denom;
    }
    if (surface.kind == SurfaceKind::EvenAsphere) {
        double power = r2;
        for (double coefficient : surface.asphere_coefficients) {
            z += coefficient * power;
            power *= r2;
        }
    }
    return z;
}

double surface_profile_radius_limit(const Surface& surface) {
    if (surface.is_plane()) {
        return std::numeric_limits<double>::infinity();
    }
    if (!std::isfinite(surface.radius) || std::abs(surface.radius) < kEps) {
        if (surface.kind == SurfaceKind::EvenAsphere) {
            return std::numeric_limits<double>::infinity();
        }
        return 0.0;
    }
    const double conic_term = 1.0 + surface.conic;
    if (!(conic_term > 0.0)) {
        return std::numeric_limits<double>::infinity();
    }
    return std::abs(surface.radius) / std::sqrt(conic_term);
}

double profile_surface_sag(const Surface& surface, double y, double profile_radius) {
    const double clear_radius = std::max(0.0, profile_radius);
    const double clamped_y = std::clamp(y, -clear_radius, clear_radius);
    const double physical_limit = surface_profile_radius_limit(surface);
    const double effective_radius = std::min(clamped_y < 0.0 ? -clamped_y : clamped_y, physical_limit);
    if (effective_radius <= 0.0) {
        return 0.0;
    }
    return surface_sag(surface, 0.0, effective_radius);
}

Vec3 surface_normal(const Surface& surface, const Vec3& local_point) {
    if (surface.is_plane()) {
        return {0.0, 0.0, 1.0};
    }

    const double r2 = local_point.x * local_point.x + local_point.y * local_point.y;
    double dfdx = 0.0;
    double dfdy = 0.0;
    if (std::isfinite(surface.radius) && std::abs(surface.radius) >= kEps) {
        const double root_arg = 1.0 - (1.0 + surface.conic) * r2 / (surface.radius * surface.radius);
        if (root_arg <= 0.0) {
            return {0.0, 0.0, 1.0};
        }

        const double denom = surface.radius * std::sqrt(root_arg);
        if (std::abs(denom) <= kEps) {
            return {0.0, 0.0, 1.0};
        }

        dfdx = local_point.x / denom;
        dfdy = local_point.y / denom;
    }

    if (surface.kind == SurfaceKind::EvenAsphere) {
        double power = 1.0;
        for (std::size_t i = 0; i < surface.asphere_coefficients.size(); ++i) {
            const double c = surface.asphere_coefficients[i];
            const double order = 2.0 * static_cast<double>(i + 1);
            dfdx += order * local_point.x * c * power;
            dfdy += order * local_point.y * c * power;
            power *= r2;
        }
    }
    return normalize({dfdx, dfdy, -1.0});
}

} // namespace optiland
