#pragma once
#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

namespace raytrace_detail {
inline ScalarInterval outward_interval(double lo, double hi) {
    if (std::isnan(lo) || std::isnan(hi)) {
        return {};
    }
    if (lo > hi) {
        std::swap(lo, hi);
    }
    if (std::isfinite(lo)) {
        lo = std::nextafter(lo, -std::numeric_limits<double>::infinity());
    }
    if (std::isfinite(hi)) {
        hi = std::nextafter(hi, std::numeric_limits<double>::infinity());
    }
    return {lo, hi, true};
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline ScalarInterval point_interval(double value) {
    return outward_interval(value, value);
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline ScalarInterval whole_interval() {
    return {
        -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        true,
    };
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline ScalarInterval interval_add(const ScalarInterval& a, const ScalarInterval& b) {
    if (!a.valid || !b.valid) {
        return {};
    }
    return outward_interval(a.lo + b.lo, a.hi + b.hi);
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline ScalarInterval interval_subtract(const ScalarInterval& a, const ScalarInterval& b) {
    if (!a.valid || !b.valid) {
        return {};
    }
    return outward_interval(a.lo - b.hi, a.hi - b.lo);
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline ScalarInterval interval_multiply(const ScalarInterval& a, const ScalarInterval& b) {
    if (!a.valid || !b.valid) {
        return {};
    }
    if (!std::isfinite(a.lo) || !std::isfinite(a.hi) ||
        !std::isfinite(b.lo) || !std::isfinite(b.hi)) {
        if ((a.lo == 0.0 && a.hi == 0.0) || (b.lo == 0.0 && b.hi == 0.0)) {
            return point_interval(0.0);
        }
        return whole_interval();
    }
    const double products[4] = {
        a.lo * b.lo,
        a.lo * b.hi,
        a.hi * b.lo,
        a.hi * b.hi,
    };
    return outward_interval(
        *std::min_element(std::begin(products), std::end(products)),
        *std::max_element(std::begin(products), std::end(products)));
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline ScalarInterval interval_scale(const ScalarInterval& value, double scale) {
    if (!value.valid || std::isnan(scale)) {
        return {};
    }
    if (!std::isfinite(scale)) {
        return whole_interval();
    }
    return scale >= 0.0
               ? outward_interval(value.lo * scale, value.hi * scale)
               : outward_interval(value.hi * scale, value.lo * scale);
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline ScalarInterval interval_divide(const ScalarInterval& numerator, const ScalarInterval& denominator) {
    if (!numerator.valid || !denominator.valid) {
        return {};
    }
    if (denominator.lo <= 0.0 && denominator.hi >= 0.0) {
        return whole_interval();
    }
    return interval_multiply(
        numerator,
        outward_interval(1.0 / denominator.hi, 1.0 / denominator.lo));
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline ScalarInterval interval_square(const ScalarInterval& value) {
    if (!value.valid) {
        return {};
    }
    if (!std::isfinite(value.lo) || !std::isfinite(value.hi)) {
        return outward_interval(0.0, std::numeric_limits<double>::infinity());
    }
    if (value.lo <= 0.0 && value.hi >= 0.0) {
        return outward_interval(0.0, std::max(value.lo * value.lo, value.hi * value.hi));
    }
    return outward_interval(
        std::min(value.lo * value.lo, value.hi * value.hi),
        std::max(value.lo * value.lo, value.hi * value.hi));
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline ScalarInterval interval_sqrt(const ScalarInterval& value) {
    if (!value.valid || value.hi < 0.0) {
        return {};
    }
    return outward_interval(
        std::sqrt(std::max(0.0, value.lo)),
        std::sqrt(std::max(0.0, value.hi)));
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline ScalarInterval affine_interval(double origin, double direction, double lo, double hi) {
    return outward_interval(origin + direction * lo, origin + direction * hi);
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline ScalarInterval even_asphere_sag_interval(
    const Surface& surface,
    const ScalarInterval& r2) {
    if (!r2.valid) {
        return {};
    }
    ScalarInterval sag = point_interval(0.0);
    if (std::isfinite(surface.radius) && std::abs(surface.radius) >= kEps) {
        const double conic_scale =
            -(1.0 + surface.conic) / (surface.radius * surface.radius);
        const ScalarInterval root_arg = interval_add(
            point_interval(1.0),
            interval_scale(r2, conic_scale));
        const ScalarInterval root = interval_sqrt(root_arg);
        if (!root.valid) {
            return {};
        }
        const ScalarInterval denominator = interval_scale(
            interval_add(point_interval(1.0), root),
            surface.radius);
        sag = interval_divide(r2, denominator);
        if (!sag.valid) {
            return {};
        }
    }
    if (!surface.asphere_coefficients.empty()) {
        ScalarInterval polynomial = point_interval(surface.asphere_coefficients.back());
        for (std::size_t i = surface.asphere_coefficients.size() - 1; i > 0; --i) {
            polynomial = interval_add(
                point_interval(surface.asphere_coefficients[i - 1]),
                interval_multiply(r2, polynomial));
        }
        sag = interval_add(sag, interval_multiply(r2, polynomial));
    }
    return sag;
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline ScalarInterval even_asphere_residual_interval(
    const Surface& surface,
    const Ray& ray,
    double lo,
    double hi) {
    const ScalarInterval x = affine_interval(ray.p.x, ray.d.x, lo, hi);
    const ScalarInterval y = affine_interval(ray.p.y, ray.d.y, lo, hi);
    const ScalarInterval z = affine_interval(ray.p.z, ray.d.z, lo, hi);
    const ScalarInterval r2 = interval_add(interval_square(x), interval_square(y));
    return interval_subtract(even_asphere_sag_interval(surface, r2), z);
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline ScalarInterval even_asphere_derivative_interval(
    const Surface& surface,
    const Ray& ray,
    double lo,
    double hi) {
    const ScalarInterval x = affine_interval(ray.p.x, ray.d.x, lo, hi);
    const ScalarInterval y = affine_interval(ray.p.y, ray.d.y, lo, hi);
    const ScalarInterval r2 = interval_add(interval_square(x), interval_square(y));
    if (!r2.valid) {
        return {};
    }

    ScalarInterval sag_q = point_interval(0.0);
    if (std::isfinite(surface.radius) && std::abs(surface.radius) >= kEps) {
        const double conic_scale =
            -(1.0 + surface.conic) / (surface.radius * surface.radius);
        const ScalarInterval root_arg = interval_add(
            point_interval(1.0),
            interval_scale(r2, conic_scale));
        const ScalarInterval root = interval_sqrt(root_arg);
        if (!root.valid || root.lo <= 0.0) {
            return whole_interval();
        }
        sag_q = interval_divide(
            point_interval(1.0),
            interval_scale(root, 2.0 * surface.radius));
    }

    if (!surface.asphere_coefficients.empty()) {
        const std::size_t last = surface.asphere_coefficients.size() - 1;
        ScalarInterval radial_derivative = point_interval(
            static_cast<double>(last + 1) * surface.asphere_coefficients[last]);
        for (std::size_t i = last; i > 0; --i) {
            radial_derivative = interval_add(
                point_interval(static_cast<double>(i) * surface.asphere_coefficients[i - 1]),
                interval_multiply(r2, radial_derivative));
        }
        sag_q = interval_add(sag_q, radial_derivative);
    }
    const ScalarInterval q_prime = interval_scale(
        interval_add(
            interval_scale(x, ray.d.x),
            interval_scale(y, ray.d.y)),
        2.0);
    return interval_subtract(
        interval_multiply(sag_q, q_prime),
        point_interval(ray.d.z));
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline bool even_asphere_residual_at(
    const Surface& surface,
    const Ray& ray,
    double tv,
    double* residual) {
    if (!residual || !std::isfinite(tv)) {
        return false;
    }
    const double x = ray.p.x + tv * ray.d.x;
    const double y = ray.p.y + tv * ray.d.y;
    const double z = ray.p.z + tv * ray.d.z;
    const double sag = surface_sag(surface, x, y);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        !std::isfinite(sag)) {
        return false;
    }
    *residual = sag - z;
    return std::isfinite(*residual);
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline bool even_asphere_derivative_at(
    const Surface& surface,
    const Ray& ray,
    double tv,
    double* derivative) {
    if (!derivative || !std::isfinite(tv)) {
        return false;
    }
    const double x = ray.p.x + tv * ray.d.x;
    const double y = ray.p.y + tv * ray.d.y;
    const double z = ray.p.z + tv * ray.d.z;
    const Vec3 normal = surface_normal(surface, {x, y, z});
    if (!std::isfinite(normal.x) || !std::isfinite(normal.y) ||
        !std::isfinite(normal.z) || std::abs(normal.z) <= kEps) {
        return false;
    }
    const double dzdx = -normal.x / normal.z;
    const double dzdy = -normal.y / normal.z;
    *derivative = dzdx * ray.d.x + dzdy * ray.d.y - ray.d.z;
    return std::isfinite(*derivative);
}
} // namespace raytrace_detail

} // namespace optiland
