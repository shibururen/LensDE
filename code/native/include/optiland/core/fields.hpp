#pragma once
#include <limits>

namespace optiland {
struct Field {
    double hx = 0.0;
    double hy = 0.0;
    double fvcx = 0.0;
    double fvcy = 0.0;
    double fvdx = 0.0;
    double fvdy = 0.0;

    double vignette_x = 0.0;
    double vignette_y = 0.0;

    // Transient diagnostics populated together with a dynamically traced
    // clear-aperture envelope.  They are intentionally not serialized: stage
    // vignetting anchors remain expressed only by FVCY/FVDY, while merit
    // evaluation can verify that each active meridional crop has selective
    // support from at least one non-stop surface in the same trace.  Zero is
    // supported; positive values measure the remaining normalized evidence
    // deficit after contact and opposite-side clearance are considered.
    double vignetting_negative_boundary_support_gap =
        std::numeric_limits<double>::quiet_NaN();
    double vignetting_positive_boundary_support_gap =
        std::numeric_limits<double>::quiet_NaN();
    int vignetting_negative_boundary_support_surface = -1;
    int vignetting_positive_boundary_support_surface = -1;
    // Certified adaptive global-trim proposal associated with the support
    // owner above.  These values are transient optimization diagnostics: the
    // score contribution serializes them so downstream materialization does
    // not have to reconstruct a trim radius from the all-field envelope.
    double vignetting_negative_boundary_candidate_radius =
        std::numeric_limits<double>::quiet_NaN();
    double vignetting_positive_boundary_candidate_radius =
        std::numeric_limits<double>::quiet_NaN();
    double vignetting_negative_boundary_pretrim_radius =
        std::numeric_limits<double>::quiet_NaN();
    double vignetting_positive_boundary_pretrim_radius =
        std::numeric_limits<double>::quiet_NaN();
    double vignetting_negative_boundary_protection_radius =
        std::numeric_limits<double>::quiet_NaN();
    double vignetting_positive_boundary_protection_radius =
        std::numeric_limits<double>::quiet_NaN();
    double vignetting_negative_boundary_trim_window =
        std::numeric_limits<double>::quiet_NaN();
    double vignetting_positive_boundary_trim_window =
        std::numeric_limits<double>::quiet_NaN();
};

struct Wavelength {
    double value_um = 0.5875618;
    double weight = 1.0;
    bool primary = false;
};


}  // namespace optiland
