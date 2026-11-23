"""Construct the explicit three-field geometry reference consumed by layer-one restarts."""
from __future__ import annotations
from typing import Any
from lensde.helpers import run_explore_dls_feedback_cycles as feedback_cycles
from .common import field_radius
import json
from .contracts import MaterialPolicy
from pathlib import Path
from .common import ancestor_file_paths
import argparse
from .common import first_existing
from .common import read_json
from lensde.workflow import run_post_de_basin_refine_funnel as material_funnel
from .common import context_paths
from .common import max_local_iter_per_restart
from .common import validated_local_iter
from .common import variable_list
from .common import write_json


def three_field_system(system: dict[str, Any], field_hy_fractions: str = "") -> dict[str, Any]:
    if str(field_hy_fractions or "").strip():
        return feedback_cycles.with_field_hy_fractions(system, field_hy_fractions)
    out = json.loads(json.dumps(system))
    fields = [field for field in out.get("fields", []) if isinstance(field, dict)]
    if len(fields) <= 3 and fields:
        out["fields"] = fields
        return out
    if not fields:
        out["fields"] = [
            {"hx": 0.0, "hy": 0.0, "fvcx": 0.0, "fvcy": 0.0, "fvdx": 0.0, "fvdy": 0.0},
            {"hx": 0.0, "hy": 0.707, "fvcx": 0.0, "fvcy": 0.0, "fvdx": 0.0, "fvdy": 0.0},
            {"hx": 0.0, "hy": 1.0, "fvcx": 0.0, "fvcy": 0.0, "fvdx": 0.0, "fvdy": 0.0},
        ]
        return out
    ordered = sorted(fields, key=field_radius)
    edge = ordered[-1]
    edge_radius = max(field_radius(edge), 1e-12)
    center = ordered[0]
    mid_target = 0.707 * edge_radius
    mid = min(ordered, key=lambda field: abs(field_radius(field) - mid_target))
    selected: list[dict[str, Any]] = []
    for field in (center, mid, edge):
        if not any((field is existing for existing in selected)):
            selected.append(field)
    if len(selected) < 3:
        for field in ordered:
            if not any((field is existing for existing in selected)):
                selected.append(field)
            if len(selected) >= 3:
                break
    out["fields"] = selected[:3]
    return out


def load_reference_context(
    args: argparse.Namespace, basin_run_dir: Path, *, _version: MaterialPolicy
) -> tuple[list[str], dict[str, Any], Path]:
    run_meta: dict[str, Any] = {}
    reference_command: list[str] = []
    try:
        run_meta_path = first_existing(ancestor_file_paths(basin_run_dir, "run_meta.json"))
        run_meta = read_json(run_meta_path)
        reference_command = (
            run_meta.get("command") if isinstance(run_meta.get("command"), list) else []
        )
    except FileNotFoundError:
        try:
            command_path = first_existing(ancestor_file_paths(basin_run_dir, "command.json"))
            command_data = read_json(command_path)
            if isinstance(command_data, list):
                reference_command = [str(item) for item in command_data]
            run_meta = {"command": reference_command, "command_source": str(command_path)}
        except FileNotFoundError:
            reference_command = []
            run_meta = {"command": [], "command_source": "generated_default"}
    exe = (
        args.exe.resolve()
        if args.exe
        else Path(
            str(
                run_meta.get("executable")
                or (
                    reference_command[0]
                    if reference_command
                    else _version.cpp_dir / "build" / "lensde_cpu.exe"
                )
            )
        )
    )
    if not exe.is_absolute():
        exe = (_version.cpp_dir / exe).resolve()
    return (reference_command, run_meta, exe)


def geometry_variables(
    system: dict[str, Any], source_variables: list[dict[str, Any]], kinds: set[str]
) -> tuple[list[dict[str, Any]], int]:
    variables: list[dict[str, Any]] = []
    out_of_bounds = 0
    for variable in source_variables:
        kind = str(variable.get("kind") or "")
        if kind not in kinds:
            continue
        item, bad = material_funnel.make_variable_from_template(system, variable)
        variables.append(item)
        out_of_bounds += int(bad)
    return (variables, out_of_bounds)


def make_three_field_geometry_reference(
    args: argparse.Namespace, *, _version: MaterialPolicy
) -> Path:
    reference = args.out_dir.resolve() / "_reference_3field_geometry_dls"
    reference.mkdir(parents=True, exist_ok=True)
    system_path, variables_path, merit_path = context_paths(args.basin_run_dir.resolve())
    system = three_field_system(
        read_json(system_path), str(getattr(args, "geometry_field_hy_fractions", "") or "")
    )
    source_variables = variable_list(variables_path)
    kinds = {
        part.strip() for part in args.geometry_kinds.replace(";", ",").split(",") if part.strip()
    }
    variables, out_of_bounds = geometry_variables(system, source_variables, kinds)
    merit = read_json(
        args.geometry_merit_template.resolve() if args.geometry_merit_template else merit_path
    )
    if isinstance(merit, dict):
        merit = dict(merit)
        merit.pop("glass_proximity", None)
    reference_command, run_meta, exe = load_reference_context(
        args, args.basin_run_dir.resolve(), _version=_version
    )
    command = material_funnel.sanitize_reference_command(reference_command, exe)
    for flag in ("--clear-aperture-update", "--sync-stop-aperture"):
        if flag not in command:
            command.append(flag)
    material_funnel.set_arg(command, "--system", str(reference / "input_system.json"))
    material_funnel.set_arg(command, "--merit", str(reference / "merit.json"))
    material_funnel.set_arg(command, "--variables", str(reference / "variables.json"))
    material_funnel.set_arg(command, "--out-dir", str(reference))
    max_iter = max_local_iter_per_restart(args)
    potential_local_iter = validated_local_iter(
        args.potential_local_iter, name="potential_local_iter", max_iter=max_iter
    )
    material_funnel.set_arg(command, "--local-iter", str(potential_local_iter))
    material_funnel.set_arg(command, "--local-lambda", f"{args.potential_local_lambda:.17g}")
    material_funnel.set_arg(command, "--local-fd", f"{args.potential_local_fd:.17g}")
    material_funnel.set_arg(command, "--local-tol", f"{args.potential_local_tol:.17g}")
    material_funnel.set_arg(command, "--local-max-reject", str(args.potential_local_max_reject))
    material_funnel.set_arg(command, "--local-method", args.local_method)
    material_funnel.set_arg(command, "--local-bounds-mode", args.local_bounds_mode)
    material_funnel.set_arg(command, "--local-residual-mode", args.local_residual_mode)
    material_funnel.set_arg(
        command, "--optimizer-internal-scaling", args.optimizer_internal_scaling
    )
    material_funnel.set_arg(command, "--asphere-reference-policy", "current")
    write_json(reference / "input_system.json", system)
    write_json(reference / "variables.json", {"variables": variables})
    write_json(reference / "merit.json", merit)
    write_json(
        reference / "run_meta.json",
        {
            "source": "generated_three_field_geometry_reference",
            "basin_run_dir": str(args.basin_run_dir.resolve()),
            "source_system": str(system_path),
            "source_variables": str(variables_path),
            "source_merit": str(merit_path),
            "field_count": len(system.get("fields", []) or []),
            "geometry_kinds": sorted(kinds),
            "variable_count": len(variables),
            "initial_out_of_template_bounds": out_of_bounds,
            "executable": str(exe),
            "command": command,
            "reference_run_meta": run_meta,
        },
    )
    return reference
