"""Production helpers extracted from run_vignetting_aperture_realized_bridge_experiment; unrelated historical entry points omitted."""

from __future__ import annotations

from lensde.resources import ProjectPaths
PROJECT_ROOT = ProjectPaths().root

import argparse

import json

import math

import re

from pathlib import Path

from lensde.helpers.run_minimal_vignetting_only_de import load_json, run_logged, write_json

from lensde.optics import dynamic_aperture_contract as dynamic_aperture_contract

from lensde.optics import vignetting_handoff_policy as vignetting_handoff_policy

from lensde.engine import core as server

DEFAULT_FIELD_NODES = (0.0, 0.25, 0.50, 0.75, 1.0)

VIGNETTING_KEYS = ("fvcx", "fvcy", "fvdx", "fvdy")

DEFAULT_SOURCE_APERTURE_RELATIVE_TOLERANCE = 0.05

DEFAULT_MODE_APERTURE_RELATIVE_TOLERANCE = 0.03

DEFAULT_COVERAGE_ABSOLUTE_TOLERANCE = 0.01

DEFAULT_MINIMUM_TRACE_COVERAGE = 0.95

def finite(value: object, default: float = 0.0) -> float:
    try:
        result = float(value)
    except (TypeError, ValueError):
        return default
    return result if math.isfinite(result) else default

def bridge_trace_admissible(
    *,
    source_fields_preserved: bool,
    set_vignetting_complete: bool,
    apg_retrace_passed: bool,
    calibrated_retrace_passed: bool,
    inherited_fields_complete: bool = False,
) -> bool:
    """APG is diagnostic; a usable initialized optimizer pupil owns handoff."""

    return (
        source_fields_preserved
        and (set_vignetting_complete or inherited_fields_complete)
        and calibrated_retrace_passed
    )

def parse_nodes(text: str) -> tuple[float, ...]:
    nodes = tuple(
        float(part.strip())
        for part in str(text or "").replace(";", ",").split(",")
        if part.strip()
    )
    if not nodes:
        return DEFAULT_FIELD_NODES
    if any(not math.isfinite(node) or node < 0.0 for node in nodes):
        raise ValueError("field nodes must be finite and non-negative")
    if tuple(sorted(set(nodes))) != nodes or abs(nodes[0]) > 1.0e-12:
        raise ValueError("field nodes must be unique, increasing, and begin at zero")
    return nodes

def zero_fields(nodes: tuple[float, ...]) -> list[dict]:
    return [
        {
            "hx": 0.0,
            "hy": float(node),
            "fvcx": 0.0,
            "fvcy": 0.0,
            "fvdx": 0.0,
            "fvdy": 0.0,
            "vignette_x": 0.0,
            "vignette_y": 0.0,
        }
        for node in nodes
    ]

def bridge_merit(
    source: dict,
    minimum_relative_illumination: float,
    aperture_margin: float,
) -> dict:
    merit = json.loads(json.dumps(source))
    merit["minimum_relative_illumination"] = minimum_relative_illumination
    merit["num_rays"] = 6
    merit["pupil_arms"] = 8
    merit["pupil_distribution"] = "gaussian_ring_arm"
    dynamic_aperture_contract.apply_merit(merit)
    merit["clear_aperture_margin"] = aperture_margin
    return merit

def aperture_rows(before: dict, after: dict) -> list[dict]:
    rows: list[dict] = []
    for index, (old, new) in enumerate(zip(before.get("surfaces", []), after.get("surfaces", []))):
        rows.append(
            {
                "surface_index": index,
                "kind": new.get("kind"),
                "is_stop": bool(new.get("is_stop", False)),
                "semi_aperture_before": finite(old.get("semi_aperture")),
                "semi_aperture_materialized": finite(new.get("semi_aperture")),
                "mechanical_aperture_materialized": finite(new.get("mechanical_aperture")),
                "aperture_radius": finite(new.get("aperture_radius")),
                "clear_aperture_fixed": bool(new.get("clear_aperture_fixed", False)),
            }
        )
    return rows

