"""Define illumination gates, aiming copies and stage-boundary aperture/trace contracts."""
from __future__ import annotations
from typing import Any
from .common import finite_float
import math
from pathlib import Path
import argparse
from lensde.workflow import run_material5_bridge_selection as material_bridge
from .common import read_json
from lensde.engine import core as server
from lensde.helpers import run_explore_dls_feedback_cycles as feedback_cycles
import copy
from .contracts import MaterialPolicy


def _vignetting_ellipse_inside_unit_pupil(system: dict[str, Any]) -> bool:
    for field in system.get("fields", []) or []:
        fvcx = finite_float(field.get("fvcx"), 0.0)
        fvcy = finite_float(field.get("fvcy"), 0.0)
        semi_x = 1.0 - finite_float(field.get("fvdx"), 0.0)
        semi_y = 1.0 - finite_float(field.get("fvdy"), 0.0)
        if semi_x < -1e-12 or semi_y < -1e-12:
            return False
        for sample in range(2048):
            angle = 2.0 * math.pi * sample / 2048.0
            x = fvcx + semi_x * math.cos(angle)
            y = fvcy + semi_y * math.sin(angle)
            if x * x + y * y > 1.0 + 2e-10:
                return False
    return True


def illumination_optimization_enabled(
    args: argparse.Namespace, source_merit: dict[str, Any] | None = None
) -> bool:
    """Resolve illumination from the active material contract, including resumes."""
    template = getattr(args, "material_merit_template", None)
    context = getattr(args, "material_reference_run", None) or getattr(args, "basin_run_dir", None)
    if template:
        merit = read_json(Path(template))
    elif context:
        merit = read_json(material_bridge.source_file(Path(context), "merit.json"))
    else:
        merit = source_merit or {}
    return server.vignetting_optimization_enabled(merit)


def stage_boundary_vignetting_recalibration_enabled(
    args: argparse.Namespace, source_merit: dict[str, Any] | None = None
) -> bool:
    """Automatic handoff calibration requires an active illumination objective."""
    return bool(
        getattr(args, "stage_boundary_vignetting_recalibration_enabled", True)
    ) and illumination_optimization_enabled(args, source_merit)


def terminal_zemax_vignetting_finalization_enabled(
    args: argparse.Namespace, source_merit: dict[str, Any] | None = None
) -> bool:
    """Automatic terminal calibration requires an active illumination objective."""
    return bool(
        getattr(args, "terminal_zemax_vignetting_finalization_enabled", True)
    ) and illumination_optimization_enabled(args, source_merit)


def vignetting_calibration_row_indices(
    args: argparse.Namespace, source_rows: list[dict[str, Any]], *, terminal: bool
) -> set[int]:
    """Enforce the gate inside directly callable boundary/terminal helpers too."""
    flag = (
        "terminal_zemax_vignetting_finalization_enabled"
        if terminal
        else "stage_boundary_vignetting_recalibration_enabled"
    )
    if not bool(getattr(args, flag, True)):
        return set()
    predicate = (
        terminal_zemax_vignetting_finalization_enabled
        if terminal
        else stage_boundary_vignetting_recalibration_enabled
    )
    if any(
        (
            getattr(args, name, None)
            for name in ("material_merit_template", "material_reference_run", "basin_run_dir")
        )
    ):
        return set(range(1, len(source_rows) + 1)) if predicate(args) else set()
    return {
        position
        for position, row in enumerate(source_rows, 1)
        if predicate(
            args,
            read_json(feedback_cycles.file_from_source(Path(str(row["out_dir"])), "merit.json")),
        )
    }


def strict_adaptive_pilot_grid_system(system: dict[str, Any]) -> dict[str, Any]:
    """Return a strict exact-aiming copy suitable for terminal validation."""
    working = copy.deepcopy(system)
    aiming = working.get("ray_aiming")
    if not isinstance(aiming, dict):
        aiming = {}
        working["ray_aiming"] = aiming
    aiming["mode"] = "adaptive_pilot_grid"
    aiming["max_iter"] = max(40, int(finite_float(aiming.get("max_iter"), 40)))
    aiming["continuation_steps"] = max(16, int(finite_float(aiming.get("continuation_steps"), 16)))
    aiming["require_convergence"] = True
    adaptive = aiming.get("adaptive_pilot_grid")
    if not isinstance(adaptive, dict):
        adaptive = {}
        aiming["adaptive_pilot_grid"] = adaptive
    adaptive["enabled"] = True
    adaptive["require_final_convergence"] = True
    return working


def calibrated_paraxial_optimizer_system(
    exact_system: dict[str, Any], source_system: dict[str, Any]
) -> dict[str, Any]:
    """Transfer exact pupil coefficients without leaking measurement apertures.

    Set Vignetting measures a solver-local copy whose automatic clear
    apertures have been frozen to the displayed Zemax DIAM/FLAP values.  Those
    hard apertures are part of the measurement contract, not a request to
    change the optimizer's aperture semantics.  Start from the source system
    and transfer only the four field-vignetting coefficients.
    """
    working = server.vignetting_optimizer_aperture_copy(source_system)
    exact_fields = list(exact_system.get("fields") or [])
    working_fields = list(working.get("fields") or [])
    if len(exact_fields) != len(working_fields):
        raise ValueError(
            f"Set Vignetting changed the field count: {len(working_fields)} -> {len(exact_fields)}"
        )
    for target, measured in zip(working_fields, exact_fields, strict=True):
        for key in ("fvcx", "fvcy", "fvdx", "fvdy"):
            target[key] = finite_float(measured.get(key), 0.0)
    source_aiming = source_system.get("ray_aiming")
    aiming = copy.deepcopy(source_aiming) if isinstance(source_aiming, dict) else {}
    aiming["mode"] = "calibrated_paraxial"
    calibrated = aiming.get("calibrated_paraxial")
    if not isinstance(calibrated, dict):
        calibrated = {}
        aiming["calibrated_paraxial"] = calibrated
    calibrated["enabled"] = True
    calibrated["fallback"] = "none"
    calibrated["edge_calibration_enabled"] = True
    calibrated["edge_calibration_solver"] = "secant"
    working["ray_aiming"] = aiming
    return working


