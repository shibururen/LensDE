"""Shared checkpoint selection and numerical contract constants."""
from __future__ import annotations

from .paths import CPP_DIR


QUALITY_CAP = 1.10

CORE_QUALITY_CAP = 1.02

BASIN_FILL_QUALITY_CAP = 6.0

FINAL_RANK1_CORE_QUALITY_CAP = 3.0

FINAL_RANK1_POTENTIAL_QUALITY_CAP = 6.0

FINAL_RANK1_POTENTIAL_MIN_IMPROVEMENT = 1.5

CHECKPOINT_TOTAL_COUNT = 20

RANK1_ANCHOR_LIMIT = 5

FULL_DISTANCE_FLOOR = 0.05

SKELETON_DISTANCE_FLOOR = 0.05

ASPHERE_DISTANCE_FLOOR = 0.08

CHECKPOINT_DISTANCE_RELAXATION_TIERS = ()

CHECKPOINT_MERIT_RELAXATION_CAPS = (6.0, 30.0, 100.0)

OUTER_SHORTLIST_LIMIT = 512

OUTER_PREFILTER_LIMIT = 6

GLOBAL_OUTER_FLOOR = 2

GLOBAL_KEY_SEAT_LIMIT = 4

CHECKPOINT_TOPOLOGY_REPLACEMENT_LIMIT = 4

CHECKPOINT_TOPOLOGY_POWER_DISTANCE_FLOOR = 0.25

CHECKPOINT_TOPOLOGY_EFFECTIVE_DISTANCE_FLOOR = 1.0

ASPHERE_KINDS = {"conic", "asphere_coeff"}

SKELETON_KINDS = {"curvature", "radius", "thickness"}

CONTINUOUS_MATERIAL_KINDS = {"material_nd", "material_vd"}

CHECKPOINT_MULTI_ISLAND_VALIDATED_CONTRACT = {
    "minimum_island_count": 2,
    "maximum_island_count": 20,
    "population": 100,
    "generations": 400,
}

CHECKPOINT_MULTI_ISLAND_RUNTIME_INPUTS = (
    CPP_DIR / "cuda" / "de_cuda_scorer.cu",
    CPP_DIR / "cuda" / "de_cuda_device_common.cuh",
    CPP_DIR / "cuda" / "de_cuda_asphere_intersection.cu",
    CPP_DIR / "include" / "optiland" / "population_broker.hpp",
    CPP_DIR / "src" / "population_broker.cpp",
    CPP_DIR / "src" / "multi_island_experiment_main.cpp",
)

CHECKPOINT_SCORE_CONTRACT = "cpu_materialized_dynamic_aperture_4x8_v3"

MATERIALIZATION_TOLERANCE = 1.0e-12
