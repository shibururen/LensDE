"""Anchored optical variables and retained population preparation for continuation."""
from __future__ import annotations

import argparse
import copy
import csv
from pathlib import Path
from typing import Any
from lensde.helpers import run_explore_dls_feedback_cycles as feedback_cycles
from lensde.engine import core as server
from . import contracts as _module_contracts
from . import files as _module_files
from . import paths as _module_paths


def bridge_executable(args: argparse.Namespace) -> Path:
    cuda_text = str(getattr(args, "material_bridge_de_cuda_exe", "") or "").strip()
    if bool(getattr(args, "material_bridge_de_use_cuda", True)):
        candidate = Path(cuda_text).resolve() if cuda_text else _module_paths.CPP_DIR / "build" / "lensde_de_cuda.exe"
        if not candidate.is_file():
            raise FileNotFoundError(f"material bridge CUDA executable not found: {candidate}")
        return candidate
    if getattr(args, "exe", None):
        return Path(args.exe).resolve()
    return _module_paths.CPP_DIR / "build" / "lensde_cpu.exe"


def prepare_case_inputs(
    *,
    source: Path,
    case_dir: Path,
    merit: dict[str, Any],
    nodes: tuple[float, ...],
    rays: int,
    arms: int,
    distribution: str,
) -> tuple[dict[str, Any], dict[str, Any]]:
    system = copy.deepcopy(_module_files.read_json(_module_files.source_file(source, "system.json")))
    system["fields"] = _module_contracts.fields_on_exact_nodes(
        system,
        nodes,
        allow_zero_for_missing=_module_contracts.minimum_relative_illumination(merit) <= 0.0,
    )
    variables_source = _module_files.read_json(_module_files.source_file(source, "variables.json"))
    source_variables = (
        list(variables_source.get("variables") or [])
        if isinstance(variables_source, dict)
        else list(variables_source or [])
    )
    # The aperture-realized handoff contains solved coefficients for every
    # target field, while its copied variables.json still has the sparse
    # source-field indexing.  Rebuild the internal vignetting variables before
    # synchronizing initials so newly introduced fields remain controllable in
    # repair/deepen DE.  This preserves explicit user-authored variables and
    # replaces only entries marked as auto-generated vignetting variables.
    variables = server.ensure_vignetting_variables_for_system(
        system,
        source_variables,
        merit,
        handoff=True,
    )
    variables = server.sync_variables_from_system(variables, system)
    variables_doc = {"variables": variables}
    _module_contracts.assert_continuous_material_variables_preserved(
        source_variables,
        variables_doc,
        stage="material bridge case preparation",
    )
    common_merit = copy.deepcopy(merit)
    common_merit["num_rays"] = rays
    server.dynamic_aperture_contract.apply_merit(common_merit)
    common_merit["pupil_arms"] = arms
    common_merit["pupil_distribution"] = distribution
    case_dir.mkdir(parents=True, exist_ok=True)
    _module_files.write_json(case_dir / "system_input.json", system)
    _module_files.write_json(case_dir / "variables.json", variables_doc)
    _module_files.write_json(case_dir / "merit.json", common_merit)
    return system, variables_doc


def read_population(path: Path) -> list[list[float]]:
    if not path.is_file():
        return []
    rows: list[list[float]] = []
    with path.open("r", newline="", encoding="utf-8") as handle:
        for raw in csv.reader(handle):
            if not raw:
                continue
            try:
                rows.append([float(value) for value in raw])
            except ValueError:
                continue
    return rows


def retained_population(
    *,
    source_population: Path | None,
    parent: list[float],
    variables: list[dict[str, Any]],
    system: dict[str, Any],
    population: int,
    seed: int,
    scale: str,
    args: argparse.Namespace,
) -> list[list[float]]:
    rows: list[list[float]] = []
    seen: set[tuple[float, ...]] = set()

    def append(values: list[float]) -> None:
        if len(values) != len(parent):
            return
        key = tuple(round(value, 10) for value in values)
        if key in seen:
            return
        seen.add(key)
        rows.append(list(values))

    append(parent)
    if source_population is not None:
        for values in read_population(source_population):
            append(values)
            if len(rows) >= population:
                break
    if len(rows) < population:
        generated = feedback_cycles.build_population(
            system=system,
            variables_doc={"variables": variables},
            glasses=[],
            material_mode="continuous",
            population=max(population * 2, population + 20),
            seed=seed,
            scale=scale,
            args=args,
        )
        for values in generated:
            append(values)
            if len(rows) >= population:
                break
    return rows[:population]


def prepare_de_case(
    *,
    source: Path,
    case_dir: Path,
    merit: dict[str, Any],
    args: argparse.Namespace,
    generations: int,
    population: int,
    population_scale: str,
    seed: int,
    purpose: str,
    source_population: Path | None = None,
) -> dict[str, Any]:
    nodes = _module_contracts.field_nodes(args)
    rays = max(1, int(getattr(args, "material_bridge_trace_rays", 6)))
    arms = max(1, int(getattr(args, "material_bridge_trace_arms", 8)))
    distribution = str(
        getattr(args, "material_bridge_trace_distribution", "gaussian_ring_arm")
        or "gaussian_ring_arm"
    )
    system, variables_doc = prepare_case_inputs(
        source=source,
        case_dir=case_dir,
        merit=merit,
        nodes=nodes,
        rays=rays,
        arms=arms,
        distribution=distribution,
    )
    variables = list(variables_doc.get("variables") or [])
    parent = [_module_files.finite_float(variable.get("initial"), 0.0) for variable in variables]
    rows = retained_population(
        source_population=source_population,
        parent=parent,
        variables=variables,
        system=system,
        population=max(20, population),
        seed=seed,
        scale=population_scale,
        args=args,
    )
    population_path = case_dir / "initial_population.csv"
    feedback_cycles.write_population(population_path, rows)
    generations = max(1, generations)
    constraint_handling = (
        str(getattr(args, "material_bridge_repair_constraint_handling", "scalar_merit"))
        if purpose == "material_bridge_aperture_repair"
        else str(
            getattr(
                args,
                "material_bridge_deepen_constraint_handling",
                "feasibility_rules",
            )
        )
    )
    return {
        "case_dir": case_dir,
        "system": system,
        "variables_doc": variables_doc,
        "variables": variables,
        "continuous_material_de": _module_contracts.continuous_material_variable_contract(variables_doc),
        "population_path": population_path,
        "population_rows": len(rows),
        "generations": generations,
        "seed": seed,
        "purpose": purpose,
        "profile": (
            "material_repair"
            if purpose == "material_bridge_aperture_repair"
            else "material_deepen"
        ),
        "constraint_handling": constraint_handling,
        "aiming_mode": _module_contracts.system_aiming_mode(system),
    }


def anchored_variables_doc(
    variables_doc: dict[str, Any], best_values: list[float]
) -> dict[str, Any]:
    anchored = copy.deepcopy(variables_doc)
    variables = list(anchored.get("variables") or [])
    if len(variables) != len(best_values):
        raise ValueError("best-value count does not match variables")
    for variable, value in zip(variables, best_values):
        variable["initial"] = float(value)
    return anchored
