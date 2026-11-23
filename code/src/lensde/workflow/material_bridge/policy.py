"""Retained material-bridge contracts and numerical tolerances."""
from __future__ import annotations

from .paths import CPP_DIR


CONTINUOUS_MATERIAL_KINDS = {"material_nd", "material_vd"}

MATERIAL_BRIDGE_VIGNETTING_CONTRACT = "minimal_apg_cp_dynamic_4x8_bridge_v9"

MATERIAL_BRIDGE_SCORE_CONTRACT = "cpu_cuda_dynamic_4x8_score_v3"

MATERIAL_BRIDGE_STAGE_CONTRACT = "dynamic_4x8_full_aperture_except_cover_bridge_v11"

MATERIAL_BRIDGE_SELECTION_MERIT_CONTRACT = (
    "pre_vignetting_calibration_merit_then_post_bridge_effective_v1"
)

MATERIAL_BRIDGE_CPU_CUDA_MERIT_REL_TOL = 5.0e-3

MATERIAL_BRIDGE_CPU_CUDA_COVERAGE_TOL = 1.0e-10

DE_DLS_TRACE_COVERAGE_BASELINE = 0.95

MATERIAL_BRIDGE_CPU_CUDA_EDGE_TOL = 1.0e-6
