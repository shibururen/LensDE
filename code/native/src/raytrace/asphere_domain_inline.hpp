#pragma once
#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

namespace raytrace_detail {
inline double production_asphere_radial_slope(
    const Surface& surface,
    double radius) {
    double radial_slope = 0.0;
    if (std::isfinite(surface.radius) && std::abs(surface.radius) >= kEps &&
        std::abs(surface.radius) < 0.5 * kInf) {
        const double root_arg =
            1.0 - (1.0 + surface.conic) * radius * radius /
                      (surface.radius * surface.radius);
        if (!(root_arg > 0.0)) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        radial_slope += radius / (surface.radius * std::sqrt(root_arg));
    }
    double radial_power = radius;
    for (std::size_t i = 0; i < surface.asphere_coefficients.size(); ++i) {
        radial_slope +=
            2.0 * static_cast<double>(i + 1) *
            surface.asphere_coefficients[i] * radial_power;
        radial_power *= radius * radius;
    }
    return radial_slope;
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline bool production_asphere_radial_slope_interval(
    const Surface& surface,
    double radius_lo,
    double radius_hi,
    double* slope_lo,
    double* slope_hi) {
    if (!slope_lo || !slope_hi || !std::isfinite(radius_lo) ||
        !std::isfinite(radius_hi) || radius_lo < 0.0 ||
        radius_hi < radius_lo) {
        return false;
    }
    double conic_lo = 0.0;
    double conic_hi = 0.0;
    if (std::isfinite(surface.radius) && std::abs(surface.radius) >= kEps &&
        std::abs(surface.radius) < 0.5 * kInf) {
        const double radius_squared = surface.radius * surface.radius;
        const double root_arg_lo =
            1.0 - (1.0 + surface.conic) * radius_lo * radius_lo /
                      radius_squared;
        const double root_arg_hi =
            1.0 - (1.0 + surface.conic) * radius_hi * radius_hi /
                      radius_squared;
        if (!(root_arg_lo > 0.0) || !(root_arg_hi > 0.0) ||
            !std::isfinite(root_arg_lo) || !std::isfinite(root_arg_hi)) {
            return false;
        }
        const double endpoint_lo =
            radius_lo / (surface.radius * std::sqrt(root_arg_lo));
        const double endpoint_hi =
            radius_hi / (surface.radius * std::sqrt(root_arg_hi));
        if (!std::isfinite(endpoint_lo) || !std::isfinite(endpoint_hi)) {
            return false;
        }
        conic_lo = std::nextafter(
            std::min(endpoint_lo, endpoint_hi),
            -std::numeric_limits<double>::max());
        conic_hi = std::nextafter(
            std::max(endpoint_lo, endpoint_hi),
            std::numeric_limits<double>::max());
    }
    const double u_lo = std::nextafter(
        radius_lo * radius_lo, -std::numeric_limits<double>::max());
    const double u_hi = std::nextafter(
        radius_hi * radius_hi, std::numeric_limits<double>::max());
    if (!std::isfinite(u_lo) || !std::isfinite(u_hi)) {
        return false;
    }
    double polynomial_lo = 0.0;
    double polynomial_hi = 0.0;
    for (std::size_t count = surface.asphere_coefficients.size(); count > 0; --count) {
        const std::size_t i = count - 1;
        const double coefficient_value = surface.asphere_coefficients[i];
        if (!std::isfinite(coefficient_value)) {
            return false;
        }
        const double p0 = polynomial_lo * u_lo;
        const double p1 = polynomial_lo * u_hi;
        const double p2 = polynomial_hi * u_lo;
        const double p3 = polynomial_hi * u_hi;
        if (!std::isfinite(p0) || !std::isfinite(p1) ||
            !std::isfinite(p2) || !std::isfinite(p3)) {
            return false;
        }
        const double coefficient =
            2.0 * static_cast<double>(i + 1) * coefficient_value;
        const double product_lo = std::nextafter(
            std::min(std::min(p0, p1), std::min(p2, p3)),
            -std::numeric_limits<double>::max());
        const double product_hi = std::nextafter(
            std::max(std::max(p0, p1), std::max(p2, p3)),
            std::numeric_limits<double>::max());
        polynomial_lo = std::nextafter(
            product_lo + coefficient, -std::numeric_limits<double>::max());
        polynomial_hi = std::nextafter(
            product_hi + coefficient, std::numeric_limits<double>::max());
    }
    const double p0 = polynomial_lo * radius_lo;
    const double p1 = polynomial_lo * radius_hi;
    const double p2 = polynomial_hi * radius_lo;
    const double p3 = polynomial_hi * radius_hi;
    if (!std::isfinite(p0) || !std::isfinite(p1) ||
        !std::isfinite(p2) || !std::isfinite(p3)) {
        return false;
    }
    const double bounded_lo = std::nextafter(
        std::min(std::min(p0, p1), std::min(p2, p3)),
        -std::numeric_limits<double>::max());
    const double bounded_hi = std::nextafter(
        std::max(std::max(p0, p1), std::max(p2, p3)),
        std::numeric_limits<double>::max());
    *slope_lo = std::nextafter(
        conic_lo + bounded_lo, -std::numeric_limits<double>::max());
    *slope_hi = std::nextafter(
        conic_hi + bounded_hi, std::numeric_limits<double>::max());
    return std::isfinite(*slope_lo) && std::isfinite(*slope_hi);
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline bool production_asphere_slope_interval_below_limit(
    const Surface& surface,
    double radius_lo,
    double radius_hi,
    double max_slope) {
    double slope_lo = 0.0;
    double slope_hi = 0.0;
    if (!production_asphere_radial_slope_interval(
            surface, radius_lo, radius_hi, &slope_lo, &slope_hi)) {
        return false;
    }
    const double guard =
        512.0 * std::numeric_limits<double>::epsilon() *
        std::max(1.0, max_slope);
    return slope_lo > -max_slope + guard &&
           slope_hi < max_slope - guard;
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline double production_asphere_slope_domain_radius_squared_uncached(
    const Surface& surface) {
    if (surface.kind != SurfaceKind::EvenAsphere) {
        return std::numeric_limits<double>::infinity();
    }
    const double reference_radius = std::max(
        0.0,
        std::max(
            surface.semi_aperture,
            std::max(surface.mechanical_aperture, surface.aperture_radius)));
    const double clear_radius = std::isfinite(surface.semi_aperture)
                                    ? std::max(0.0, surface.semi_aperture)
                                    : 0.0;
    double physical_limit = std::numeric_limits<double>::infinity();
    if (std::isfinite(surface.radius) && std::abs(surface.radius) >= kEps &&
        std::abs(surface.radius) < 0.5 * kInf &&
        1.0 + surface.conic > 0.0) {
        physical_limit =
            std::abs(surface.radius) / std::sqrt(1.0 + surface.conic);
    }

    double domain = std::numeric_limits<double>::infinity();
    if (!(reference_radius > 0.0) || !std::isfinite(reference_radius)) {
        domain = physical_limit;
    } else {
        const double max_slope = kAsphereRootDomainMaximumRadialSlope;
        const auto margin = [&](double radius) {
            const double slope = production_asphere_radial_slope(surface, radius);
            return std::isfinite(slope)
                       ? max_slope - std::abs(slope)
                       : -std::numeric_limits<double>::infinity();
        };
        if (clear_radius > 0.0 && margin(clear_radius) <= 0.0) {
            domain = clear_radius;
        } else {
            double previous_radius = 0.0;
            double interval_end = std::min(reference_radius, physical_limit);
            constexpr int kSamplesPerExpansion = 256;
            for (int expansion = 0; expansion < 7; ++expansion) {
                const double interval_start = previous_radius;
                if (!(interval_end > interval_start) ||
                    !std::isfinite(interval_end)) {
                    break;
                }
                int stack_begin[9] = {0};
                int stack_end[9] = {kSamplesPerExpansion};
                int stack_size = 1;
                const double interval_span = interval_end - interval_start;
                while (stack_size > 0 && !std::isfinite(domain)) {
                    --stack_size;
                    const int cell_begin = stack_begin[stack_size];
                    const int cell_end = stack_end[stack_size];
                    const double radius_begin =
                        interval_start + interval_span *
                            static_cast<double>(cell_begin) /
                            static_cast<double>(kSamplesPerExpansion);
                    const double radius_end =
                        interval_start + interval_span *
                            static_cast<double>(cell_end) /
                            static_cast<double>(kSamplesPerExpansion);
                    if (production_asphere_slope_interval_below_limit(
                            surface, radius_begin, radius_end, max_slope)) {
                        previous_radius = radius_end;
                        continue;
                    }
                    if (cell_end - cell_begin > 1) {
                        const int cell_mid =
                            cell_begin + (cell_end - cell_begin) / 2;
                        stack_begin[stack_size] = cell_mid;
                        stack_end[stack_size++] = cell_end;
                        stack_begin[stack_size] = cell_begin;
                        stack_end[stack_size++] = cell_mid;
                        continue;
                    }
                    if (margin(radius_end) <= 0.0) {
                        double lo = previous_radius;
                        double hi = radius_end;
                        for (int iteration = 0; iteration < 64; ++iteration) {
                            const double mid = 0.5 * (lo + hi);
                            if (margin(mid) > 0.0) {
                                lo = mid;
                            } else {
                                hi = mid;
                            }
                        }
                        domain = hi;
                    } else {
                        previous_radius = radius_end;
                    }
                }
                if (std::isfinite(domain) ||
                    (std::isfinite(physical_limit) &&
                     interval_end >= physical_limit)) {
                    break;
                }
                interval_end = std::min(interval_end * 2.0, physical_limit);
            }
        }
    }
    if (clear_radius > 0.0) {
        domain = std::max(domain, clear_radius);
        if (std::isfinite(physical_limit)) {
            domain = std::min(domain, physical_limit);
        }
    }
    if (!std::isfinite(domain) || !std::isfinite(domain * domain)) {
        return std::numeric_limits<double>::infinity();
    }
    return domain * domain;
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline double unified_asphere_slope_margin(
    const Surface& surface,
    double radius,
    double max_slope) {
    const double slope = production_asphere_radial_slope(surface, radius);
    return std::isfinite(slope)
               ? max_slope - std::abs(slope)
               : -std::numeric_limits<double>::infinity();
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline UnifiedAsphereDomain unified_asphere_domain_uncached(const Surface& surface) {
    UnifiedAsphereDomain result;
    if (surface.kind != SurfaceKind::EvenAsphere) {
        result.status = UnifiedAsphereDomainStatus::Unbounded;
        result.radius_squared = std::numeric_limits<double>::infinity();
        return result;
    }
    constexpr double kMaximumRadialSlope =
        kAsphereRootDomainMaximumRadialSlope;
    const bool finite_radius =
        std::isfinite(surface.radius) &&
        std::abs(surface.radius) < 0.5 * kInf;
    if (finite_radius && std::abs(surface.radius) < kEps) {
        return result;
    }
    const double conic_term = 1.0 + surface.conic;
    double search_upper = std::numeric_limits<double>::infinity();
    if (finite_radius && conic_term > 0.0) {
        const double physical_limit =
            std::abs(surface.radius) / std::sqrt(conic_term);
        search_upper = std::nextafter(physical_limit, 0.0);
        if (!(search_upper > 0.0) || !std::isfinite(search_upper)) {
            return result;
        }
    } else {
        const std::size_t count = surface.asphere_coefficients.size();
        std::vector<double> slope_coefficients(count, 0.0);
        for (std::size_t i = 0; i < count; ++i) {
            slope_coefficients[i] =
                2.0 * static_cast<double>(i + 1) *
                surface.asphere_coefficients[i];
        }
        if (finite_radius && conic_term == 0.0) {
            if (slope_coefficients.empty()) {
                slope_coefficients.resize(1, 0.0);
            }
            slope_coefficients[0] += 1.0 / surface.radius;
        }
        int highest = -1;
        for (int i = static_cast<int>(slope_coefficients.size()) - 1;
             i >= 0;
             --i) {
            if (slope_coefficients[static_cast<std::size_t>(i)] != 0.0 &&
                std::isfinite(
                    slope_coefficients[static_cast<std::size_t>(i)])) {
                highest = i;
                break;
            }
        }
        double conic_bound = 0.0;
        if (finite_radius && conic_term < 0.0) {
            conic_bound = 1.0 / std::sqrt(-conic_term);
        }
        if (highest < 0) {
            if (!(conic_bound > kMaximumRadialSlope)) {
                result.status = UnifiedAsphereDomainStatus::Unbounded;
                result.radius_squared =
                    std::numeric_limits<double>::infinity();
                return result;
            }
            const double denominator =
                1.0 - (-conic_term) * kMaximumRadialSlope *
                          kMaximumRadialSlope;
            if (!(denominator > 0.0)) {
                result.status = UnifiedAsphereDomainStatus::Unbounded;
                result.radius_squared =
                    std::numeric_limits<double>::infinity();
                return result;
            }
            const double boundary =
                std::abs(surface.radius) * kMaximumRadialSlope /
                std::sqrt(denominator);
            const double boundary_squared = boundary * boundary;
            if (!(boundary > 0.0) || !std::isfinite(boundary_squared)) {
                return result;
            }
            result.status = UnifiedAsphereDomainStatus::Finite;
            result.radius_squared = boundary_squared;
            return result;
        }

        const double leading = std::abs(
            slope_coefficients[static_cast<std::size_t>(highest)]);
        if (!(leading > 0.0) || !std::isfinite(leading)) {
            return result;
        }
        double dominance_radius_squared = 1.0;
        if (highest > 0) {
            const double factor = 2.0 * static_cast<double>(highest);
            for (int i = 0; i < highest; ++i) {
                const double lower = std::abs(
                    slope_coefficients[static_cast<std::size_t>(i)]);
                if (!(lower > 0.0)) {
                    continue;
                }
                const double exponent =
                    1.0 / static_cast<double>(highest - i);
                const double required =
                    std::pow(factor * lower / leading, exponent);
                if (!std::isfinite(required)) {
                    return result;
                }
                dominance_radius_squared =
                    std::max(dominance_radius_squared, required);
            }
        }
        const double target =
            2.0 * (kMaximumRadialSlope + conic_bound + 1.0);
        const double magnitude_radius = std::pow(
            target / leading,
            1.0 / static_cast<double>(2 * highest + 1));
        search_upper = std::max(
            1.0,
            std::max(std::sqrt(dominance_radius_squared), magnitude_radius));
        if (!(search_upper > 0.0) || !std::isfinite(search_upper)) {
            return result;
        }
        search_upper = std::nextafter(
            search_upper *
                (1.0 + 64.0 * std::numeric_limits<double>::epsilon()),
            std::numeric_limits<double>::max());
    }

    constexpr int kMaxDepth = 56;
    constexpr int kStackCapacity = kMaxDepth + 1;
    constexpr int kMaxNodes = 65536;
    std::array<double, kStackCapacity> pending_lo{};
    std::array<double, kStackCapacity> pending_hi{};
    std::array<int, kStackCapacity> pending_depth{};
    int pending_count = 0;
    int nodes = 0;
    double lo = 0.0;
    double hi = search_upper;
    int depth = 0;
    while (true) {
        if (++nodes > kMaxNodes) {
            return result;
        }
        if (!production_asphere_slope_interval_below_limit(
                surface, lo, hi, kMaximumRadialSlope)) {
            const double width_tolerance =
                2.0e-13 * std::max(
                    1.0, std::max(std::abs(lo), std::abs(hi)));
            if (depth >= kMaxDepth || hi - lo <= width_tolerance) {
                const double mid = 0.5 * (lo + hi);
                const double margin_lo = unified_asphere_slope_margin(
                    surface, lo, kMaximumRadialSlope);
                const double margin_mid = unified_asphere_slope_margin(
                    surface, mid, kMaximumRadialSlope);
                const double margin_hi = unified_asphere_slope_margin(
                    surface, hi, kMaximumRadialSlope);
                double bracket_lo = lo;
                double bracket_hi = hi;
                if (margin_lo <= 0.0) {
                    bracket_hi = lo;
                } else if (margin_mid <= 0.0) {
                    bracket_hi = mid;
                } else if (margin_hi <= 0.0) {
                    bracket_lo = mid;
                } else {
                    // Every interval before this left-first leaf has a strict
                    // slope certificate.  If outward rounding remains
                    // inconclusive at machine-scale width while all three
                    // point values are still physical, use the leaf's left
                    // edge as a conservative finite boundary.  This can omit
                    // only the unresolved final cell; it never admits a radius
                    // beyond a possible contract boundary.
                    const double boundary_squared = lo * lo;
                    if (!std::isfinite(boundary_squared)) {
                        return result;
                    }
                    result.status = UnifiedAsphereDomainStatus::Finite;
                    result.radius_squared = boundary_squared;
                    return result;
                }
                for (int iteration = 0; iteration < 64; ++iteration) {
                    const double probe = 0.5 * (bracket_lo + bracket_hi);
                    if (unified_asphere_slope_margin(
                            surface, probe, kMaximumRadialSlope) > 0.0) {
                        bracket_lo = probe;
                    } else {
                        bracket_hi = probe;
                    }
                }
                const double boundary_squared = bracket_hi * bracket_hi;
                if (!std::isfinite(boundary_squared)) {
                    return result;
                }
                result.status = UnifiedAsphereDomainStatus::Finite;
                result.radius_squared = boundary_squared;
                return result;
            }
            const double mid = 0.5 * (lo + hi);
            if (!(mid > lo) || !(hi > mid) ||
                pending_count >= kStackCapacity) {
                return result;
            }
            pending_lo[static_cast<std::size_t>(pending_count)] = mid;
            pending_hi[static_cast<std::size_t>(pending_count)] = hi;
            pending_depth[static_cast<std::size_t>(pending_count)] = depth + 1;
            ++pending_count;
            hi = mid;
            ++depth;
            continue;
        }
        if (pending_count <= 0) {
            return result;
        }
        --pending_count;
        lo = pending_lo[static_cast<std::size_t>(pending_count)];
        hi = pending_hi[static_cast<std::size_t>(pending_count)];
        depth = pending_depth[static_cast<std::size_t>(pending_count)];
    }
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline std::uint64_t production_asphere_domain_signature(const Surface& surface) {
    // Hash every input used by the domain proof. This lets each CPU worker
    // reuse the materialized domain for all rays of an unchanged candidate,
    // while direct surface mutations automatically invalidate the entry.
    constexpr std::uint64_t kOffset = 1469598103934665603ull;
    constexpr std::uint64_t kPrime = 1099511628211ull;
    std::uint64_t signature = kOffset;
    const auto append_double = [&](double value) {
        std::uint64_t bits = 0;
        static_assert(sizeof(bits) == sizeof(value));
        std::memcpy(&bits, &value, sizeof(bits));
        signature ^= bits;
        signature *= kPrime;
    };
    append_double(surface.radius);
    append_double(surface.conic);
    append_double(surface.semi_aperture);
    append_double(surface.mechanical_aperture);
    append_double(surface.aperture_radius);
    signature ^= static_cast<std::uint64_t>(surface.asphere_coefficients.size());
    signature *= kPrime;
    for (double coefficient : surface.asphere_coefficients) {
        append_double(coefficient);
    }
    return signature;
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline double production_asphere_slope_domain_radius_squared(const Surface& surface) {
    if (surface.kind != SurfaceKind::EvenAsphere) {
        return std::numeric_limits<double>::infinity();
    }
    struct CacheEntry {
        const Surface* surface = nullptr;
        std::uint64_t signature = 0;
        double radius_squared = std::numeric_limits<double>::infinity();
    };
    // Direct-mapped and deliberately small: CPU scoring walks a small set of
    // candidate surfaces repeatedly, and each worker owns its entries.
    thread_local std::array<CacheEntry, 64> cache{};
    const std::uint64_t signature =
        production_asphere_domain_signature(surface);
    const std::uintptr_t address =
        reinterpret_cast<std::uintptr_t>(&surface);
    const std::size_t slot = static_cast<std::size_t>(
        ((address >> 4) ^ signature ^ (signature >> 32)) &
        (cache.size() - 1));
    CacheEntry& entry = cache[slot];
    if (entry.surface == &surface && entry.signature == signature) {
        return entry.radius_squared;
    }
    const double radius_squared =
        production_asphere_slope_domain_radius_squared_uncached(surface);
    entry.surface = &surface;
    entry.signature = signature;
    entry.radius_squared = radius_squared;
    return radius_squared;
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline UnifiedAsphereDomain unified_asphere_domain(const Surface& surface) {
    struct CacheEntry {
        const Surface* surface = nullptr;
        std::uint64_t signature = 0;
        UnifiedAsphereDomain domain;
    };
    // Four-way sets avoid the deterministic pointer/signature collisions seen
    // when an 18-asphere candidate is walked repeatedly.  The full signature
    // remains part of every hit, so in-place optimizer mutations invalidate
    // safely without relying on pointer identity alone.
    thread_local std::array<CacheEntry, 1024> cache{};
    thread_local std::array<unsigned char, 256> next_way{};
    const std::uint64_t signature =
        production_asphere_domain_signature(surface);
    const std::uintptr_t address =
        reinterpret_cast<std::uintptr_t>(&surface);
    const std::size_t set = static_cast<std::size_t>(
        ((address >> 4) ^ signature ^ (signature >> 32)) & 255u);
    const std::size_t first = set * 4;
    CacheEntry* empty = nullptr;
    for (std::size_t way = 0; way < 4; ++way) {
        CacheEntry& candidate = cache[first + way];
        if (candidate.surface == &surface &&
            candidate.signature == signature) {
            return candidate.domain;
        }
        if (!empty && candidate.surface == nullptr) {
            empty = &candidate;
        }
    }
    CacheEntry& entry = empty
        ? *empty
        : cache[first + (next_way[set]++ & 3u)];
    entry.surface = &surface;
    entry.signature = signature;
    entry.domain = unified_asphere_domain_uncached(surface);
    return entry.domain;
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline bool unified_direction_certificate(const Ray& ray) {
    const double transverse_speed = std::hypot(ray.d.x, ray.d.y);
    if (!std::isfinite(transverse_speed) || !std::isfinite(ray.d.z)) {
        return false;
    }
    const double axial_speed = std::abs(ray.d.z);
    const double bound =
        kAsphereRootDomainMaximumRadialSlopeConservative * transverse_speed;
    const double guard =
        512.0 * std::numeric_limits<double>::epsilon() *
        std::max(1.0, axial_speed);
    return axial_speed > bound + guard;
}
} // namespace raytrace_detail

namespace raytrace_detail {
inline bool restrict_search_to_production_slope_domain(
    const Surface& surface,
    const Ray& ray,
    double radius_squared,
    double* lower,
    double* upper) {
    if (!lower || !upper || !std::isfinite(*lower) || *lower < 0.0 ||
        *upper < *lower) {
        return false;
    }
    if (!std::isfinite(radius_squared)) {
        return true;
    }
    if (!(radius_squared >= 0.0)) {
        return false;
    }
    const double accepted_radius_squared =
        radius_squared * (1.0 + 2.0e-12);
    const double a = ray.d.x * ray.d.x + ray.d.y * ray.d.y;
    const double b = 2.0 * (ray.p.x * ray.d.x + ray.p.y * ray.d.y);
    const double c = ray.p.x * ray.p.x + ray.p.y * ray.p.y -
                     accepted_radius_squared;
    if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c)) {
        return false;
    }
    if (!(a > 0.0)) {
        return c <= 0.0;
    }
    double discriminant = b * b - 4.0 * a * c;
    const double discriminant_scale =
        std::max(1.0, std::abs(b * b) + std::abs(4.0 * a * c));
    const double discriminant_tolerance =
        64.0 * std::numeric_limits<double>::epsilon() * discriminant_scale;
    if (discriminant < -discriminant_tolerance) {
        return false;
    }
    discriminant = std::max(0.0, discriminant);
    const double sqrt_discriminant = std::sqrt(discriminant);
    double domain_lower = 0.0;
    double domain_upper = 0.0;
    const double q = -0.5 * (b + std::copysign(sqrt_discriminant, b));
    if (std::abs(q) > std::numeric_limits<double>::min()) {
        const double root_a = q / a;
        const double root_b = c / q;
        domain_lower = std::min(root_a, root_b);
        domain_upper = std::max(root_a, root_b);
    } else {
        domain_lower = domain_upper = -b / (2.0 * a);
    }
    if (!std::isfinite(domain_lower) || !std::isfinite(domain_upper)) {
        return false;
    }
    const double clipped_lower = std::max(*lower, domain_lower);
    const double clipped_upper = std::min(*upper, domain_upper);
    const double scale = std::max(
        1.0,
        std::max(
            std::abs(clipped_lower),
            std::isfinite(clipped_upper) ? std::abs(clipped_upper) : 1.0));
    const double interval_tolerance =
        64.0 * std::numeric_limits<double>::epsilon() * scale;
    if (clipped_upper < clipped_lower - interval_tolerance) {
        return false;
    }
    *lower = clipped_lower;
    *upper = std::max(clipped_lower, clipped_upper);
    return true;
}
} // namespace raytrace_detail

} // namespace optiland
