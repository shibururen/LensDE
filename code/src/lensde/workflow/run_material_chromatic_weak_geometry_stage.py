from __future__ import annotations

from lensde.resources import ProjectPaths
PROJECT_ROOT = ProjectPaths().root

import argparse
import copy
import math
import sys
from pathlib import Path
from typing import Any


SCRIPT_PATH = Path(__file__).resolve()
GUI_DIR = PROJECT_ROOT / "web"
CPP_DIR = PROJECT_ROOT / "native"
SCRIPTS_DIR = Path(__file__).resolve().parents[1] / "helpers"




from lensde.helpers import run_explore_dls_feedback_cycles as feedback_cycles  # noqa: E402
from lensde.workflow import run_material5_bridge_selection as material_bridge  # noqa: E402
from lensde.engine import core as server  # noqa: E402


REQUIRED_CHROMATIC_OPERANDS = {
    "lateral_color",
    "axial_color",
    "worst_wavelength_spot",
    "spectral_spot_spread",
}
MATERIAL_CHROMATIC_STAGE_CONTRACT = "sampled_pupil_cpu_materialized_chromatic_v2"


def read_json(path: Path) -> Any:
    return material_bridge.read_json(path)


def write_json(path: Path, data: Any) -> None:
    material_bridge.write_json(path, data)


def finite_float(value: Any, default: float = math.nan) -> float:
    return material_bridge.finite_float(value, default)


def assert_chromatic_merit(merit: dict[str, Any]) -> dict[str, Any]:
    """Reject a stale pre-stage merit instead of running a false repair."""
    present = {
        str(item.get("name") or "").strip().lower()
        for item in merit.get("operands", []) or []
        if isinstance(item, dict)
    }
    missing = sorted(REQUIRED_CHROMATIC_OPERANDS - present)
    if not bool(merit.get("chromatic_spot_merit_enabled", False)) or missing:
        raise RuntimeError(
            "pre-Material-Funnel chromatic repair requires the current chromatic "
            "merit contract; chromatic_spot_merit_enabled="
            f"{bool(merit.get('chromatic_spot_merit_enabled', False))}, "
            f"missing_operands={missing}. Rebuild the saved automatic merit before rerunning."
        )
    return {
        "chromatic_spot_merit_enabled": True,
        "required_operands": sorted(REQUIRED_CHROMATIC_OPERANDS),
        "present_operands": sorted(REQUIRED_CHROMATIC_OPERANDS & present),
    }


def repair_runtime_args(args: argparse.Namespace) -> argparse.Namespace:
    runtime = argparse.Namespace(**vars(args))
    runtime.material_bridge_trace_rays = max(
        1, int(getattr(args, "material_chromatic_repair_trace_rays", 3))
    )
    runtime.material_bridge_trace_arms = max(
        1, int(getattr(args, "material_chromatic_repair_trace_arms", 6))
    )
    runtime.material_bridge_trace_distribution = str(
        getattr(
            args,
            "material_chromatic_repair_trace_distribution",
            "gaussian_ring_arm",
        )
        or "gaussian_ring_arm"
    )
    runtime.material_bridge_repair_constraint_handling = "scalar_merit"
    runtime.material_bridge_repair_epoch_generations = 50
    return runtime


def population_args(args: argparse.Namespace) -> argparse.Namespace:
    return argparse.Namespace(
        continuous_glass_neighbor_rate=float(
            getattr(args, "material_chromatic_repair_glass_neighbor_rate", 0.85)
        ),
        continuous_glass_neighbor_topk=max(
            1, int(getattr(args, "material_chromatic_repair_glass_neighbor_topk", 69))
        ),
        continuous_glass_neighbor_pick=max(
            1, int(getattr(args, "material_chromatic_repair_glass_neighbor_pick", 69))
        ),
        continuous_glass_nd_noise=max(
            0.0,
            float(getattr(args, "material_chromatic_repair_glass_nd_noise", 0.0005)),
        ),
        continuous_glass_vd_noise=max(
            0.0,
            float(getattr(args, "material_chromatic_repair_glass_vd_noise", 0.12)),
        ),
    )