def retrace_system(system: dict, mode: str) -> dict:
    """Select one strict aiming mode without changing calibrated coefficients."""

    working = json.loads(json.dumps(system))
    aiming = working.get("ray_aiming")
    if not isinstance(aiming, dict):
        aiming = {}
        working["ray_aiming"] = aiming
    aiming["mode"] = mode
    if mode == "adaptive_pilot_grid":
        aiming["require_convergence"] = True
        aiming["max_iter"] = max(40, int(finite(aiming.get("max_iter"), 40)))
        aiming["continuation_steps"] = max(
            16, int(finite(aiming.get("continuation_steps"), 16))
        )
        adaptive = aiming.get("adaptive_pilot_grid")
        if not isinstance(adaptive, dict):
            adaptive = {}
            aiming["adaptive_pilot_grid"] = adaptive
        adaptive["enabled"] = True
        adaptive["require_final_convergence"] = True
    elif mode == "calibrated_paraxial":
        calibrated = aiming.get("calibrated_paraxial")
        if not isinstance(calibrated, dict):
            calibrated = {}
            aiming["calibrated_paraxial"] = calibrated
        calibrated["enabled"] = True
        calibrated["fallback"] = "none"
        calibrated["edge_calibration_enabled"] = True
        calibrated["edge_calibration_solver"] = "secant"
    return working

def aperture_stability(
    source: dict,
    apg: dict,
    calibrated: dict,
) -> dict:
    """Compare only physical non-stop optical-surface clear apertures."""

    source_surfaces = list(source.get("surfaces") or [])
    apg_surfaces = list(apg.get("surfaces") or [])
    calibrated_surfaces = list(calibrated.get("surfaces") or [])
    if not (
        len(source_surfaces) == len(apg_surfaces) == len(calibrated_surfaces)
    ):
        return {
            "surface_count_consistent": False,
            "source_to_apg_max_relative_change": math.inf,
            "source_to_calibrated_max_relative_change": math.inf,
            "apg_to_calibrated_max_relative_difference": math.inf,
            "surfaces": [],
        }
    rows: list[dict] = []
    for index, (before, apg_surface, calibrated_surface) in enumerate(
        zip(source_surfaces, apg_surfaces, calibrated_surfaces, strict=True)
    ):
        if (
            str(before.get("kind") or "").lower() in {"object", "image"}
            or bool(before.get("is_stop", False))
        ):
            continue
        source_radius = finite(before.get("semi_aperture"))
        apg_radius = finite(apg_surface.get("semi_aperture"))
        calibrated_radius = finite(calibrated_surface.get("semi_aperture"))

        def relative(left: float, right: float) -> float:
            if not (math.isfinite(left) and math.isfinite(right)):
                return math.inf
            return abs(left - right) / max(abs(left), abs(right), 1.0e-12)

        rows.append({
            "surface_index": index,
            "source_semi_aperture": source_radius,
            "apg_semi_aperture": apg_radius,
            "calibrated_semi_aperture": calibrated_radius,
            "source_to_apg_relative_change": relative(source_radius, apg_radius),
            "source_to_calibrated_relative_change": relative(
                source_radius, calibrated_radius
            ),
            "apg_to_calibrated_relative_difference": relative(
                apg_radius, calibrated_radius
            ),
        })
    return {
        "surface_count_consistent": True,
        "source_to_apg_max_relative_change": max(
            (row["source_to_apg_relative_change"] for row in rows),
            default=0.0,
        ),
        "source_to_calibrated_max_relative_change": max(
            (row["source_to_calibrated_relative_change"] for row in rows),
            default=0.0,
        ),
        "apg_to_calibrated_max_relative_difference": max(
            (row["apg_to_calibrated_relative_difference"] for row in rows),
            default=0.0,
        ),
        "surfaces": rows,
    }

def field_signature(system: dict) -> list[dict]:
    return [
        {
            "hx": finite(field.get("hx")),
            "hy": finite(field.get("hy")),
            **{key: finite(field.get(key)) for key in VIGNETTING_KEYS},
        }
        for field in system.get("fields", [])
    ]

def hard_aperture_copy(materialized: dict) -> dict:
    return server.zemax_set_vignetting_hard_aperture_copy(materialized)[0]

def hard_aperture_target_copy(materialized: dict, nodes: tuple[float, ...]) -> dict:
    working = hard_aperture_copy(materialized)
    # Target nodes are introduced only after the old-field aperture contract is frozen.
    working["fields"] = zero_fields(nodes)
    return working

