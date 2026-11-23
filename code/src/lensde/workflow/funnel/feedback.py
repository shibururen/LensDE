"""Coordinate ordered preparation, serial/batched fallback and fixed-glass report publication."""
from __future__ import annotations
from typing import Any
from .contracts import MaterialPolicy
from pathlib import Path
from .common import _row_truthy
import argparse
from lensde.runtime import concurrency as concurrent
from lensde.helpers import run_explore_dls_feedback_cycles as feedback_cycles
import os
from .common import read_json
from .feedback_case import run_prepared_refine_like_feedback_serial
from .feedback_contracts import score_refine_like_parent_on_locked_contract
from .feedback_contracts import select_fixed_glass_feedback_attempt
from .common import write_csv
from .common import write_json


def run_refine_like_feedback(
    args: argparse.Namespace,
    source_dirs: list[Path],
    feedback_dir: Path,
    *,
    _version: MaterialPolicy,
) -> None:
    algorithms = feedback_cycles.list_arg(str(args.algorithms))
    material_modes = feedback_cycles.list_arg(str(args.material_modes))
    source_systems = [
        read_json(feedback_cycles.file_from_source(source_dir, "system.json"))
        for source_dir in source_dirs
    ]
    glass_libraries = feedback_cycles.glass_libraries_for_systems(source_systems)
    glasses = feedback_cycles.load_glasses(
        Path(args.glass_database),
        glass_libraries,
        feedback_cycles.systems_wavelengths_um(source_systems),
    )
    attempt_rows: list[dict[str, Any]] = []
    rows: list[dict[str, Any]] = []
    jobs = [
        (source_rank, source_dir, material_mode, algorithm)
        for source_rank, source_dir in enumerate(source_dirs, start=1)
        for material_mode in material_modes
        for algorithm in algorithms
    ]
    workers = min(len(jobs), max(1, int(getattr(args, "de_workers", 1)))) if jobs else 1
    child_cpu_workers = max(0, int(getattr(args, "de_child_cpu_workers", 0)))
    if child_cpu_workers <= 0 and workers > 1:
        child_cpu_workers = max(1, (os.cpu_count() or 1) // workers)
    args.de_resolved_child_cpu_workers = child_cpu_workers

    def prepare_job(job: tuple[int, Path, str, str]) -> dict[str, Any]:
        source_rank, source_dir, material_mode, algorithm = job
        return _version.prepare_refine_like_feedback_case(
            source_rank=source_rank,
            source_dir=source_dir.resolve(),
            material_mode=material_mode,
            algorithm=algorithm,
            out_root=feedback_dir,
            glasses=glasses,
            args=args,
        )

    def record(row: dict[str, Any]) -> None:
        selected = select_fixed_glass_feedback_attempt(row, args)
        attempt_rows.append(row)
        rows.append(selected)
        case_dir = Path(str(row.get("case_dir") or Path(str(row.get("out_dir"))).parent))
        write_json(case_dir / "summary.json", row)
        write_json(case_dir / "admission.json", {"attempt": row, "selected": selected})
        ranked = feedback_cycles.sort_rows(rows, args)
        write_csv(feedback_dir / "fixed_glass_feedback_ranked_partial.csv", ranked)
        write_json(feedback_dir / "fixed_glass_feedback_ranked_partial.json", {"rows": ranked})

    prepared = [prepare_job(job) for job in jobs]
    for item in prepared:
        score_refine_like_parent_on_locked_contract(item, args, _version=_version)
    policy = _version.fixed_glass_feedback_multi_island_policy(args, prepared)
    write_json(feedback_dir / "fixed_glass_feedback_multi_island_policy.json", policy)
    cohort_execution = bool(policy["requested"] and (not policy.get("global_fallback_reasons")))
    if cohort_execution:
        print(
            f"=== fixed-glass feedback executor: compatibility cohorts cohorts={policy['cohort_count']} batched_islands={policy['batched_island_count']} serial_islands={policy['serial_island_count']} ===",
            flush=True,
        )
        for cohort in policy["cohorts"]:
            cohort_items = [prepared[index] for index in cohort["member_indices"]]
            cohort_id = str(cohort["cohort_id"])
            if cohort["used"]:
                print(
                    f"=== fixed-glass cohort {cohort_id}: batched islands={len(cohort_items)} ===",
                    flush=True,
                )
                cohort_rows = _version.run_prepared_refine_like_feedback_batched(
                    cohort_items,
                    args,
                    policy,
                    batch_dir=feedback_dir / "multi_island_cohorts" / cohort_id,
                )
                for row in cohort_rows:
                    row["multi_island_cohort_id"] = cohort_id
                    row["multi_island_cohort_size"] = len(cohort_items)
                    record(row)
            else:
                print(
                    f"=== fixed-glass cohort {cohort_id}: serial islands={len(cohort_items)}; "
                    + "; ".join(cohort["fallback_reasons"])
                    + " ===",
                    flush=True,
                )
                for item in cohort_items:
                    row = run_prepared_refine_like_feedback_serial(item, args)
                    row["multi_island_cohort_id"] = cohort_id
                    row["multi_island_cohort_size"] = len(cohort_items)
                    record(row)
    elif policy["requested"] and policy["fallback_reasons"]:
        print(
            "=== fixed-glass multi-island fallback to serial pure DE: "
            + "; ".join(policy["fallback_reasons"])
            + " ===",
            flush=True,
        )
    if not rows and workers <= 1:
        for item in prepared:
            record(run_prepared_refine_like_feedback_serial(item, args))
    elif not rows:
        print(
            f"=== fixed-glass feedback candidate executor: serial-fallback workers={workers} evaluator={('CUDA' if bool(getattr(args, 'de_use_cuda', False)) else 'CPU')} child_cpu_workers={child_cpu_workers} ===",
            flush=True,
        )
        with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
            futures = [
                pool.submit(run_prepared_refine_like_feedback_serial, item, args)
                for item in prepared
            ]
            for future in concurrent.futures.as_completed(futures):
                record(future.result())
    ranked_attempts = feedback_cycles.sort_rows(attempt_rows, args)
    for index, row in enumerate(ranked_attempts, start=1):
        row["attempt_rank"] = index
    rejected = [row for row in ranked_attempts if not _row_truthy(row.get("admission_ok"))]
    write_csv(feedback_dir / "fixed_glass_feedback_attempts.csv", ranked_attempts)
    write_json(feedback_dir / "fixed_glass_feedback_attempts.json", {"rows": ranked_attempts})
    write_csv(feedback_dir / "fixed_glass_feedback_rejected.csv", rejected)
    write_json(feedback_dir / "fixed_glass_feedback_rejected.json", {"rows": rejected})
    ranked = feedback_cycles.sort_rows(
        [row for row in rows if _row_truthy(row.get("admission_ok"))], args
    )
    if not ranked:
        raise RuntimeError("fixed-glass feedback produced no admissible DE result or parent guard")
    write_csv(feedback_dir / "fixed_glass_feedback_ranked.csv", ranked)
    write_json(feedback_dir / "fixed_glass_feedback_ranked.json", {"rows": ranked})
    write_csv(feedback_dir / "cycle_summary.csv", ranked)
    write_json(feedback_dir / "cycle_summary.json", {"rows": ranked})