def prepare_case(
    *,
    source: Path,
    case_dir: Path,
    merit: dict[str, Any],
    glasses: list[dict[str, Any]],
    args: argparse.Namespace,
    seed: int,
) -> tuple[dict[str, Any], dict[str, Any]]:
    system = copy.deepcopy(read_json(material_bridge.source_file(source, "system.json")))
    nodes = tuple(
        material_bridge.parse_float_list(
            str(
                getattr(
                    args,
                    "material_chromatic_repair_field_hy_fractions",
                    "0,0.25,0.5,0.75,1",
                )
            )
        )
    ) or (0.0, 0.25, 0.5, 0.75, 1.0)
    # The aperture-realized bridge has already solved this exact field grid.
    # Replacing it with zero-vignetting records silently changes the sampled
    # pupil back to a full pupil, which can invalidate an otherwise traceable
    # system.  Preserve the complete field records and fail closed if an
    # active-vignetting source has not been bridged to every requested node.
    system["fields"] = material_bridge.fields_on_exact_nodes(
        system,
        nodes,
        allow_zero_for_missing=material_bridge.minimum_relative_illumination(merit)
        <= 0.0,
    )
    # The preceding stage's variable document is the stage-to-stage contract.
    # Preserve its complete variable combination; this repair must not invent a
    # narrower geometry/material schema of its own.
    variables_doc = feedback_cycles.variables_doc_from_system(source, system)
    variables_doc = {
        "variables": server.sync_variables_from_system(
            server.ensure_vignetting_variables_for_system(
                system,
                list(variables_doc.get("variables") or []),
                merit,
                handoff=True,
            ),
            system,
        )
    }
    variables = list(variables_doc.get("variables") or [])
    common_merit = copy.deepcopy(merit)
    common_merit["num_rays"] = max(
        1, int(getattr(args, "material_chromatic_repair_trace_rays", 3))
    )
    common_merit["pupil_arms"] = max(
        1, int(getattr(args, "material_chromatic_repair_trace_arms", 6))
    )
    common_merit["pupil_distribution"] = str(
        getattr(
            args,
            "material_chromatic_repair_trace_distribution",
            "gaussian_ring_arm",
        )
        or "gaussian_ring_arm"
    )
    case_dir.mkdir(parents=True, exist_ok=True)
    write_json(case_dir / "system_input.json", system)
    write_json(case_dir / "variables.json", variables_doc)
    write_json(case_dir / "merit.json", common_merit)

    population = max(
        20, int(getattr(args, "material_chromatic_repair_population", 320))
    )
    rows = feedback_cycles.build_population(
        system=system,
        variables_doc=variables_doc,
        glasses=glasses,
        material_mode="continuous",
        population=population,
        seed=seed,
        scale=str(getattr(args, "material_chromatic_repair_population_scale", "wide")),
        args=population_args(args),
    )
    population_path = case_dir / "initial_population.csv"
    feedback_cycles.write_population(population_path, rows)
    prepared = {
        "case_dir": case_dir,
        "system": system,
        "variables_doc": variables_doc,
        "variables": variables,
        "continuous_material_de": material_bridge.continuous_material_variable_contract(
            variables_doc
        ),
        "population_path": population_path,
        "population_rows": len(rows),
        "generations": max(
            1, int(getattr(args, "material_chromatic_repair_generations", 1200))
        ),
        "seed": seed,
        "purpose": "material_chromatic_weak_geometry_repair",
        "profile": "material_repair",
        "constraint_handling": "scalar_merit",
        "aiming_mode": material_bridge.system_aiming_mode(system),
    }
    return prepared, common_merit


