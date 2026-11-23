from __future__ import annotations

from lensde.resources import ProjectPaths
PROJECT_ROOT = ProjectPaths().root

import argparse
from lensde.runtime import concurrency as concurrent
import csv
import json
import math
import os
import shutil
from lensde.runtime import processes as subprocess
import time
from pathlib import Path
from typing import Any

import sys


SCRIPT_PATH = Path(__file__).resolve()
GUI_DIR = PROJECT_ROOT / "web"
CPP_DIR = PROJECT_ROOT / "native"
REPO_ROOT = PROJECT_ROOT


from lensde.engine import core as server  # noqa: E402


VALUE_FLAGS = {
    "--system",
    "--merit",
    "--variables",
    "--out-dir",
}


def read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")


def finite_float(value: Any, default: float = math.nan) -> float:
    try:
        out = float(value)
    except (TypeError, ValueError):
        return default
    return out if math.isfinite(out) else default


def positive_int(value: Any, default: int = 1) -> int:
    try:
        out = int(float(value))
    except (TypeError, ValueError):
        return default
    return max(1, out)


def nonnegative_int(value: Any, default: int = 0) -> int:
    try:
        out = int(float(value))
    except (TypeError, ValueError):
        return default
    return max(0, out)


def resolved_child_cpu_workers(workers: int, requested: int) -> int:
    if requested > 0:
        return requested
    if workers <= 1:
        return 0
    detected = os.cpu_count() or 1
    return max(1, detected // workers)


def set_arg(command: list[str], flag: str, value: str) -> None:
    try:
        index = command.index(flag)
    except ValueError:
        command.extend([flag, value])
        return
    if index + 1 >= len(command):
        command.append(value)
    else:
        command[index + 1] = value


def variable_list(path: Path) -> list[dict[str, Any]]:
    data = read_json(path)
    if isinstance(data, dict):
        values = data.get("variables", [])
        return values if isinstance(values, list) else []
    return data if isinstance(data, list) else []


def first_existing(paths: list[Path]) -> Path:
    for path in paths:
        if path.exists():
            return path
    raise FileNotFoundError("none of these paths exists: " + ", ".join(str(path) for path in paths))


def full_context_paths(run_dir: Path) -> tuple[Path, Path]:
    return (
        first_existing([
            run_dir / "system_input.json",
            run_dir.parent / "system_input.json",
            run_dir.parent.parent / "system_input.json",
        ]),
        first_existing([
            run_dir / "variables.json",
            run_dir.parent / "variables.json",
            run_dir.parent.parent / "variables.json",
        ]),
    )


def surface_value(system: dict[str, Any], variable: dict[str, Any]) -> float:
    surfaces = system.get("surfaces", [])
    index = int(finite_float(variable.get("index"), -1))
    if not (0 <= index < len(surfaces)):
        return finite_float(variable.get("initial"), 0.0)
    surface = surfaces[index]
    kind = str(variable.get("kind") or "")
    if kind == "thickness":
        return finite_float(surface.get("thickness"), finite_float(variable.get("initial"), 0.0))
    if kind == "curvature":
        radius = finite_float(surface.get("radius"), math.nan)
        if not math.isfinite(radius) or abs(radius) <= 1.0e-14:
            return 0.0
        return 1.0 / radius
    if kind == "material_nd":
        material = surface.get("material_after", {}) if isinstance(surface, dict) else {}
        nd, _vd = server.material_nd_vd(material)
        return finite_float(nd, finite_float(variable.get("initial"), 1.5))
    if kind == "material_vd":
        material = surface.get("material_after", {}) if isinstance(surface, dict) else {}
        _nd, vd = server.material_nd_vd(material)
        return finite_float(vd, finite_float(variable.get("initial"), 50.0))
    if kind == "conic":
        return finite_float(surface.get("conic"), finite_float(variable.get("initial"), 0.0))
    if kind == "asphere_coeff":
        coeffs = surface.get("asphere_coefficients", []) or []
        term = int(finite_float(variable.get("term"), 0))
        if 0 <= term < len(coeffs):
            return finite_float(coeffs[term], finite_float(variable.get("initial"), 0.0))
    return finite_float(variable.get("initial"), 0.0)


def dls_variables_for_system(
    system: dict[str, Any],
    template_variables: list[dict[str, Any]],
) -> tuple[list[dict[str, Any]], int]:
    variables: list[dict[str, Any]] = []
    out_of_bounds = 0
    for variable in template_variables:
        item = dict(variable)
        value = surface_value(system, item)
        item["initial"] = value
        lower = finite_float(item.get("lower"), -math.inf)
        upper = finite_float(item.get("upper"), math.inf)
        if value < lower - 1.0e-12 or value > upper + 1.0e-12:
            out_of_bounds += 1
        variables.append(item)
    return variables, out_of_bounds


def archive_merit(basin: dict[str, Any]) -> float:
    score = basin.get("score") if isinstance(basin.get("score"), dict) else {}
    return finite_float(score.get("merit"), finite_float(basin.get("merit"), basin.get("objective")))


def basin_key(basin: dict[str, Any]) -> str:
    return str(
        basin.get("structural_topology_key")
        or basin.get("topology_key")
        or basin.get("semantic_key")
        or ""
    )


def materialize_basin_system(
    base_system: dict[str, Any],
    full_variables: list[dict[str, Any]],
    basin: dict[str, Any],
    reference_system: dict[str, Any],
) -> dict[str, Any]:
    values = [finite_float(value, 0.0) for value in basin.get("values", [])]
    system = server.apply_variable_values_to_system(base_system, full_variables, values, None)
    if isinstance(reference_system.get("ray_aiming"), dict):
        system["ray_aiming"] = reference_system["ray_aiming"]
    return system


def build_command(
    reference_command: list[str],
    *,
    exe: Path,
    system_path: Path,
    merit_path: Path,
    variables_path: Path,
    out_dir: Path,
) -> list[str]:
    command = [str(item) for item in reference_command]
    if command:
        command[0] = str(exe)
    else:
        command = [str(exe)]
    set_arg(command, "--system", str(system_path))
    set_arg(command, "--merit", str(merit_path))
    set_arg(command, "--variables", str(variables_path))
    set_arg(command, "--out-dir", str(out_dir))
    if "--cpu" not in command:
        command.append("--cpu")
    return command


def run_command(command: list[str], out_dir: Path, timeout: float, child_cpu_workers: int = 0) -> dict[str, Any]:
    out_dir.mkdir(parents=True, exist_ok=True)
    write_json(out_dir / "command.json", command)
    (out_dir / "command.txt").write_text(" ".join(command), encoding="utf-8")
    env = None
    if child_cpu_workers > 0:
        env = os.environ.copy()
        env["OPTILAND_CPU_WORKERS"] = str(child_cpu_workers)
    started = time.perf_counter()
    try:
        proc = subprocess.run(
            command,
            cwd=REPO_ROOT,
            env=env,
            text=True,
            encoding="utf-8",
            errors="replace",
            capture_output=True,
            check=False,
            timeout=timeout if timeout > 0 else None,
        )
        stdout = proc.stdout
        stderr = proc.stderr
        returncode = proc.returncode
        timed_out = False
    except subprocess.TimeoutExpired as exc:
        stdout = exc.stdout or ""
        stderr = exc.stderr or ""
        returncode = -999
        timed_out = True
    (out_dir / "stdout.log").write_text(stdout, encoding="utf-8", errors="replace")
    (out_dir / "stderr.log").write_text(stderr, encoding="utf-8", errors="replace")
    return {
        "returncode": returncode,
        "timed_out": timed_out,
        "elapsed_seconds": max(0.0, time.perf_counter() - started),
    }


def score_digest(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {}
    score = read_json(path)
    keys = [
        "merit",
        "uncapped_merit",
        "trace_ok",
        "trace_coverage",
        "min_field_coverage",
        "mean_spot",
        "max_spot",
        "max_distortion",
        "efl",
        "ttl",
        "bfl",
        "min_edge",
        "constraint_group_error",
        "first_order_group_error",
        "image_group_error",
        "chief_ray_path_bend_max_deg",
        "chief_ray_path_excess_max",
    ]
    return {key: score.get(key) for key in keys if key in score}


def run_once(args: argparse.Namespace) -> Path:
    run_dir = args.basin_run_dir.resolve()
    reference_run = args.reference_dls_run.resolve()
    out_root = args.out_dir.resolve()
    out_root.mkdir(parents=True, exist_ok=True)

    archive = read_json(run_dir / "basin_archive.json")
    basins = list(archive.get("basins", []))
    if args.max_basins > 0:
        basins = basins[: args.max_basins]

    full_system_path, full_variables_path = full_context_paths(run_dir)
    base_system = read_json(full_system_path)
    full_variables = variable_list(full_variables_path)
    reference_system = read_json(reference_run / "input_system.json")
    template_variables = variable_list(reference_run / "variables.json")
    run_meta = read_json(reference_run / "run_meta.json")
    reference_command = run_meta.get("command") if isinstance(run_meta.get("command"), list) else []
    exe = args.exe.resolve() if args.exe else Path(str(run_meta.get("executable") or CPP_DIR / "build" / "lensde_cpu.exe"))
    if not exe.is_absolute():
        exe = (CPP_DIR / exe).resolve()

    merit_path = out_root / "dls_reference_merit.json"
    shutil.copy2(reference_run / "merit.json", merit_path)
    write_json(out_root / "sweep_config.json", {
        "basin_run_dir": str(run_dir),
        "reference_dls_run": str(reference_run),
        "out_dir": str(out_root),
        "archive_count": len(archive.get("basins", [])),
        "selected_basin_count": len(basins),
        "full_system_path": str(full_system_path),
        "full_variables_path": str(full_variables_path),
        "template_variable_count": len(template_variables),
        "template_variable_kinds": sorted({str(v.get("kind")) for v in template_variables}),
        "reference_command": reference_command,
        "exe": str(exe),
        "workers": positive_int(getattr(args, "workers", 1), 1),
        "child_cpu_workers": resolved_child_cpu_workers(
            positive_int(getattr(args, "workers", 1), 1),
            nonnegative_int(getattr(args, "child_cpu_workers", 0), 0),
        ),
    })

    rows: list[dict[str, Any]] = []
    workers = min(len(basins), positive_int(getattr(args, "workers", 1), 1)) if basins else 1
    child_cpu_workers = resolved_child_cpu_workers(
        workers,
        nonnegative_int(getattr(args, "child_cpu_workers", 0), 0),
    )

    def process_basin(position: int, basin: dict[str, Any]) -> dict[str, Any]:
        rank = int(finite_float(basin.get("rank"), position))
        basin_dir = out_root / f"basin_{position:03d}_rank_{rank:04d}"
        basin_dir.mkdir(parents=True, exist_ok=True)
        input_system_path = basin_dir / "input_system.json"
        variables_path = basin_dir / "variables.json"
        if args.force_rerun or not (basin_dir / "score.json").exists():
            system = materialize_basin_system(base_system, full_variables, basin, reference_system)
            dls_variables, out_of_bounds = dls_variables_for_system(system, template_variables)
            write_json(input_system_path, system)
            write_json(variables_path, {"variables": dls_variables})
            write_json(basin_dir / "basin_meta.json", {
                "basin_rank": rank,
                "basin_id": basin.get("id"),
                "archive_merit": archive_merit(basin),
                "structural_topology_key": basin_key(basin),
                "dls_variable_count": len(dls_variables),
                "initial_out_of_template_bounds": out_of_bounds,
            })
            command = build_command(
                reference_command,
                exe=exe,
                system_path=input_system_path,
                merit_path=merit_path,
                variables_path=variables_path,
                out_dir=basin_dir,
            )
            print(f"[{position}/{len(basins)}] DLS basin rank {rank}", flush=True)
            result = run_command(command, basin_dir, args.timeout, child_cpu_workers)
        else:
            result = {"returncode": 0, "timed_out": False, "elapsed_seconds": 0.0, "reused": True}
            meta = read_json(basin_dir / "basin_meta.json") if (basin_dir / "basin_meta.json").exists() else {}
            out_of_bounds = int(finite_float(meta.get("initial_out_of_template_bounds"), 0))

        initial = score_digest(basin_dir / "initial_score.json")
        final = score_digest(basin_dir / "score.json")
        initial_merit = finite_float(initial.get("merit"), math.nan)
        final_merit = finite_float(final.get("merit"), math.nan)
        improvement = initial_merit - final_merit if math.isfinite(initial_merit) and math.isfinite(final_merit) else math.nan
        improvement_ratio = (
            final_merit / initial_merit
            if math.isfinite(initial_merit) and abs(initial_merit) > 1.0e-300 and math.isfinite(final_merit)
            else math.nan
        )
        row = {
            "index": position,
            "basin_rank": rank,
            "basin_id": basin.get("id"),
            "archive_merit": archive_merit(basin),
            "archive_generation": basin.get("generation"),
            "structural_topology_key": basin_key(basin),
            "initial_out_of_template_bounds": out_of_bounds,
            "returncode": result.get("returncode"),
            "timed_out": result.get("timed_out"),
            "elapsed_seconds": result.get("elapsed_seconds"),
            "initial_merit": initial_merit,
            "final_merit": final_merit,
            "improvement": improvement,
            "final_over_initial": improvement_ratio,
            "initial_trace_ok": initial.get("trace_ok"),
            "final_trace_ok": final.get("trace_ok"),
            "final_trace_coverage": final.get("trace_coverage"),
            "final_min_field_coverage": final.get("min_field_coverage"),
            "initial_mean_spot": initial.get("mean_spot"),
            "final_mean_spot": final.get("mean_spot"),
            "final_max_spot": final.get("max_spot"),
            "final_max_distortion": final.get("max_distortion"),
            "final_efl": final.get("efl"),
            "final_ttl": final.get("ttl"),
            "final_bfl": final.get("bfl"),
            "final_min_edge": final.get("min_edge"),
            "final_constraint_group_error": final.get("constraint_group_error"),
            "final_first_order_group_error": final.get("first_order_group_error"),
            "final_image_group_error": final.get("image_group_error"),
            "out_dir": str(basin_dir),
        }
        return row

    if workers <= 1 or len(basins) <= 1:
        for position, basin in enumerate(basins, start=1):
            rows.append(process_basin(position, basin))
            write_report(out_root, rows)
    else:
        print(
            f"Running DLS potential sweep with workers={workers}, "
            f"child_cpu_workers={child_cpu_workers}",
            flush=True,
        )
        with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
            futures = [
                pool.submit(process_basin, position, basin)
                for position, basin in enumerate(basins, start=1)
            ]
            for future in concurrent.futures.as_completed(futures):
                rows.append(future.result())
                rows.sort(key=lambda row: int(finite_float(row.get("index"), 0)))
                write_report(out_root, rows)
    return out_root


def write_report(out_root: Path, rows: list[dict[str, Any]]) -> None:
    ranked = sorted(rows, key=lambda row: finite_float(row.get("final_merit"), math.inf))
    write_json(out_root / "dls_potential_summary.json", {
        "count": len(rows),
        "best_final_merit": ranked[0].get("final_merit") if ranked else None,
        "rows_by_final_merit": ranked,
        "rows": rows,
    })
    if rows:
        fieldnames = list(rows[0].keys())
        with (out_root / "dls_potential_summary.csv").open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(rows)
        with (out_root / "dls_potential_ranked.csv").open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(ranked)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--basin-run-dir",
        type=Path,
        default=GUI_DIR / "work" / "explore_seed_variant_selected_current_default_pure_de_11p_hfov90_unlucky_20260702" / "01_traceable_existing" / "pure_de_600",
    )
    parser.add_argument(
        "--reference-dls-run",
        type=Path,
        default=GUI_DIR / "work" / "run_20260702_135338_588",
    )
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--max-basins", type=int, default=0)
    parser.add_argument("--timeout", type=float, default=0.0)
    parser.add_argument("--force-rerun", action="store_true")
    parser.add_argument("--exe", type=Path, default=None)
    parser.add_argument("--workers", type=int, default=1)
    parser.add_argument("--child-cpu-workers", type=int, default=0)
    args = parser.parse_args()
    out_root = run_once(args)
    print(out_root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
