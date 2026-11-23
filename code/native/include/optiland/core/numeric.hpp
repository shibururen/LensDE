#pragma once
#include <cmath>
#include <limits>

namespace optiland {


// Fixed production semantics, not a per-candidate fallback mode.
inline constexpr char kChiefReferenceContract[] = "vignetted_pupil_chief_v1";

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kEps = 1.0e-12;

inline double deg_to_rad(double deg) { return deg * kPi / 180.0; }
inline double rad_to_deg(double rad) { return rad * 180.0 / kPi; }

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    Vec3() = default;
    Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& other) const {
        return {x + other.x, y + other.y, z + other.z};
    }

    Vec3 operator-(const Vec3& other) const {
        return {x - other.x, y - other.y, z - other.z};
    }

    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
};

inline double dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline double norm(const Vec3& v) {
    return std::sqrt(dot(v, v));
}

inline Vec3 normalize(const Vec3& v) {
    const double n = norm(v);
    if (n <= kEps || !std::isfinite(n)) {
        return {0.0, 0.0, 1.0};
    }
    return {v.x / n, v.y / n, v.z / n};
}


}  // namespace optiland