def with_solved_fields(base: dict, solved: dict, *, y_only: bool) -> dict:
    realized = json.loads(json.dumps(base))
    realized["fields"] = json.loads(json.dumps(solved.get("fields", [])))
    if y_only:
        for field in realized["fields"]:
            # X is not an optimization variable downstream, but it remains a
            # fixed part of the aperture-realized pupil map.  Clearing the X
            # factors after a coupled Set Vignetting solve changes the four
            # marginal rays and can invalidate an otherwise traceable field.
            field["vignette_x"] = finite(field.get("fvdx"))
            if math.hypot(finite(field.get("hx")), finite(field.get("hy"))) <= 1.0e-12:
                field["fvcx"] = 0.0
                field["fvdx"] = 0.0
                field["vignette_x"] = 0.0
                field["fvcy"] = 0.0
                field["fvdy"] = 0.0
                field["vignette_y"] = 0.0
    return realized

def restore_source_field_coefficients(target: dict, source: dict) -> tuple[dict, int, int]:
    """Retain known pupils exactly; never interpolate or invent new fields."""
    for field in [*target.get("fields", []), *source.get("fields", [])]:
        for key in ("hx", "hy", *VIGNETTING_KEYS):
            if not math.isfinite(float(field.get(key, 0.0))):
                raise ValueError(f"non-finite field parameter {key}")
    restored = json.loads(json.dumps(target))
    matched = changed = 0
    for field in restored.get("fields", []):
        original = next((item for item in source.get("fields", []) if all(
            abs(finite(item.get(key)) - finite(field.get(key))) <= 1.0e-10
            for key in ("hx", "hy")
        )), None)
        if original is None:
            continue
        matched += 1
        if any(finite(field.get(key)) != finite(original.get(key)) for key in VIGNETTING_KEYS):
            changed += 1
        for key in VIGNETTING_KEYS:
            field[key] = finite(original.get(key))
        for key in ("vignette_x", "vignette_y"):
            if key in original:
                field[key] = original[key]
    return restored, matched, changed

def parse_limiting_surfaces(stdout: str) -> list[dict]:
    pattern = re.compile(
        r"field\s+(\d+)\s+fvcx=([^\s]+)\s+fvcy=([^\s]+)\s+"
        r"fvdx=([^\s]+)\s+fvdy=([^\s]+)\s+limits\(x- x\+ y- y\+\)="
        r"\s*(-?\d+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)"
        r"(?:\s+x_contraction=([^\s]+)\s+contraction=([^\s]+))?"
    )
    rows: list[dict] = []
    for match in pattern.finditer(stdout):
        rows.append(
            {
                "field_index": int(match.group(1)),
                "fvcx": finite(match.group(2)),
                "fvcy": finite(match.group(3)),
                "fvdx": finite(match.group(4)),
                "fvdy": finite(match.group(5)),
                "x_min_limiting_surface": int(match.group(6)),
                "x_max_limiting_surface": int(match.group(7)),
                "y_min_limiting_surface": int(match.group(8)),
                "y_max_limiting_surface": int(match.group(9)),
                "pupil_x_contraction": finite(match.group(10), 1.0),
                "pupil_contraction": finite(match.group(11), 1.0),
            }
        )
    return rows

