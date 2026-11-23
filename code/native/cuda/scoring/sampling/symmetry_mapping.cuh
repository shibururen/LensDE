#pragma once
#include "../prelude.cuh"
#include "../aiming/edge_scale_solver.cuh"
#include "../contracts/constants.cuh"
#include "../contracts/device_config.cuh"
#include "../contracts/optical_records.cuh"
#include "pupil_nodes.cuh"

namespace optiland {
namespace {
__device__ int d_positive_mod(int value, int modulus) {
    if (modulus <= 0) {
        return 0;
    }
    const int remainder = value % modulus;
    return remainder < 0 ? remainder + modulus : remainder;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_symmetry_allows_transform(int symmetry, int transform_code) {
    if (transform_code == 0) {
        return true;
    }
    if (symmetry == kPupilSymmetryMirrorX) {
        return transform_code == 1;
    }
    if (symmetry == kPupilSymmetryMirrorY) {
        return transform_code == 2;
    }
    if (symmetry == kPupilSymmetrySignXY) {
        return transform_code >= 1 && transform_code <= 3;
    }
    return symmetry == kPupilSymmetryDihedral4 &&
           transform_code >= 1 && transform_code <= 7;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ bool d_uses_selective_spot_tail_edge_sampling(
    const DeviceConfig& cfg,
    const DeviceField& field) {
    return cfg.operand_single_ray_spot_tail &&
           hypot(field.hx, field.hy) + 1.0e-12 >=
               cfg.single_ray_spot_tail_extra_edge_min_field_radius;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
// The extra edge set is phase shifted by half an arm from the ordinary
// Gaussian 8-arm set: theta = 22.5 deg + 45 deg * arm.  These index formulas
// preserve that half-step phase under the same eight transverse transforms
// used by the main pupil reconstruction path.
__device__ int d_spot_tail_extra_transformed_arm(
    int arm,
    int transform_code) {
    const int source = d_positive_mod(arm, kSpotTailExtraEdgeCount);
    if (transform_code == 0) {
        return source;
    }
    if (transform_code == 1) {
        return d_positive_mod(3 - source, kSpotTailExtraEdgeCount);
    }
    if (transform_code == 2) {
        return d_positive_mod(7 - source, kSpotTailExtraEdgeCount);
    }
    if (transform_code == 3) {
        return d_positive_mod(source + 4, kSpotTailExtraEdgeCount);
    }
    if (transform_code == 4) {
        return d_positive_mod(1 - source, kSpotTailExtraEdgeCount);
    }
    if (transform_code == 5) {
        return d_positive_mod(source + 2, kSpotTailExtraEdgeCount);
    }
    if (transform_code == 6) {
        return d_positive_mod(source + 6, kSpotTailExtraEdgeCount);
    }
    if (transform_code == 7) {
        return d_positive_mod(5 - source, kSpotTailExtraEdgeCount);
    }
    return source;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ int d_spot_tail_extra_canonical_arm(
    int arm,
    int symmetry) {
    int canonical = d_positive_mod(arm, kSpotTailExtraEdgeCount);
    for (int transform_code = 1; transform_code <= 7; ++transform_code) {
        if (d_symmetry_allows_transform(symmetry, transform_code)) {
            canonical = min(
                canonical,
                d_spot_tail_extra_transformed_arm(arm, transform_code));
        }
    }
    return canonical;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ int d_spot_tail_extra_transform_code_between(
    int source_arm,
    int target_arm,
    int symmetry) {
    for (int transform_code = 0; transform_code <= 7; ++transform_code) {
        if (d_symmetry_allows_transform(symmetry, transform_code) &&
            d_spot_tail_extra_transformed_arm(
                source_arm, transform_code) ==
                d_positive_mod(target_arm, kSpotTailExtraEdgeCount)) {
            return transform_code;
        }
    }
    return -1;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ void d_spot_tail_extra_edge_pupil_at(
    int arm,
    double* px,
    double* py) {
    const double theta =
        2.0 * 3.14159265358979323846 *
        (static_cast<double>(d_positive_mod(arm, kSpotTailExtraEdgeCount)) +
         0.5) /
        static_cast<double>(kSpotTailExtraEdgeCount);
    *px = cos(theta);
    *py = sin(theta);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
// Return the arm reached by one of the fixed transverse transforms used by
// the reconstruction kernel.  Transforms that do not preserve the discrete
// arm set are rejected, so an unsupported arm count safely traces more rays.
__device__ bool d_gaussian_transformed_arm(
    int arms,
    int arm,
    int transform_code,
    int* transformed_arm) {
    if (!transformed_arm || arms <= 0) {
        return false;
    }
    const int source = d_positive_mod(arm, arms);
    if (transform_code == 0) {
        *transformed_arm = source;
        return true;
    }
    if (transform_code == 2) {
        *transformed_arm = d_positive_mod(-source, arms);
        return true;
    }
    if (transform_code == 1 || transform_code == 3) {
        if ((arms % 2) != 0) {
            return false;
        }
        *transformed_arm = transform_code == 1
                               ? d_positive_mod(arms / 2 - source, arms)
                               : d_positive_mod(source + arms / 2, arms);
        return true;
    }
    if ((arms % 4) != 0) {
        return false;
    }
    if (transform_code == 4) {
        *transformed_arm = d_positive_mod(arms / 4 - source, arms);
    } else if (transform_code == 5) {
        *transformed_arm = d_positive_mod(source + arms / 4, arms);
    } else if (transform_code == 6) {
        *transformed_arm = d_positive_mod(source + 3 * arms / 4, arms);
    } else if (transform_code == 7) {
        *transformed_arm = d_positive_mod(3 * arms / 4 - source, arms);
    } else {
        return false;
    }
    return true;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ int d_gaussian_canonical_arm(
    int arms,
    int arm,
    int symmetry) {
    int canonical = d_positive_mod(arm, arms);
    for (int transform_code = 1; transform_code <= 7; ++transform_code) {
        if (!d_symmetry_allows_transform(symmetry, transform_code)) {
            continue;
        }
        int candidate = canonical;
        if (d_gaussian_transformed_arm(
                arms, arm, transform_code, &candidate)) {
            canonical = min(canonical, candidate);
        }
    }
    return canonical;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ int d_gaussian_representative_arm_count(
    int arms,
    int symmetry) {
    int count = 0;
    for (int arm = 0; arm < arms; ++arm) {
        if (d_gaussian_canonical_arm(arms, arm, symmetry) == arm) {
            ++count;
        }
    }
    return max(count, 1);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ int d_gaussian_representative_arm_at(
    int arms,
    int symmetry,
    int representative_index) {
    int current = 0;
    for (int arm = 0; arm < arms; ++arm) {
        if (d_gaussian_canonical_arm(arms, arm, symmetry) != arm) {
            continue;
        }
        if (current == representative_index) {
            return arm;
        }
        ++current;
    }
    return -1;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ int d_gaussian_representative_arm_rank(
    int arms,
    int symmetry,
    int representative_arm) {
    int rank = 0;
    for (int arm = 0; arm < representative_arm; ++arm) {
        if (d_gaussian_canonical_arm(arms, arm, symmetry) == arm) {
            ++rank;
        }
    }
    return rank;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ int d_gaussian_transform_code_between(
    int arms,
    int source_arm,
    int target_arm,
    int symmetry) {
    for (int transform_code = 0; transform_code <= 7; ++transform_code) {
        if (!d_symmetry_allows_transform(symmetry, transform_code)) {
            continue;
        }
        int transformed_arm = source_arm;
        if (d_gaussian_transformed_arm(
                arms, source_arm, transform_code, &transformed_arm) &&
            transformed_arm == d_positive_mod(target_arm, arms)) {
            return transform_code;
        }
    }
    return -1;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ int d_field_gaussian_rings(
    const DeviceConfig& cfg,
    const DeviceField& field) {
    const int balanced = d_fixed_budget_pupil_shape(cfg,field);
    if (balanced) return balanced == 1 ? 12 : 8;
    return d_uses_elevated_field_sampling(cfg, field)
               ? max(cfg.elevated_field_num_rays, 1)
               : max(cfg.num_rays, 1);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ int d_field_gaussian_arms(
    const DeviceConfig& cfg,
    const DeviceField& field) {
    const int balanced = d_fixed_budget_pupil_shape(cfg,field);
    if (balanced) return balanced == 1 ? 4 : 6;
    return d_uses_elevated_field_sampling(cfg, field)
               ? max(cfg.elevated_field_pupil_arms, 1)
               : max(cfg.pupil_arms, 1);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ int d_codev_dihedral_representative_count(
    const DeviceConfig& cfg) {
    const int quadrant_count = d_codev_quadrant_count(cfg);
    int count = 0;
    for (int qi = 0; qi < quadrant_count; ++qi) {
        double x = 0.0;
        double y = 0.0;
        double radius_sq = 0.0;
        if (d_codev_quadrant_at(cfg, qi, &x, &y, &radius_sq) &&
            x + 2.0e-12 >= y) {
            ++count;
        }
    }
    return count;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ int d_field_physical_pupil_count(
    const DeviceConfig& cfg,
    const DeviceField& field) {
    const int full_count = d_field_pupil_count(cfg, field);
    const int symmetry = d_field_pupil_symmetry(cfg, field);
    if (cfg.pupil_sampling_type == kPupilSamplingGaussianRingArm &&
        symmetry != kPupilSymmetryNone) {
        return d_field_gaussian_rings(cfg, field) *
               d_gaussian_representative_arm_count(
                   d_field_gaussian_arms(cfg, field), symmetry);
    }
    if (symmetry == kPupilSymmetryMirrorX ||
        symmetry == kPupilSymmetryMirrorY) {
        return full_count / 2;
    }
    if (symmetry == kPupilSymmetrySignXY) {
        return full_count / 4;
    }
    if (symmetry == kPupilSymmetryDihedral4) {
        return d_codev_dihedral_representative_count(cfg);
    }
    return full_count;
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
__device__ void d_field_physical_pupil_at(
    const DeviceConfig& cfg,
    const DeviceField& field,
    int physical_index,
    double* px,
    double* py,
    double* weight,
    double* wavefront_weight = nullptr) {
    const int symmetry = d_field_pupil_symmetry(cfg, field);
    int logical_index = physical_index;
    if (cfg.pupil_sampling_type == kPupilSamplingGaussianRingArm &&
        symmetry != kPupilSymmetryNone) {
        const int arms = d_field_gaussian_arms(cfg, field);
        const int representative_count =
            d_gaussian_representative_arm_count(arms, symmetry);
        const int ring = physical_index / representative_count;
        const int representative_index = physical_index % representative_count;
        const int arm = d_gaussian_representative_arm_at(
            arms, symmetry, representative_index);
        logical_index = arm >= 0 ? ring * arms + arm : -1;
    } else if (symmetry == kPupilSymmetryMirrorX) {
        logical_index = (physical_index / 2) * 4 +
                        (physical_index % 2) * 2;
    } else if (symmetry == kPupilSymmetryMirrorY) {
        logical_index = (physical_index / 2) * 4 +
                        (physical_index % 2);
    } else if (symmetry == kPupilSymmetrySignXY) {
        logical_index = physical_index * 4;
    } else if (symmetry == kPupilSymmetryDihedral4) {
        const int quadrant_count = d_codev_quadrant_count(cfg);
        int representative = 0;
        logical_index = -1;
        for (int qi = 0; qi < quadrant_count; ++qi) {
            double x = 0.0;
            double y = 0.0;
            double radius_sq = 0.0;
            if (!d_codev_quadrant_at(cfg, qi, &x, &y, &radius_sq) ||
                x + 2.0e-12 < y) {
                continue;
            }
            if (representative == physical_index) {
                logical_index = qi * 4;
                break;
            }
            ++representative;
        }
    }
    if (logical_index < 0) {
        *px = 0.0;
        *py = 0.0;
        *weight = 0.0;
        if (wavefront_weight) {
            *wavefront_weight = 0.0;
        }
        return;
    }
    d_field_pupil_at(
        cfg, field, logical_index, px, py, weight, wavefront_weight);
}
} // namespace
} // namespace optiland

namespace optiland {
namespace {
// Maps a logical full-pupil index to its compact physical trace and to one of
// the eight transverse transforms used by the CPU symmetry reconstruction:
// identity, flip-x, flip-y, flip-xy, swap, (-y,x), (y,-x), and (-y,-x).
__device__ bool d_field_logical_pupil_mapping(
    const DeviceConfig& cfg,
    const DeviceField& field,
    int logical_index,
    int* physical_index,
    int* transform_code) {
    if (!physical_index || !transform_code || logical_index < 0) {
        return false;
    }
    const int symmetry = d_field_pupil_symmetry(cfg, field);
    if (cfg.pupil_sampling_type == kPupilSamplingGaussianRingArm) {
        if (symmetry == kPupilSymmetryNone) {
            *physical_index = logical_index;
            *transform_code = 0;
            return true;
        }
        const int arms = d_field_gaussian_arms(cfg, field);
        const int ring = logical_index / arms;
        const int target_arm = logical_index - ring * arms;
        const int representative_arm =
            d_gaussian_canonical_arm(arms, target_arm, symmetry);
        const int representative_count =
            d_gaussian_representative_arm_count(arms, symmetry);
        const int representative_rank = d_gaussian_representative_arm_rank(
            arms, symmetry, representative_arm);
        const int code = d_gaussian_transform_code_between(
            arms, representative_arm, target_arm, symmetry);
        if (code < 0) {
            return false;
        }
        *physical_index = ring * representative_count + representative_rank;
        *transform_code = code;
        return true;
    }
    const int quadrant_index = logical_index / 4;
    const int sign_index = logical_index % 4;
    if (symmetry == kPupilSymmetryMirrorX) {
        *physical_index = 2 * quadrant_index + (sign_index >= 2 ? 1 : 0);
        *transform_code = (sign_index & 1) ? 1 : 0;
        return true;
    }
    if (symmetry == kPupilSymmetryMirrorY) {
        *physical_index = 2 * quadrant_index + (sign_index & 1);
        *transform_code = sign_index >= 2 ? 2 : 0;
        return true;
    }
    if (symmetry == kPupilSymmetrySignXY) {
        *physical_index = quadrant_index;
        *transform_code = sign_index;
        return true;
    }
    if (symmetry == kPupilSymmetryDihedral4) {
        double target_x = 0.0;
        double target_y = 0.0;
        double target_radius_sq = 0.0;
        if (!d_codev_quadrant_at(
                cfg,
                quadrant_index,
                &target_x,
                &target_y,
                &target_radius_sq)) {
            return false;
        }
        const double source_x = fmax(target_x, target_y);
        const double source_y = fmin(target_x, target_y);
        const int quadrant_count = d_codev_quadrant_count(cfg);
        int representative = 0;
        for (int qi = 0; qi < quadrant_count; ++qi) {
            double x = 0.0;
            double y = 0.0;
            double radius_sq = 0.0;
            if (!d_codev_quadrant_at(cfg, qi, &x, &y, &radius_sq) ||
                x + 2.0e-12 < y) {
                continue;
            }
            if (fabs(x - source_x) <= 2.0e-12 &&
                fabs(y - source_y) <= 2.0e-12) {
                *physical_index = representative;
                *transform_code =
                    target_x + 2.0e-12 >= target_y
                        ? sign_index
                        : 4 + sign_index;
                return true;
            }
            ++representative;
        }
        return false;
    }
    *physical_index = logical_index;
    *transform_code = 0;
    return true;
}
} // namespace
} // namespace optiland