def cached_stage_rows(
    *,
    stage_dir: Path,
    entry_rows: list[dict[str, Any]],
    sources: list[Path],
    runtime_args: argparse.Namespace,
    args: argparse.Namespace,
) -> list[dict[str, Any]] | None:
    summary_path = stage_dir / "repair_summary.json"
    parent_audit_path = stage_dir / "parent_score_audit.json"
    if not summary_path.is_file() or not parent_audit_path.is_file():
        return None
    summary = read_json(summary_path)
    parent_audit = read_json(parent_audit_path)
    summary_rows = list(summary.get("rows") or [])
    parent_rows = list(parent_audit.get("rows") or [])
    if len(summary_rows) != len(entry_rows) or len(parent_rows) != len(entry_rows):
        return None
    if not bool(parent_audit.get("all_valid", False)):
        return None
    expected = [
        (
            int(finite_float(row.get("basin_rank"), position)),
            str(source.resolve()),
        )
        for position, (row, source) in enumerate(
            zip(entry_rows, sources, strict=True), start=1
        )
    ]
    actual = [
        (
            int(finite_float(row.get("basin_rank"), position)),
            str(Path(str(row.get("source_dir") or "")).resolve()),
        )
        for position, row in enumerate(summary_rows, start=1)
    ]
    parent_actual = [
        (
            int(finite_float(row.get("basin_rank"), position)),
            str(Path(str(row.get("source_dir") or "")).resolve()),
        )
        for position, row in enumerate(parent_rows, start=1)
    ]
    if actual != expected or parent_actual != expected:
        return None
    if any(
        not bool(row.get("valid", False))
        or not bool(row.get("parent_seed_contract_match", False))
        or not bool(
            row.get(
                "parent_cpu_cuda_repair_seed_admissible",
                row.get("parent_cpu_cuda_consistent", False),
            )
        )
        for row in parent_rows
    ):
        return None

    out: list[dict[str, Any]] = []
    for source_row, audit in zip(entry_rows, summary_rows, strict=True):
        if not bool(audit.get("accepted", False)):
            return None
        endpoint = Path(str(audit.get("result_dir") or "")).resolve()
        score_path = endpoint / "score.json"
        result_path = endpoint.parent / "experimental_result" / "result.json"
        if not score_path.is_file() or not result_path.is_file():
            return None
        score = read_json(score_path)
        result = read_json(result_path)
        scored = {
            **score,
            "returncode": 0,
            "timed_out": False,
        }
        if not (
            bool(result.get("materialization_valid", False))
            and material_bridge.trace_acceptable(scored, runtime_args)
            and 0.0 < finite_float(score.get("merit"), math.inf) < 1.0e12
            and finite_float(score.get("min_edge"), -math.inf)
            >= float(getattr(args, "material_bridge_repair_min_edge", 0.0))
        ):
            return None
        row = dict(source_row)
        material_bridge.apply_effective_score(
            row,
            score,
            endpoint,
            "material_chromatic_weak_geometry_repair",
        )
        row.update(material_bridge.normalized_entry_row(row, args))
        row.update(
            {
                "material_chromatic_repair_attempted": True,
                "material_chromatic_repair_accepted": True,
                "material_chromatic_repair_seed": audit.get("seed"),
                "material_chromatic_repair_parent_merit": audit.get("parent_merit"),
                "material_chromatic_repair_result_merit": audit.get("result_merit"),
                "material_chromatic_repair_relative_improvement": audit.get(
                    "relative_improvement"
                ),
                "material_chromatic_repair_result_dir": str(endpoint),
                "material_chromatic_repair_acceptance_rule": "common_merit_strict_improvement",
                "material_chromatic_repair_stage_cache_hit": True,
            }
        )
        out.append(row)
    summary["contract"] = MATERIAL_CHROMATIC_STAGE_CONTRACT
    summary["stage_cache_hit"] = True
    write_json(summary_path, summary)
    return out