def main() -> int:
    root = PROJECT_ROOT / "native"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-system", type=Path, required=True)
    parser.add_argument("--source-merit", type=Path, required=True)
    parser.add_argument(
        "--source-variables",
        type=Path,
        help=(
            "Optional optimization-variable contract used only to scope "
            "manufacturing constraints in diagnostic scores."
        ),
    )
    parser.add_argument("--executable", type=Path, default=root / "build/lensde_cpu.exe")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument(
        "--field-nodes",
        default=",".join(str(value) for value in DEFAULT_FIELD_NODES),
    )
    parser.add_argument("--minimum-relative-illumination", type=float, default=0.40)
    parser.add_argument("--centroid-grid", type=int, default=98)
    parser.add_argument("--coarse-samples", type=int, default=33)
    parser.add_argument("--boundary-iterations", type=int, default=44)
    parser.add_argument("--aperture-margin", type=float, default=1.02)
    parser.add_argument(
        "--source-aperture-relative-tolerance",
        type=float,
        default=DEFAULT_SOURCE_APERTURE_RELATIVE_TOLERANCE,
    )
    parser.add_argument(
        "--mode-aperture-relative-tolerance",
        type=float,
        default=DEFAULT_MODE_APERTURE_RELATIVE_TOLERANCE,
    )
    parser.add_argument(
        "--coverage-absolute-tolerance",
        type=float,
        default=DEFAULT_COVERAGE_ABSOLUTE_TOLERANCE,
    )
    parser.add_argument(
        "--minimum-trace-coverage",
        type=float,
        default=DEFAULT_MINIMUM_TRACE_COVERAGE,
    )
    parser.add_argument(
        "--minimum-field-coverage",
        type=float,
        default=DEFAULT_MINIMUM_TRACE_COVERAGE,
    )
    parser.add_argument(
        "--set-vignetting-aiming",
        choices=(
            "source",
            "gui_default",
            "direct",
            "paraxial",
            "iterative",
            "robust",
            "adaptive_pilot_grid",
            "calibrated_paraxial",
            "three_step",
        ),
        default="source",
        help=(
            "Use the source/project reference pupil (recommended), or explicitly "
            "select a mode for a mode-matrix experiment. gui_default is retained "
            "as a compatibility alias for source because the GUI no longer "
            "overrides the project mode."
        ),
    )
    args = parser.parse_args()

    source_path = args.source_system.resolve()
    source_merit_path = args.source_merit.resolve()
    source_variables_path = (
        args.source_variables.resolve() if args.source_variables else None
    )
    executable = args.executable.resolve()
    output = server.native_io_paths.python_path(args.output_dir.resolve())
    nodes = parse_nodes(args.field_nodes)
    minimum = min(1.0, max(0.0, finite(args.minimum_relative_illumination)))
    aperture_margin = max(1.0, finite(args.aperture_margin, 1.02))
    required_paths = [source_path, source_merit_path, executable]
    if source_variables_path is not None:
        required_paths.append(source_variables_path)
    for required in required_paths:
        if not required.exists():
            raise FileNotFoundError(required)
    if not (0.0 < minimum <= 1.0):
        raise ValueError("minimum relative illumination must be in (0, 1]")

    source = load_json(source_path)
    source_fields = field_signature(source)
    source_aiming = source.get("ray_aiming")
    if not isinstance(source_aiming, dict):
        source_aiming = {}
    source_aiming_mode = str(source_aiming.get("mode") or "paraxial")
    solve_aiming_mode = (
        source_aiming_mode
        if args.set_vignetting_aiming == "source"
        else "adaptive_pilot_grid"
        if args.set_vignetting_aiming == "gui_default"
        else args.set_vignetting_aiming
    )
    # Honor the selected calibration mode (including ordinary CP). Its output
    # coefficients must not be contracted for another optimizer aiming mode.
    solve_aiming_iter = max(40, int(finite(source_aiming.get("max_iter"), 20)))
    solve_continuation = max(
        16, int(finite(source_aiming.get("continuation_steps"), 8))
    )
    merit_path = output / "bridge_merit.json"
    write_json(
        merit_path,
        bridge_merit(load_json(source_merit_path), minimum, aperture_margin),
    )

    # This script is used only when minimum relative illumination is enabled.
    # In that contract the incoming system already owns its physical apertures;
    # changing them while expanding the field grid would change the optical
    # problem instead of merely calibrating the five field pupils.
    materialized = json.loads(json.dumps(source))
    materialized_path = output / "aperture_materialized_source_system.json"
    write_json(materialized_path, materialized)
    source_fields_preserved = field_signature(materialized) == source_fields
    write_json(
        output / "aperture_contract.json",
        {
            "contract": "vignetting_existing_physical_aperture_reuse_v1",
            "source": str(source_path),
            "source_field_count": len(source_fields),
            "source_fields": source_fields,
            "source_fields_preserved": source_fields_preserved,
            "target_fields_excluded_from_aperture_update": True,
            "aperture_source": "incoming_system_existing_physical_apertures",
            "clear_aperture_update_performed": False,
            "margin_factor_applied": 1.0,
            "surfaces": aperture_rows(source, materialized),
        },
    )

    hard_target = hard_aperture_target_copy(materialized, nodes)
    hard_target_path = output / "hard_aperture_target_fields.json"
    write_json(hard_target_path, hard_target)
    solve_dir = server.native_io_paths.named_child(output, 'set_vignetting')
    variable_scope_args = (
        ["--variables", str(source_variables_path)]
        if source_variables_path is not None
        else []
    )
    measurement_error = None
    try:
        run_logged(
            [
                str(executable),
                "--system", str(hard_target_path),
                "--merit", str(merit_path),
                *variable_scope_args,
                "--aiming", solve_aiming_mode,
                "--aiming-iter", str(solve_aiming_iter),
                "--aiming-continuation-steps", str(solve_continuation),
                "--set-vignetting",
                "--set-vignetting-exact-measurement",
                "--set-vignetting-no-aiming-fallback",
                "--set-vignetting-coarse-samples", str(max(5, args.coarse_samples)),
                "--set-vignetting-centroid-grid", str(max(4, args.centroid_grid)),
                "--set-vignetting-boundary-iter", str(max(1, args.boundary_iterations)),
                "--score-only",
                "--aiming-diagnostics",
                "--out-dir", str(solve_dir),
            ],
            root,
            solve_dir,
        )
    except RuntimeError as exc:
        if not vignetting_handoff_policy.optical_measurement_failure(solve_dir):
            raise
        inherited, matched, _ = restore_source_field_coefficients(hard_target, source)
        if matched != len(nodes):
            raise RuntimeError("new field pupils could not be initialized") from exc
        measurement_error = str(exc)
        solved = inherited
    else:
        solved = load_json(solve_dir / "system.json")
    rows = parse_limiting_surfaces(
        (solve_dir / "stdout.log").read_text(encoding="utf-8")
    )
    full_realized = with_solved_fields(materialized, solved, y_only=False)
    full_realized_path = output / "aperture_realized_full_xy_system.json"
    write_json(full_realized_path, full_realized)

    # Restore optimizer aperture semantics after calibration in either mode.
    # Protective glass remains non-limiting in APG diagnostics and CP scoring.
    cp_source = server.vignetting_optimizer_aperture_copy(full_realized)
    apg_input = retrace_system(cp_source, "adaptive_pilot_grid")
    calibrated_input = retrace_system(cp_source, "calibrated_paraxial")
    apg_input_path = output / "apg_retrace_input_system.json"
    calibrated_input_path = output / "calibrated_retrace_input_system.json"
    write_json(apg_input_path, apg_input)
    write_json(calibrated_input_path, calibrated_input)

    def retrace_command(system_path: Path, mode: str, out_dir: Path) -> list[str]:
        aiming_options = [
            "--aiming-iter", str(solve_aiming_iter),
            "--aiming-continuation-steps", str(solve_continuation),
        ] if mode == "adaptive_pilot_grid" else []
        return [
            str(executable),
            "--system", str(system_path),
            "--merit", str(merit_path),
            *variable_scope_args,
            "--cpu",
            "--aiming", mode,
            *aiming_options,
            *(["--aiming-require-convergence"] if mode == "adaptive_pilot_grid" else []),
            "--score-only",
            "--rays", "6",
            "--arms", "8",
            "--clear-aperture-update", "--sync-stop-aperture",
            "--clear-rings", "4",
            "--no-vignetting-merit-aperture-envelope-experimental",
            "--distribution", "gaussian_ring_arm",
            "--sampling-seed", "1",
            "--out-dir", str(out_dir),
        ]

    apg_retrace_dir = server.native_io_paths.named_child(output, 'apg_retrace')
    calibrated_retrace_dir = server.native_io_paths.named_child(output, 'calibrated_paraxial_retrace')
    run_logged(
        retrace_command(apg_input_path, "adaptive_pilot_grid", apg_retrace_dir),
        root,
        apg_retrace_dir,
    )
    run_logged(
        retrace_command(
            calibrated_input_path,
            "calibrated_paraxial",
            calibrated_retrace_dir,
        ),
        root,
        calibrated_retrace_dir,
    )
    apg_realized = load_json(apg_retrace_dir / "system.json")
    calibrated_realized = load_json(calibrated_retrace_dir / "system.json")
    set_vignetting_score = (
        load_json(solve_dir / "score.json") if measurement_error is None else {}
    )
    apg_score = load_json(apg_retrace_dir / "score.json")
    calibrated_score = load_json(calibrated_retrace_dir / "score.json")

    min_trace = min(1.0, max(0.0, finite(args.minimum_trace_coverage, 0.95)))
    min_field = min(1.0, max(0.0, finite(args.minimum_field_coverage, 0.95)))
    source_pupil_retained = measurement_error is not None
    restored_field_count = len(nodes) if source_pupil_retained else 0
    proposed_calibrated_score = calibrated_score
    authoritative_cp_dir = calibrated_retrace_dir
    if not vignetting_handoff_policy.score_usable(
        calibrated_score, min_trace=min_trace, min_field=min_field
    ):
        restored, matched, changed = restore_source_field_coefficients(calibrated_input, source)
        if changed:
            # Only on a declined proposal: check retained source coefficients
            # under the existing CP contract. New nodes keep their solved pupils.
            fallback_input = output / "fallback_input.json"
            authoritative_cp_dir = output / "fallback_cp"
            write_json(fallback_input, restored)
            run_logged(retrace_command(fallback_input, "calibrated_paraxial", authoritative_cp_dir), root, authoritative_cp_dir)
            calibrated_realized = load_json(authoritative_cp_dir / "system.json")
            calibrated_score = load_json(authoritative_cp_dir / "score.json")
            restored_field_count = matched
            source_pupil_retained = matched == len(nodes)

    # Initialize every target field first, then offer a physical stage trim.
    # The already usable CP input remains paired with its score on a decline.
    trim_measurement, illumination_trim = server.stage_illumination_trim.suggest(
        server, calibrated_realized, calibrated_score, load_json(merit_path),
        {"system": calibrated_realized}, server.native_io_paths.named_child(output, 'illumination_trim'), executable,
        retrace_command(calibrated_input_path, "calibrated_paraxial", calibrated_retrace_dir),
    )
    if illumination_trim["performed"]:
        selected = illumination_trim["trials"][illumination_trim["selected_trial"]-1]
        authoritative_cp_dir = output / "trimmed_cp"
        server.copy_candidate_artifacts(Path(selected["dynamic_output_dir"]), authoritative_cp_dir)
        calibrated_realized = load_json(authoritative_cp_dir / "system.json")
        calibrated_score = load_json(authoritative_cp_dir / "score.json")
        proposed_calibrated_score = calibrated_score
        full_realized = server.post_asphere_optimizer_system_from_measurement(
            full_realized, trim_measurement["system"])
        write_json(full_realized_path, full_realized)
        apg_input = retrace_system(full_realized, "adaptive_pilot_grid")
        apg_input_path = output / "trimmed_apg_input.json"
        write_json(apg_input_path, apg_input)
        apg_retrace_dir = output / "trimmed_apg"
        run_logged(retrace_command(apg_input_path, "adaptive_pilot_grid", apg_retrace_dir), root, apg_retrace_dir)
        apg_realized = load_json(apg_retrace_dir / "system.json")
        apg_score = load_json(apg_retrace_dir / "score.json")
        set_vignetting_score = trim_measurement["set_vignetting_score"]
        rows = parse_limiting_surfaces((server.native_io_paths.named_child(Path(trim_measurement["output_dir"]), 'set_vignetting') / 'stdout.log').read_text(encoding="utf-8"))
        source_pupil_retained = False

    # Retain the historical filename as an upper-layer compatibility alias.
    # Measurement uses the incoming physical apertures. The optimizer handoff
    # instead owns the CP dynamic-4x8 system AND its complete dynamic score.
    y_only_realized_path = output / "aperture_realized_y_only_system.json"
    write_json(y_only_realized_path, calibrated_realized)

    max_abs_fvcx = max((abs(row["fvcx"]) for row in rows), default=math.inf)
    max_fvdx = max((row["fvdx"] for row in rows), default=math.inf)
    set_vignetting_complete = measurement_error is None and len(rows) == len(nodes) and all(
        all(math.isfinite(finite(row.get(key), math.nan)) for key in VIGNETTING_KEYS)
        for row in rows
    )
    aperture_comparison = aperture_stability(
        full_realized,
        apg_realized,
        calibrated_realized,
    )
    source_aperture_tolerance = max(
        0.0, finite(args.source_aperture_relative_tolerance)
    )
    mode_aperture_tolerance = max(
        0.0, finite(args.mode_aperture_relative_tolerance)
    )
    coverage_tolerance = max(0.0, finite(args.coverage_absolute_tolerance))
    minimum_trace_coverage = min(
        1.0, max(0.0, finite(args.minimum_trace_coverage, 0.95))
    )
    minimum_field_coverage = min(
        1.0, max(0.0, finite(args.minimum_field_coverage, 0.95))
    )
    apg_trace_coverage = finite(apg_score.get("trace_coverage"), 0.0)
    calibrated_trace_coverage = finite(
        calibrated_score.get("trace_coverage"), 0.0
    )
    apg_field_coverage = finite(apg_score.get("min_field_coverage"), 0.0)
    calibrated_field_coverage = finite(
        calibrated_score.get("min_field_coverage"), 0.0
    )
    trace_coverage_difference = abs(
        apg_trace_coverage - calibrated_trace_coverage
    )
    field_coverage_difference = abs(
        apg_field_coverage - calibrated_field_coverage
    )
    apg_retrace_passed = (
        bool(apg_score.get("trace_ok"))
        and apg_trace_coverage >= minimum_trace_coverage
        and apg_field_coverage >= minimum_field_coverage
    )
    calibrated_retrace_passed = vignetting_handoff_policy.score_usable(
        calibrated_score, min_trace=minimum_trace_coverage,
        min_field=minimum_field_coverage,
    )
    source_aperture_change_within_diagnostic_tolerance = (
        aperture_comparison["source_to_apg_max_relative_change"]
        <= source_aperture_tolerance
        and aperture_comparison["source_to_calibrated_max_relative_change"]
        <= source_aperture_tolerance
    )
    aperture_stable = (
        bool(aperture_comparison["surface_count_consistent"])
        and aperture_comparison["apg_to_calibrated_max_relative_difference"]
        <= mode_aperture_tolerance
    )
    coverage_consistent = (
        trace_coverage_difference <= coverage_tolerance
        and field_coverage_difference <= coverage_tolerance
    )
    acceptance = {
        "source_fields_preserved_during_materialization": source_fields_preserved,
        "set_vignetting_solved_every_target_field": set_vignetting_complete,
        "inherited_fields_complete": source_pupil_retained,
        "apg_retrace_is_diagnostic_not_admission": True,
        "apg_retrace_with_aperture_update_passed": apg_retrace_passed,
        "calibrated_retrace_with_aperture_update_passed": (
            calibrated_retrace_passed
        ),
        "aperture_stable": aperture_stable,
        "source_aperture_change_within_diagnostic_tolerance": (
            source_aperture_change_within_diagnostic_tolerance
        ),
        "coverage_consistent": coverage_consistent,
        "coverage_difference_is_diagnostic_not_admission": True,
        "mode_aperture_difference_is_diagnostic_not_admission": True,
        "source_aperture_relative_tolerance": source_aperture_tolerance,
        "mode_aperture_relative_tolerance": mode_aperture_tolerance,
        "coverage_absolute_tolerance": coverage_tolerance,
        "minimum_trace_coverage": minimum_trace_coverage,
        "minimum_field_coverage": minimum_field_coverage,
        "apg_trace_coverage": apg_trace_coverage,
        "calibrated_trace_coverage": calibrated_trace_coverage,
        "trace_coverage_absolute_difference": trace_coverage_difference,
        "apg_min_field_coverage": apg_field_coverage,
        "calibrated_min_field_coverage": calibrated_field_coverage,
        "min_field_coverage_absolute_difference": field_coverage_difference,
    }
    passed = bridge_trace_admissible(
        source_fields_preserved=acceptance[
            "source_fields_preserved_during_materialization"
        ],
        set_vignetting_complete=acceptance[
            "set_vignetting_solved_every_target_field"
        ],
        apg_retrace_passed=apg_retrace_passed,
        calibrated_retrace_passed=calibrated_retrace_passed,
        inherited_fields_complete=source_pupil_retained,
    )
    comparison = {
        "experiment": "vignetting_sparse_to_dense_full_except_cover_dynamic_4x8_bridge_v17",
        "passed": passed,
        "calibration_diagnostics_passed": set_vignetting_complete and apg_retrace_passed,
        "fallback_used": bool(restored_field_count),
        "source_pupil_retained": source_pupil_retained,
        "source_coefficients_restored_field_count": restored_field_count,
        "measurement_error": measurement_error,
        "bridge_policy": {
            "source_existing_physical_apertures_reused": True,
            "aperture_materialization_performed": False,
            "target_field_hy_interpolation": False,
            "target_field_solver": "mode_specific_exact_set_vignetting",
            "set_vignetting_measurement_aiming": solve_aiming_mode,
            "set_vignetting_primary_wavelength_only": True,
            "set_vignetting_all_wavelengths": False,
            "set_vignetting_requires_full_trace": False,
            "set_vignetting_per_ray_fallback_allowed": False,
            # The exact measurement deliberately places an edge ray on a hard
            # boundary.  Its ordinary score may therefore trip a non-finite
            # single-ray sentinel even though measurement and coverage pass.
            # Keep that score visible, but never present it as the handoff
            # quality score; the dynamic CP retrace is the optimizer baseline.
            "set_vignetting_measurement_score_is_authoritative": False,
            "authoritative_handoff_score_stage": authoritative_cp_dir.name,
            "apg_retrace_is_diagnostic_not_admission": True,
            "cp_per_ray_convergence_gate": False,
            "diagnostic_merit_uses_source_variable_scope": (
                source_variables_path is not None
            ),
            "final_handoff_aiming_fallback_allowed": False,
            "post_measurement_projection_or_contraction": False,
            "post_measurement_retrace_modes": [
                "adaptive_pilot_grid",
                "calibrated_paraxial",
            ],
            "post_measurement_clear_aperture_update": True,
            "optimizer_aperture_contract": dynamic_aperture_contract.CONTRACT,
            "all_displayed_apertures_frozen_except_protective_glass": True,
            "excluded_surface_indices": server.protective_glass_surface_indices(materialized),
            "admission_uses_merit": False,
            "admission_uses_aperture_stability_and_coverage_only": False,
            "admission_uses_trace_coverage_not_mode_aperture_parity": True,
            "mode_aperture_difference_is_diagnostic_not_admission": True,
            "coverage_difference_is_diagnostic_not_admission": True,
            "source_aperture_change_is_diagnostic_not_admission": True,
            "source_aiming_mode": source_aiming_mode,
            "materialization_aiming_mode": solve_aiming_mode,
            "set_vignetting_aiming_policy": args.set_vignetting_aiming,
            "set_vignetting_aiming_mode": solve_aiming_mode,
        },
        "illumination_margin_trim": illumination_trim,
        "field_radius_nodes": list(nodes),
        "minimum_relative_illumination": minimum,
        "aperture_margin": aperture_margin,
        "set_vignetting": {
            "fields": rows,
            "maximum_absolute_fvcx": max_abs_fvcx,
            "maximum_fvdx": max_fvdx,
            "centroid_grid": max(4, args.centroid_grid),
            "coarse_samples": max(5, args.coarse_samples),
            "boundary_iterations": max(1, args.boundary_iterations),
            "require_full_trace": False,
            "all_wavelengths": False,
        },
        "acceptance": acceptance,
        "aperture_stability": aperture_comparison,
        "retrace_evidence": {
            "set_vignetting_exact_boundary_measurement": {
                "role": "coefficient_measurement_only",
                "merit_is_authoritative": False,
                "merit": set_vignetting_score.get("merit"),
                "trace_ok": set_vignetting_score.get("trace_ok"),
                "trace_coverage": set_vignetting_score.get("trace_coverage"),
                "min_field_coverage": set_vignetting_score.get(
                    "min_field_coverage"
                ),
                "large_merit_may_be_exact_boundary_single_ray_sentinel": True,
            },
            "adaptive_pilot_grid": {
                "role": "authoritative_post_measurement_retrace",
                "trace_ok": apg_score.get("trace_ok"),
                "trace_coverage": apg_trace_coverage,
                "min_field_coverage": apg_field_coverage,
                "merit": apg_score.get("merit"),
            },
            "calibrated_paraxial": {
                "role": "authoritative_handoff_retrace",
                "trace_ok": calibrated_score.get("trace_ok"),
                "trace_coverage": calibrated_trace_coverage,
                "min_field_coverage": calibrated_field_coverage,
                "merit": calibrated_score.get("merit"),
                "proposed_score_before_fallback": proposed_calibrated_score,
            },
        },
        "outputs": {
            "aperture_materialized_source_system": str(materialized_path),
            "aperture_contract": str(output / "aperture_contract.json"),
            "hard_aperture_target_fields": str(hard_target_path),
            "aperture_realized_full_xy_system": str(full_realized_path),
            "apg_retrace_system": str(apg_retrace_dir / "system.json"),
            "calibrated_retrace_system": str(
                authoritative_cp_dir / "system.json"
            ),
            "aperture_realized_y_only_system": str(y_only_realized_path),
            "bridge_comparison": str(output / "bridge_comparison.json"),
        },
    }
    write_json(output / "bridge_comparison.json", comparison)
    print(json.dumps(comparison, ensure_ascii=False, indent=2))
    return 0 if passed else 1

if __name__ == "__main__":
    raise SystemExit(main())
