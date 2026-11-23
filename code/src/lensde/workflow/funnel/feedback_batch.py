"""Group compatible fixed-glass cases and execute their retained broker boundary."""
from __future__ import annotations
from typing import Any
from .settings import DE_DLS_TRACE_COVERAGE_BASELINE
from .contracts import MaterialPolicy
from pathlib import Path
import argparse
from .feedback_contracts import canonical_contract_fingerprint
from lensde.workflow import run_material5_bridge_selection as material_bridge
from .feedback_contracts import ordered_compatibility_cohorts
from .feedback_case import finish_refine_like_feedback_case
from .common import finite_float
from .common import read_json
from .common import write_json


def fixed_glass_feedback_multi_island_policy(
    args: argparse.Namespace, prepared: list[dict[str, Any]], *, _version: MaterialPolicy
) -> dict[str, Any]:
    executable = Path(
        str(getattr(args, "multi_island_exe", "") or "")
        or _version.cpp_dir / "build" / "lensde_multi_island.exe"
    ).resolve()
    requested = bool(getattr(args, "multi_island_enabled", True))
    fallback_reasons: list[str] = []
    if len(prepared) < 2:
        fallback_reasons.append("fewer than two islands")
    if not bool(getattr(args, "de_use_cuda", False)):
        fallback_reasons.append("CUDA is disabled")
    if not executable.is_file():
        fallback_reasons.append(f"multi-island executable is missing: {executable}")
    if {str(item["algorithm"]) for item in prepared} != {"adaptive_jade"}:
        fallback_reasons.append("multi-island requires adaptive_jade")
    if {str(item["material_mode"]) for item in prepared} != {"locked_glass"}:
        fallback_reasons.append("multi-island currently requires locked_glass")
    if int(getattr(args, "refine_outer", 20)) != 20:
        fallback_reasons.append("refine outer epochs must be 20")
    if int(getattr(args, "hybrid_parents", 10)) != 10:
        fallback_reasons.append("hybrid parent count must be 10")
    if abs(float(getattr(args, "refresh", 0.25)) - 0.25) > 1e-12:
        fallback_reasons.append("refresh fraction must be 0.25")
    if str(getattr(args, "constraint_handling", "feasibility_rules")) != "feasibility_rules":
        fallback_reasons.append("constraint handling must be feasibility_rules")
    if (
        str(getattr(args, "de_variant", "jade_pbest_archive_lpsr_feas"))
        != "jade_pbest_archive_lpsr_feas"
    ):
        fallback_reasons.append("unsupported DE variant")
    if str(getattr(args, "de_strategy", "rand1bin")) != "rand1bin":
        fallback_reasons.append("unsupported DE strategy")
    if int(getattr(args, "de_cuda_min_batch", 1)) != 1:
        fallback_reasons.append("CUDA minimum batch must be 1")
    requested_min_trace = float(getattr(args, "min_trace_coverage", DE_DLS_TRACE_COVERAGE_BASELINE))
    requested_min_field = float(getattr(args, "min_field_coverage", requested_min_trace))
    if min(requested_min_trace, requested_min_field) < DE_DLS_TRACE_COVERAGE_BASELINE - 1e-12:
        fallback_reasons.append(
            f"minimum trace and field coverage must be at least {DE_DLS_TRACE_COVERAGE_BASELINE:.2f} for the batched broker"
        )
    expected_early_stop = {
        "refine_early_stop": True,
        "refine_early_stop_min_epochs": 2,
        "refine_early_stop_window": 1,
        "refine_early_stop_min_rel_improve": 0.01,
        "refine_early_stop_min_abs_improve": 0.01,
        "refine_early_stop_min_lpsr_progress": 0.1,
        "refine_early_stop_tail_epochs": 0,
        "refine_lpsr_horizon_epochs": 24,
    }
    for name, expected in expected_early_stop.items():
        actual = getattr(args, name, expected)
        if isinstance(expected, float):
            matches = abs(float(actual) - expected) <= 1e-12
        else:
            matches = actual == expected
        if not matches:
            fallback_reasons.append(f"unsupported {name}={actual}")
    global_fallback_reasons = list(fallback_reasons)
    cohort_indices = ordered_compatibility_cohorts(
        [
            (
                material_bridge.multi_island_layout_signature(item),
                canonical_contract_fingerprint(item["merit"]),
            )
            for item in prepared
        ]
    )
    cohorts: list[dict[str, Any]] = []
    for position, member_indices in enumerate(cohort_indices, start=1):
        reference = prepared[member_indices[0]]
        cohort_used = bool(
            requested and (not global_fallback_reasons) and (len(member_indices) >= 2)
        )
        reasons = list(global_fallback_reasons)
        if not reasons and len(member_indices) < 2:
            reasons.append("fewer than two compatible islands")
        cohorts.append(
            {
                "cohort_id": f"cohort_{position:02d}",
                "member_indices": member_indices,
                "members": [index + 1 for index in member_indices],
                "island_count": len(member_indices),
                "layout_fingerprint": canonical_contract_fingerprint(
                    material_bridge.multi_island_layout_signature(reference)
                ),
                "merit_fingerprint": canonical_contract_fingerprint(reference["merit"]),
                "used": cohort_used,
                "execution_mode": "batched_multi_island_pure_de"
                if cohort_used
                else "serial_pure_de",
                "fallback_reasons": reasons,
            }
        )
    used = any((cohort["used"] for cohort in cohorts))
    if requested and (not global_fallback_reasons) and prepared and (not used):
        fallback_reasons.append("no compatible cohort has at least two islands")
    return {
        "default_enabled": True,
        "requested": requested,
        "eligible": bool(
            not global_fallback_reasons and any((cohort["island_count"] >= 2 for cohort in cohorts))
        ),
        "used": used,
        "fully_batched": bool(used and all((cohort["used"] for cohort in cohorts))),
        "pure_de": True,
        "executable": str(executable),
        "internal_scaling": "edge_sag_normalized",
        "broker_search_minimum_trace_coverage": DE_DLS_TRACE_COVERAGE_BASELINE,
        "final_admission_minimum_trace_coverage": requested_min_trace,
        "final_admission_minimum_field_coverage": requested_min_field,
        "island_count": len(prepared),
        "cohort_count": len(cohorts),
        "batched_island_count": sum(
            (cohort["island_count"] for cohort in cohorts if cohort["used"])
        ),
        "serial_island_count": sum(
            (cohort["island_count"] for cohort in cohorts if not cohort["used"])
        ),
        "global_fallback_reasons": global_fallback_reasons,
        "cohorts": cohorts,
        "fallback_reasons": fallback_reasons,
    }