def parent_repair_seed_valid(
    parent_score: dict[str, Any],
    runtime_args: argparse.Namespace,
    *,
    seed_contract_match: bool,
) -> bool:
    """Return whether a parent is safe to *attempt* chromatic repair.

    A negative illumination edge after strict Set Vignetting is repair work,
    not evidence that the optical seed is unusable.  Requiring ``min_edge``
    to be non-negative here would discard exactly the candidates this stage
    owns.  The repaired endpoint still has to satisfy the non-negative edge
    gate before it can replace its parent.
    """

    trace_score = dict(parent_score)
    trace_score.pop("cpu_cuda_consistent", None)
    cpu_cuda_admissible = parent_repair_seed_cpu_cuda_admissible(
        parent_score,
        runtime_args,
    )
    return (
        material_bridge.trace_acceptable(trace_score, runtime_args)
        and 0.0 < finite_float(parent_score.get("merit"), math.inf) < 1.0e12
        and cpu_cuda_admissible
        and seed_contract_match
    )


def parent_repair_seed_cpu_cuda_admissible(
    parent_score: dict[str, Any],
    runtime_args: argparse.Namespace,
) -> bool:
    """Keep strict parity, with one narrow allowance for a repairable edge.

    CUDA and CPU can differ by a few relative 1e-5 in an already-negative
    finite-ray edge thickness.  Its steep manufacturing penalty magnifies that
    harmless geometric delta.  This allowance applies only when both scores
    agree that the parent violates the edge gate; accepted repaired endpoints
    continue to use the ordinary strict bridge consistency contract.
    """

    if "cpu_cuda_consistent" not in parent_score:
        return True
    if bool(parent_score.get("cpu_cuda_consistent")):
        return True
    edge_gate = float(getattr(runtime_args, "material_bridge_repair_min_edge", 0.0))
    cpu_edge = finite_float(parent_score.get("min_edge"), math.inf)
    cuda_edge = finite_float(parent_score.get("optimizer_min_edge"), -math.inf)
    edge_difference = abs(cpu_edge - cuda_edge)
    edge_scale = max(abs(cpu_edge), abs(cuda_edge), 1.0e-12)
    edge_tolerance = max(
        material_bridge.MATERIAL_BRIDGE_CPU_CUDA_EDGE_TOL,
        5.0e-5 * edge_scale,
    )
    return (
        bool(parent_score.get("cpu_cuda_trace_match"))
        and finite_float(
            parent_score.get("cpu_cuda_coverage_max_abs_difference"),
            math.inf,
        )
        <= material_bridge.MATERIAL_BRIDGE_CPU_CUDA_COVERAGE_TOL
        and finite_float(
            parent_score.get("cpu_cuda_merit_relative_difference"),
            math.inf,
        )
        <= material_bridge.MATERIAL_BRIDGE_CPU_CUDA_MERIT_REL_TOL
        and cpu_edge < edge_gate
        and cuda_edge < edge_gate
        and edge_difference <= edge_tolerance
    )


