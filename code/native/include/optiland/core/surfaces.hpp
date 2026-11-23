#pragma once
#include "optiland/core/numeric.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace optiland {
enum class SurfaceKind {
    Standard,
    Plane,
    EvenAsphere,
    Image,
    Object,
};

struct Material {
    enum class Model {
        Ideal,
        Abbe,
        Formula,
        Tabulated,
    };

    std::string name = "air";
    std::string reference;
    std::string source_path;
    Model model = Model::Ideal;
    double nd = 1.0;
    double vd = 0.0;
    int formula_type = 0;
    std::vector<double> coefficients;
    std::vector<double> table_wavelengths;
    std::vector<double> table_n;
    bool wavelength_range_declared = false;
    double wavelength_min_um = std::numeric_limits<double>::quiet_NaN();
    double wavelength_max_um = std::numeric_limits<double>::quiet_NaN();

    static Material air() { return {"air", "", "", Model::Ideal, 1.0, 0.0}; }
    static Material ideal(std::string name, double index) {
        return {std::move(name), "", "", Model::Ideal, index, 0.0};
    }
    static Material abbe(std::string name, double nd, double vd) {
        return {std::move(name), "", "", Model::Abbe, nd, vd};
    }

    double refractive_index(double wavelength_um) const;
    bool covers_wavelength(double wavelength_um, double tolerance_um = 1.0e-9) const {
        if (!wavelength_range_declared || !std::isfinite(wavelength_min_um) ||
            !std::isfinite(wavelength_max_um) || wavelength_min_um > wavelength_max_um) {
            return false;
        }
        const double tolerance = std::max(0.0, tolerance_um);
        return wavelength_min_um <= wavelength_um + tolerance &&
               wavelength_max_um + tolerance >= wavelength_um;
    }
    Material as_ideal(double wavelength_um = 0.5875618) const {
        return ideal(name + "-ideal", refractive_index(wavelength_um));
    }
    Material as_abbe(double nd_value, double vd_value) const {
        return abbe(name + "-abbe", nd_value, vd_value);
    }
};

struct Surface {
    SurfaceKind kind = SurfaceKind::Standard;
    double radius = kInf;
    double conic = 0.0;
    std::vector<double> asphere_coefficients;
    int newton_max_iter = 80;
    double newton_tol = 1.0e-10;
    double thickness = 0.0;
    double semi_aperture = 0.0;
    double mechanical_aperture = 0.0;
    double aperture_radius = 0.0;
    bool clear_aperture_fixed = false;
    // Cover glass still refracts rays but cannot justify a vignetting crop.
    bool vignetting_aperture_excluded = false;
    bool mechanical_aperture_fixed = false;
    bool is_stop = false;
    bool reflective = false;
    Material material_after = Material::air();
    std::string comment;

    bool is_plane() const {
        return kind == SurfaceKind::Plane || kind == SurfaceKind::Image ||
               kind == SurfaceKind::Object ||
               ((kind != SurfaceKind::EvenAsphere) &&
                (!std::isfinite(radius) || std::abs(radius) < kEps));
    }
};


}  // namespace optiland