def adaptive_pilot_grid_optimizer_system(
    exact_system: dict[str, Any], source_system: dict[str, Any], *, _version: MaterialPolicy
) -> dict[str, Any]:
    """Transfer measured coefficients to the source aperture and select APG."""
    working = _version.calibrated_paraxial_optimizer_system(exact_system, source_system)
    return strict_adaptive_pilot_grid_system(working)


def stage_boundary_aperture_stability(
    source_system: dict[str, Any], apg_system: dict[str, Any], calibrated_system: dict[str, Any]
) -> dict[str, Any]:
    """Compare physical non-stop clear apertures after APG and CP retraces."""
    source_surfaces = list(source_system.get("surfaces") or [])
    apg_surfaces = list(apg_system.get("surfaces") or [])
    calibrated_surfaces = list(calibrated_system.get("surfaces") or [])
    if not len(source_surfaces) == len(apg_surfaces) == len(calibrated_surfaces):
        return {
            "surface_count_consistent": False,
            "source_to_apg_max_relative_change": math.inf,
            "source_to_calibrated_max_relative_change": math.inf,
            "apg_to_calibrated_max_relative_difference": math.inf,
            "surfaces": [],
        }

    def relative(left: float, right: float) -> float:
        if not (math.isfinite(left) and math.isfinite(right)):
            return math.inf
        return abs(left - right) / max(abs(left), abs(right), 1e-12)

    rows: list[dict[str, Any]] = []
    for index, (source, apg, calibrated) in enumerate(
        zip(source_surfaces, apg_surfaces, calibrated_surfaces, strict=True)
    ):
        if str(source.get("kind") or "").strip().lower() in {"object", "image"} or bool(
            source.get("is_stop", False)
        ):
            continue
        source_radius = finite_float(source.get("semi_aperture"), math.nan)
        apg_radius = finite_float(apg.get("semi_aperture"), math.nan)
        calibrated_radius = finite_float(calibrated.get("semi_aperture"), math.nan)
        rows.append(
            {
                "surface_index": index,
                "source_semi_aperture": source_radius,
                "apg_semi_aperture": apg_radius,
                "calibrated_semi_aperture": calibrated_radius,
                "source_to_apg_relative_change": relative(source_radius, apg_radius),
                "source_to_calibrated_relative_change": relative(source_radius, calibrated_radius),
                "apg_to_calibrated_relative_difference": relative(apg_radius, calibrated_radius),
            }
        )
    return {
        "surface_count_consistent": True,
        "source_to_apg_max_relative_change": max(
            (row["source_to_apg_relative_change"] for row in rows), default=0.0
        ),
        "source_to_calibrated_max_relative_change": max(
            (row["source_to_calibrated_relative_change"] for row in rows), default=0.0
        ),
        "apg_to_calibrated_max_relative_difference": max(
            (row["apg_to_calibrated_relative_difference"] for row in rows), default=0.0
        ),
        "surfaces": rows,
    }


def stage_boundary_trace_admissible(
    apg_score: dict[str, Any],
    calibrated_score: dict[str, Any],
    *,
    min_trace: float,
    min_field: float,
) -> bool:
    """APG is diagnostic; keep the existing optimizer-mode trace floors."""
    trace_coverage = finite_float(calibrated_score.get("trace_coverage"), math.nan)
    field_coverage = finite_float(calibrated_score.get("min_field_coverage"), math.nan)
    return bool(
        calibrated_score.get("trace_ok")
        and math.isfinite(trace_coverage)
        and math.isfinite(field_coverage)
        and (trace_coverage + 1e-12 >= min_trace)
        and (field_coverage + 1e-12 >= min_field)
        and server.vignetting_handoff_policy.chief_ray_usable(calibrated_score)
    )


def terminal_zemax_set_vignetting_command(
    *,
    cpu_executable: Path,
    system_path: Path,
    merit_path: Path,
    out_dir: Path,
    sampling: dict[str, Any],
) -> list[str]:
    """Build the GUI Set Vignetting module's exact APG command.

    Do not append calibrated-paraxial final-aiming validation here.  That
    optional validation contracts the measured X pupil to accommodate a
    different aiming model and therefore no longer reproduces the coefficients
    returned by the GUI module (or by Zemax-style strict ray aiming).
    """
    return [
        str(cpu_executable),
        "--system",
        str(system_path),
        "--aiming",
        "adaptive_pilot_grid",
        "--aiming-iter",
        "40",
        "--aiming-continuation-steps",
        "16",
        "--set-vignetting",
        "--set-vignetting-exact-measurement",
        "--set-vignetting-no-aiming-fallback",
        "--set-vignetting-coarse-samples",
        "33",
        "--set-vignetting-centroid-grid",
        "98",
        "--set-vignetting-boundary-iter",
        "44",
        "--out-dir",
        str(out_dir),
    ]


def materialized_stage_boundary_variables(variables: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Drop catalog-search controls once their selected glasses are materialized."""
    return [
        variable
        for variable in variables
        if str(variable.get("kind") or "").strip().lower() != "glass_choice"
    ]