def run_stage(
    args: argparse.Namespace,
    entry_rows: list[dict[str, Any]],
    *,
    material_context_run: Path,
) -> tuple[list[dict[str, Any]], Path]:
    stage_dir = server.native_io_paths.named_child(Path(args.out_dir).resolve(), 'layer2_material_chromatic_repair')
    stage_dir.mkdir(parents=True, exist_ok=True)
    merit_path = (
        Path(args.material_merit_template).resolve()
        if getattr(args, "material_merit_template", None)
        else material_bridge.source_file(material_context_run.resolve(), "merit.json")
    )
    merit = read_json(merit_path)
    chromatic_contract = assert_chromatic_merit(merit)
    if not entry_rows:
        write_json(
            stage_dir / "repair_summary.json",
            {
                "enabled": True,
                "skipped": True,
                "reason": "no material-entry candidates",
                "seed_mode": "single_shared_seed",
                "chromatic_merit_contract": chromatic_contract,
                "rows": [],
            },
        )
        return [], stage_dir
    sources = [
        Path(str(row.get("out_dir") or "")).resolve()
        for row in entry_rows
        if str(row.get("out_dir") or "").strip()
    ]
    if len(sources) != len(entry_rows):
        raise RuntimeError(
            "every material-entry candidate must provide out_dir before chromatic repair"
        )
    systems = [
        read_json(material_bridge.source_file(source, "system.json"))
        for source in sources
    ]
    libraries = feedback_cycles.glass_libraries_for_systems(systems)
    database = Path(
        str(
            getattr(args, "material_chromatic_repair_glass_database", "")
            or getattr(args, "glass_scout_glass_database", feedback_cycles.DEFAULT_DATABASE)
        )
    )
    glasses = feedback_cycles.load_glasses(
        database,
        libraries,
        feedback_cycles.systems_wavelengths_um(systems),
    )
    runtime_args = repair_runtime_args(args)
    exe = material_bridge.bridge_executable(runtime_args)
    cached = cached_stage_rows(
        stage_dir=stage_dir,
        entry_rows=entry_rows,
        sources=sources,
        runtime_args=runtime_args,
        args=args,
    )
    if cached is not None:
        print(
            "[material chromatic repair] reuse completed CPU-materialized stage",
            flush=True,
        )
        return cached, stage_dir
    base_seed = int(getattr(args, "material_chromatic_repair_seed", 20260804))
    prepared: list[dict[str, Any]] = []
    contexts: list[dict[str, Any]] = []
    parent_audit_rows: list[dict[str, Any]] = []
    for position, (row, source) in enumerate(zip(entry_rows, sources, strict=True), start=1):
        rank = int(finite_float(row.get("basin_rank"), position))
        case_dir = stage_dir / f"repair_{position:03d}_rank_{rank:04d}"
        item, common_merit = prepare_case(
            source=source,
            case_dir=case_dir,
            merit=merit,
            glasses=glasses,
            args=args,
            seed=base_seed,
        )
        parent_score = material_bridge.score_parent(
            source=case_dir,
            case_dir=case_dir / "parent_score",
            merit=common_merit,
            exe=exe,
            args=runtime_args,
        )
        parent_variables_doc = read_json(case_dir / "parent_score" / "variables.json")
        parent_seed_contract_match = parent_variables_doc == item["variables_doc"]
        parent_cpu_cuda_admissible = parent_repair_seed_cpu_cuda_admissible(
            parent_score,
            runtime_args,
        )
        parent_valid = parent_repair_seed_valid(
            parent_score,
            runtime_args,
            seed_contract_match=parent_seed_contract_match,
        )
        parent_audit_rows.append(
            {
                "basin_rank": rank,
                "source_dir": str(source),
                "valid": parent_valid,
                "parent_seed_contract_match": parent_seed_contract_match,
                "parent_cpu_cuda_repair_seed_admissible": (
                    parent_cpu_cuda_admissible
                ),
                "parent_cpu_cuda_repair_seed_allowance_used": (
                    parent_cpu_cuda_admissible
                    and not bool(parent_score.get("cpu_cuda_consistent", True))
                ),
                **material_bridge.metric_columns("parent", parent_score),
            }
        )
        if not parent_valid:
            continue
        prepared.append(item)
        contexts.append(
            {
                "row": dict(row),
                "source": source,
                "rank": rank,
                "case_dir": case_dir,
                "parent_score": parent_score,
            }
        )

    material_bridge.write_csv(
        stage_dir / "parent_score_audit.csv", parent_audit_rows
    )
    write_json(
        stage_dir / "parent_score_audit.json",
        {
            "contract": material_bridge.MATERIAL_BRIDGE_SCORE_CONTRACT,
            "candidate_count": len(entry_rows),
            "valid_count": len(prepared),
            "all_valid": len(prepared) == len(entry_rows),
            "rows": parent_audit_rows,
        },
    )
    if len(prepared) != len(entry_rows):
        rejected = [
            int(finite_float(row.get("basin_rank"), -1))
            for row in parent_audit_rows
            if not bool(row.get("valid"))
        ]
        raise RuntimeError(
            "pre-Material-Funnel parent contract rejected candidates before DE: "
            f"ranks={rejected}, audit={stage_dir / 'parent_score_audit.json'}"
        )

    policy = material_bridge.material_bridge_multi_island_policy(runtime_args, prepared)
    write_json(stage_dir / "multi_island_policy.json", policy)
    if policy["used"]:
        # Once a batched wave has started, never restart the same work in
        # serial mode after an exception.  Preserve its checkpoint artifacts
        # and surface the defect to the resume controller.
        results = material_bridge.run_prepared_de_batched(
            prepared,
            exe=exe,
            args=runtime_args,
            wave_name="material_chromatic_weak_geometry_repair",
            policy=policy,
        )
    else:
        results = [
            material_bridge.run_prepared_de_serial(item, exe, runtime_args)
            for item in prepared
        ]

    output_rows: list[dict[str, Any]] = []
    audit_rows: list[dict[str, Any]] = []
    for context, result in zip(contexts, results, strict=True):
        row = context["row"]
        parent_score = context["parent_score"]
        before = finite_float(parent_score.get("merit"), math.inf)
        after = finite_float(result.get("merit"), math.inf)
        accepted = (
            material_bridge.trace_acceptable(result, runtime_args)
            and 0.0 < after < 1.0e12
            and finite_float(result.get("min_edge"), -math.inf)
            >= float(getattr(args, "material_bridge_repair_min_edge", 0.0))
            and after < before
        )
        relative_improvement = (
            (before - after) / max(abs(before), 1.0e-12)
            if math.isfinite(before) and math.isfinite(after)
            else -math.inf
        )
        endpoint = Path(str(result.get("result_dir") or "")).resolve()
        if accepted:
            material_bridge.apply_effective_score(
                row,
                result,
                endpoint,
                "material_chromatic_weak_geometry_repair",
            )
            row.update(material_bridge.normalized_entry_row(row, args))
            material_bridge.write_bridge_basin_meta(row, endpoint)
        row.update(
            {
                "material_chromatic_repair_attempted": True,
                "material_chromatic_repair_accepted": accepted,
                "material_chromatic_repair_seed": base_seed,
                "material_chromatic_repair_parent_merit": before,
                "material_chromatic_repair_result_merit": after,
                "material_chromatic_repair_relative_improvement": relative_improvement,
                "material_chromatic_repair_result_dir": str(endpoint),
                "material_chromatic_repair_acceptance_rule": "common_merit_strict_improvement",
            }
        )
        output_rows.append(row)
        audit_rows.append(
            {
                "basin_rank": context["rank"],
                "source_dir": str(context["source"]),
                "result_dir": str(endpoint),
                "accepted": accepted,
                "seed": base_seed,
                "parent_merit": before,
                "result_merit": after,
                "relative_improvement": relative_improvement,
                "returncode": result.get("returncode"),
                "execution_mode": result.get("execution_mode"),
                "wall_seconds": result.get("wall_seconds"),
                "constraint_group_error_audit_only": result.get(
                    "constraint_group_error"
                ),
            }
        )
    material_bridge.write_csv(stage_dir / "repair_summary.csv", audit_rows)
    write_json(
        stage_dir / "repair_summary.json",
        {
            "contract": MATERIAL_CHROMATIC_STAGE_CONTRACT,
            "stage_cache_hit": False,
            "enabled": True,
            "position": "after_material_entry_selection_before_material_funnel",
            "seed_mode": "single_shared_seed",
            "seed": base_seed,
            "acceptance_rule": "common_merit_strict_improvement",
            "constraint_group_error_role": "audit_only",
            "glass_proximity_role": (
                "population_neighbor_bias_and_inherited_common_merit; "
                "no acceptance gate"
            ),
            "chromatic_merit_contract": chromatic_contract,
            "population": int(getattr(args, "material_chromatic_repair_population", 320)),
            "generations": int(getattr(args, "material_chromatic_repair_generations", 1200)),
            "multi_island": policy,
            "rows": audit_rows,
        },
    )
    return output_rows, stage_dir
