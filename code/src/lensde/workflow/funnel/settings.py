"""Preserve the immutable scientific constants shared by funnel responsibilities."""
from __future__ import annotations
from lensde.workflow import run_post_de_basin_refine_funnel as material_funnel
from lensde.engine import core as server

DEFAULT_MAX_LOCAL_ITER_PER_RESTART = material_funnel.DEFAULT_MAX_LOCAL_ITER_PER_RESTART
GLASS_SCOUT_CPU_CUDA_MERIT_REL_TOL = 0.01
FINAL_DLS_CPU_CUDA_CONTRACT = "sampled_pupil_exact_materialized_system_v1"
FINAL_DLS_CPU_CUDA_MERIT_REL_TOL = 0.005
FINAL_DLS_CPU_CUDA_COVERAGE_ABS_TOL = 1e-10
FINAL_DLS_CPU_CUDA_EDGE_ABS_TOL = 1e-06
FINAL_DLS_CPU_CUDA_CONSTRAINT_ABS_TOL = 0.0001
FINAL_DLS_CPU_CUDA_SPOT_REL_TOL = 5e-05
DE_DLS_TRACE_COVERAGE_BASELINE = 0.95
TERMINAL_ZEMAX_VIGNETTING_CONTRACT = "terminal_zemax_set_vignetting_v2"
STAGE_BOUNDARY_SOURCE_APERTURE_RELATIVE_TOLERANCE = 0.05
STAGE_BOUNDARY_MODE_APERTURE_RELATIVE_TOLERANCE = 0.03
STAGE_BOUNDARY_COVERAGE_ABSOLUTE_TOLERANCE = 0.01
FIXED_GLASS_SEED_POLICY_STABLE = "stable_candidate_identity_v1"
FIXED_GLASS_SEED_POLICY_LEGACY = "legacy_source_rank"
FIXED_GLASS_SEED_POLICY_NUMERIC = server.seed_protocol.NUMERIC_POLICY
LOCKED_GLASS_MATERIAL_VARIABLE_KINDS = {"material_nd", "material_vd", "glass_choice"}
FINAL_LOCKED_GLASS_DLS_GEOMETRY_KINDS = {
    "curvature",
    "thickness",
    "conic",
    "asphere",
    "asphere_coeff",
}
