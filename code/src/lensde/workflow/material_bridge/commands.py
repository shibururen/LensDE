"""Native bridge scoring and continuation argv under the retained optical contract."""
from __future__ import annotations

import argparse
import math
from pathlib import Path
from typing import Any
from . import contracts as _module_contracts
from . import files as _module_files


def base_trace_command(
    *,
    exe: Path,
    case_dir: Path,
    args: argparse.Namespace,
) -> list[str]:
    rays = max(1, int(getattr(args, "material_bridge_trace_rays", 6)))
    arms = max(1, int(getattr(args, "material_bridge_trace_arms", 8)))
    distribution = str(
        getattr(args, "material_bridge_trace_distribution", "gaussian_ring_arm")
        or "gaussian_ring_arm"
    )
    # The bridge is a contract transition, not a separate optical model. Keep
    # its aiming identical to the candidate system so diagnostic, repair, and
    # deepen do not silently replace calibrated aiming with plain paraxial.
    system = _module_files.read_json(case_dir / "system_input.json")
    aiming = _module_contracts.system_aiming_policy(system)
    return [
        str(exe),
        "--system", str(case_dir / "system_input.json"),
        "--merit", str(case_dir / "merit.json"),
        "--aiming", aiming["mode"],
        "--aiming-iter", str(aiming["max_iter"]),
        "--trace-policy", "soft_coverage",
        "--rays", str(rays),
        "--arms", str(arms),
        "--distribution", distribution,
        "--sampling-seed", "1",
        "--clear-rings", "4",
        "--no-vignetting-merit-aperture-envelope-experimental",
        "--spot-rays", "41",
        "--spot-arms", "64",
        "--spot-distribution", "uniform",
        "--spot-sampling-seed", "1",
        "--variables", str(case_dir / "variables.json"),
        "--optimizer-internal-scaling", str(
            getattr(args, "optimizer_internal_scaling", "edge_sag_normalized")
            or "edge_sag_normalized"
        ),
        "--aiming-require-convergence" if aiming["require_convergence"] else "--aiming-best-effort",
        "--aiming-continuation-steps", str(aiming["continuation_steps"]),
        "--aiming-stop-radius-source", aiming["stop_radius_source"],
        "--asphere-reference-policy", "current",
    ]


def cpu_score_command(
    optimizer_command: list[str], cpu_exe: Path, cpu_case_dir: Path
) -> list[str]:
    command: list[str] = [str(cpu_exe.resolve())]
    skip_next = False
    for index, value in enumerate(optimizer_command[1:], start=1):
        if skip_next:
            skip_next = False
            continue
        if value == "--de-cuda-min-batch":
            skip_next = True
            continue
        if index > 1 and optimizer_command[index - 1] == "--out-dir":
            command.append(str(cpu_case_dir / "score"))
        else:
            command.append(value)
    if "--cpu" not in command:
        command.append("--cpu")
    return command


def serial_de_command(
    prepared: dict[str, Any], exe: Path, args: argparse.Namespace
) -> list[str]:
    case_dir = Path(prepared["case_dir"])
    generations = int(prepared["generations"])
    requested_epoch = max(
        1, int(getattr(args, "material_bridge_repair_epoch_generations", 50))
    )
    epoch_generations = math.gcd(generations, requested_epoch)
    epochs = max(1, generations // epoch_generations)
    command = base_trace_command(exe=exe, case_dir=case_dir, args=args)
    command.extend(
        [
            "--clear-aperture-update",
            "--sync-stop-aperture",
            "--initial-population", str(prepared["population_path"]),
            "--adaptive-hybrid-de-dls",
            "--pop", str(prepared["population_rows"]),
            "--gen", str(epoch_generations),
            "--hybrid-outer", str(epochs),
            "--hybrid-de-gen", str(epoch_generations),
            "--hybrid-parents", "4",
            "--hybrid-polish-workers", "0",
            "--hybrid-refresh", "0.25",
            "--hybrid-distance-metric", "bound_normalized",
            "--hybrid-local-iter", "0",
            "--no-polish",
            "--de-variant", str(
                getattr(args, "material_bridge_de_variant", "jade_pbest_archive_lpsr_feas")
            ),
            "--de-strategy", str(getattr(args, "material_bridge_de_strategy", "rand1bin")),
            "--constraint-handling", str(prepared["constraint_handling"]),
            "--adaptive-search-purpose", str(prepared["purpose"]),
            "--adaptive-min-population", "20",
            "--basin-disable",
            "--adaptive-no-topology-summary",
            "--seed", str(prepared["seed"]),
            "--out-dir", str(case_dir / "run"),
            "--de-cuda-min-batch", str(
                max(1, int(getattr(args, "material_bridge_de_cuda_min_batch", 1)))
            ),
            "--adaptive-lpsr-horizon-generations", str(max(9600, generations * 24)),
        ]
    )
    min_coverage = _module_contracts.minimum_trace_coverage(args)
    if min_coverage < 1.0:
        command.extend(
            [
                "--adaptive-allow-near-trace-infeasible",
                "--trace-coverage-epsilon",
                f"{1.0 - min_coverage:.17g}",
            ]
        )
    return command