def run_prepared_refine_like_feedback_batched(
    prepared: list[dict[str, Any]],
    args: argparse.Namespace,
    policy: dict[str, Any],
    batch_dir: Path | None = None,
    *,
    _version: MaterialPolicy,
) -> list[dict[str, Any]]:
    batch_args = argparse.Namespace(**vars(args))
    batch_args.material_bridge_trace_rays = int(getattr(args, "trace_rays", 6))
    batch_args.material_bridge_trace_arms = int(getattr(args, "trace_arms", 8))
    batch_args.material_bridge_trace_distribution = str(
        getattr(args, "trace_distribution", "gaussian_ring_arm")
    )
    batch_args.material_bridge_multi_island_coalesce_us = max(
        0, int(getattr(args, "multi_island_coalesce_us", 1000))
    )
    batch_args.material_bridge_timeout = int(getattr(args, "timeout", 0))
    batch_args.material_bridge_repair_min_trace_coverage = DE_DLS_TRACE_COVERAGE_BASELINE
    batch_args.optimizer_internal_scaling = "edge_sag_normalized"
    executable = Path(
        str(getattr(args, "de_cuda_exe", "") or "")
        or _version.cpp_dir / "build" / "lensde_de_cuda.exe"
    ).resolve()
    results = material_bridge.run_prepared_de_batched(
        prepared,
        exe=executable,
        args=batch_args,
        wave_name="fixed_glass_feedback",
        policy=policy,
        batch_dir=batch_dir,
    )
    rows: list[dict[str, Any]] = []
    for item, result in zip(prepared, results):
        experimental_result_path = Path(item["case_dir"]) / "experimental_result" / "result.json"
        experimental_result = read_json(experimental_result_path)
        dls_counters = {
            key: finite_float(experimental_result.get(key), 0.0)
            for key in ("dls_iterations", "dls_accepted_steps", "dls_rejected_steps")
        }
        if any((value != 0.0 for value in dls_counters.values())):
            raise RuntimeError(
                f"pure-DE invariant failed for {experimental_result_path}: {dls_counters}"
            )
        run_result = {
            "returncode": result.get("returncode"),
            "timed_out": result.get("timed_out"),
            "seconds": result.get("wall_seconds"),
            "execution_mode": "batched_multi_island_pure_de",
            "shared_batch_wall_seconds": result.get("shared_batch_wall_seconds"),
            **dls_counters,
        }
        for key in (
            "optimizer_best_merit",
            "materialized_merit",
            "materialization_merit_delta",
            "materialization_merit_relative_difference",
            "materialization_merit_relative_tolerance",
            "cpu_cuda_materialization_consistent",
            "materialization_valid",
            "multi_island_cache_hit",
        ):
            if key in result:
                run_result[key] = result.get(key)
        write_json(Path(item["case_dir"]) / "multi_island_result.json", run_result)
        rows.append(finish_refine_like_feedback_case(item, run_result, args))
    return rows
