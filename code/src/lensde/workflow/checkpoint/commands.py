"""Native continuation/scoring argv and anchored physical-variable documents."""
from __future__ import annotations

import copy
import math
from pathlib import Path
from typing import Any, Iterable
from lensde.helpers import run_explore_dls_feedback_cycles as feedback_cycles
from . import contracts as _module_contracts
from . import files as _module_files


def variables_for_anchor(variables_doc: dict[str, Any], values: list[float]) -> dict[str, Any]:
    out = copy.deepcopy(variables_doc)
    variables = list(out.get("variables") or [])
    for variable, value in zip(variables, values, strict=False):
        variable["initial"] = float(value)
        if "lower" in variable and float(value) < _module_files.finite(variable.get("lower"), -math.inf):
            variable["lower"] = float(value)
        if "upper" in variable and float(value) > _module_files.finite(variable.get("upper"), math.inf):
            variable["upper"] = float(value)
    out["variables"] = variables
    _module_contracts.assert_continuous_material_variables_preserved(
        variables_doc,
        out,
        stage="checkpoint anchor preparation",
    )
    return out


def remove_flag(command: list[str], flag: str, takes_value: bool = False) -> list[str]:
    out = list(command)
    while flag in out:
        index = out.index(flag)
        del out[index]
        if takes_value and index < len(out) and not str(out[index]).startswith("--"):
            del out[index]
    return out


def append_glass_contract(
    command: list[str],
    source_command: list[str],
    case_dir: Path,
) -> None:
    if "--glass-search" not in source_command:
        return
    command.extend(
        [
            "--glass-search",
            "--glass-search-config",
            str(case_dir / "glass_search.json"),
        ]
    )
    for flag in (
        "--glass-database",
        "--glass-mutation-rate",
        "--glass-random-rate",
        "--glass-pbest-inherit-rate",
        "--glass-neighbor-count",
    ):
        value = _module_files.flag_value(source_command, flag)
        if value:
            command.extend([flag, value])
    if "--glass-no-local-search" in source_command:
        command.append("--glass-no-local-search")


def build_de_command(
    source_command: list[str],
    *,
    exe: Path,
    case_dir: Path,
    population: int,
    generations: int,
    seed: int,
    checkpoint_generation: int,
    use_cuda: bool,
) -> list[str]:
    command = feedback_cycles.base_command_prefix(source_command)
    # The checkpoint system is synthesized by applying archived variable
    # values to the prepared (pre-materialization) basin base.  Its clear
    # apertures therefore belong to the base geometry, not to this candidate.
    # Let the executable materialize the candidate once before scoring/DE.
    command = remove_flag(command, "--input-clear-apertures-materialized")
    command[0] = str(exe)
    command = remove_flag(command, "--cpu")
    if not use_cuda:
        command.append("--cpu")
    feedback_cycles.disable_post_outer_key_tracking(command)
    command.extend(
        [
            "--system", str(case_dir / "system_input.json"),
            "--merit", str(case_dir / "merit.json"),
            "--variables", str(case_dir / "variables.json"),
            "--initial-population", str(case_dir / "initial_population.csv"),
            "--adaptive-hybrid-de-dls",
            "--pop", str(population),
            "--gen", str(generations),
            "--hybrid-outer", "1",
            "--hybrid-de-gen", str(generations),
            "--hybrid-parents", "4",
            "--hybrid-polish-workers", "0",
            "--hybrid-refresh", "0",
            "--hybrid-distance-metric", "bound_normalized",
            "--hybrid-local-iter", "0",
            "--no-polish",
            "--de-variant", "jade_pbest_archive_lpsr_feas",
            "--de-strategy", "rand1bin",
            "--constraint-handling", "scalar_merit",
            "--adaptive-search-purpose", "checkpoint_de400_layer",
            "--adaptive-min-population", "20",
            "--adaptive-lpsr-horizon-generations", str(max(1000, generations)),
            "--pbest-scope", "legacy",
            "--pbest-fraction", "0.1",
            "--de-mating-scope", "global",
            "--hybrid-no-inject-best",
            "--seed", str(seed),
            "--progress-every", str(checkpoint_generation),
            "--out-dir", str(case_dir / "run"),
        ]
    )
    if use_cuda:
        feedback_cycles.set_command_arg(command, "--de-cuda-min-batch", 1)
    _module_contracts.append_checkpoint_adaptive_trace_contract(command, source_command)
    append_glass_contract(command, source_command, case_dir)
    return command


def build_score_command(
    source_command: list[str],
    *,
    exe: Path,
    case_dir: Path,
    use_cuda: bool,
) -> list[str]:
    command = feedback_cycles.base_command_prefix(source_command)
    # See build_de_command(): case_dir/system_input.json is synthesized from
    # the prepared basin base and must receive its first aperture update.
    command = remove_flag(command, "--input-clear-apertures-materialized")
    command[0] = str(exe)
    command = remove_flag(command, "--cpu")
    # Optimizer snapshots may come from CUDA, but every workflow handoff and
    # reference is deliberately evaluated by the independent CPU path.
    command.append("--cpu")
    command.extend(
        [
            "--system", str(case_dir / "system_input.json"),
            "--merit", str(case_dir / "merit.json"),
            "--variables", str(case_dir / "variables.json"),
            "--out-dir", str(case_dir / "result"),
        ]
    )
    append_glass_contract(command, source_command, case_dir)
    return command
