"""Prepare, run and finish one refine-like fixed-glass feedback case."""
from __future__ import annotations
from typing import Any
from .settings import FIXED_GLASS_SEED_POLICY_STABLE
from .contracts import MaterialPolicy
from pathlib import Path
from .feedback_contracts import add_refine_like_feedback_args
import argparse
from .feedback_contracts import configure_refine_like_feedback_evaluator
from lensde.helpers import run_explore_dls_feedback_cycles as feedback_cycles
from .feedback_contracts import inherit_refine_like_aiming_from_system
from lensde.workflow import run_material5_bridge_selection as material_bridge
from .common import read_json
from lensde.engine import core as server
from .common import finite_float
import math


def prepare_refine_like_feedback_case(
    *,
    source_rank: int,
    source_dir: Path,
    material_mode: str,
    algorithm: str,
    out_root: Path,
    glasses: list[dict[str, Any]],
    args: argparse.Namespace,
    _version: MaterialPolicy,
) -> dict[str, Any]:
    source_name = source_dir.name
    source_system = read_json(feedback_cycles.file_from_source(source_dir, "system.json"))
    parent_score = read_json(feedback_cycles.file_from_source(source_dir, "score.json"))
    source_command = feedback_cycles.load_command(source_dir)
    if material_mode == "true_glass":
        problem_system, variables_doc, glass_search = feedback_cycles.true_glass_problem(
            source_dir=source_dir, system=source_system, glasses=glasses, args=args
        )
    elif material_mode == "continuous":
        problem_system, variables_doc, glass_search = feedback_cycles.continuous_problem(
            source_dir, source_system
        )
    elif material_mode == "locked_glass":
        problem_system, variables_doc, glass_search = feedback_cycles.locked_glass_problem(
            source_dir, source_system
        )
    else:
        raise ValueError(f"Unknown material mode: {material_mode}")
    problem_system = feedback_cycles.with_field_hy_fractions(
        problem_system, str(getattr(args, "field_hy_fractions", "") or "")
    )
    case_root = server.native_io_paths.named_path(
        out_root,
        f"refine_like/{source_rank:02d}_{feedback_cycles.safe_name(source_name)}/{feedback_cycles.safe_name(material_mode)}_{feedback_cycles.safe_name(algorithm)}",
    )
    system_path = case_root / "system_input.json"
    variables_path = case_root / "variables.json"
    merit_path = case_root / "merit.json"
    seed_path = case_root / "seed_population.csv"
    glass_path = case_root / "glass_search_config.json" if glass_search else None
    case_out = case_root / "run"
    feedback_cycles.write_json(system_path, problem_system)
    feedback_cycles.write_json(variables_path, variables_doc)
    feedback_merit = feedback_cycles.adjusted_merit(source_dir, args, material_mode)
    feedback_cycles.write_json(merit_path, feedback_merit)
    if glass_search and glass_path:
        feedback_cycles.write_json(glass_path, glass_search)
    seed_contract = _version.seed_contract(
        source_system=source_system,
        material_mode=material_mode,
        algorithm=algorithm,
        source_rank=source_rank,
        optimizer_seed_base=int(args.seed),
        population_seed_base=int(args.rng_seed),
        seed_profile=str(getattr(args, "seed_profile", server.seed_protocol.GENERIC_PROFILE)),
        master_seed=getattr(args, "master_seed", None),
        seed_policy=str(getattr(args, "seed_policy", FIXED_GLASS_SEED_POLICY_STABLE)),
    )
    population = feedback_cycles.build_population(
        system=problem_system,
        variables_doc=variables_doc,
        glasses=glasses,
        material_mode=material_mode,
        population=int(args.population),
        seed=int(seed_contract["population_seed"]),
        scale=str(args.population_scale),
        args=args,
    )
    feedback_cycles.write_population(seed_path, population)
    command = feedback_cycles.make_command(
        source_command=source_command,
        system_path=system_path,
        merit_path=merit_path,
        variables_path=variables_path,
        seed_path=seed_path,
        glass_path=glass_path,
        out_dir=case_out,
        algorithm=algorithm,
        seed=int(seed_contract["optimizer_seed"]),
        args=args,
    )
    command = add_refine_like_feedback_args(command, args)
    command = configure_refine_like_feedback_evaluator(command, args)
    command = inherit_refine_like_aiming_from_system(command, problem_system)
    feedback_cycles.write_json(case_root / "command.json", command)
    feedback_cycles.write_json(
        case_root / "case_input_summary.json",
        {
            "source_rank": source_rank,
            "source_dir": str(source_dir),
            "material_mode": material_mode,
            "algorithm": algorithm,
            "repair_mode": "refine_like",
            "population_rows": len(population),
            "variable_count": len(variables_doc.get("variables") or []),
            "glass_group_count": len((glass_search or {}).get("groups") or []),
            "outer": int(getattr(args, "refine_outer", 80)),
            "de_gen": int(getattr(args, "refine_de_gen", args.generations)),
            "de_evaluator": "CUDA" if bool(getattr(args, "de_use_cuda", False)) else "CPU",
            "optimizer_contract": "pure_adaptive_de_no_dls",
            "polish_enabled": False,
            "dls_max_iterations": 0,
            **seed_contract,
            "field_hy_fractions": feedback_cycles.parse_field_hy_fractions(
                str(getattr(args, "field_hy_fractions", "") or "")
            ),
            "field_count": len(problem_system.get("fields", []) or []),
        },
    )
    print(
        f"refine-like source {source_rank} {source_name} {material_mode}/{algorithm}: pop={len(population)} vars={len(variables_doc.get('variables') or [])} outer={int(getattr(args, 'refine_outer', 80))} de_gen={int(getattr(args, 'refine_de_gen', args.generations))}",
        flush=True,
    )
    return {
        "source_rank": source_rank,
        "source_dir": source_dir,
        "source_name": source_name,
        "material_mode": material_mode,
        "algorithm": algorithm,
        "parent_score": parent_score,
        "source_system": source_system,
        "system": problem_system,
        "variables_doc": variables_doc,
        "variables": list(variables_doc.get("variables") or []),
        "merit": feedback_merit,
        "case_dir": case_root,
        "case_out": case_out,
        "command": command,
        "population_path": seed_path,
        "population_rows": len(population),
        "generations": int(getattr(args, "refine_de_gen", getattr(args, "generations", 420))),
        "seed": int(seed_contract["optimizer_seed"]),
        **seed_contract,
        "profile": "fixed_glass_feedback",
        "constraint_handling": str(getattr(args, "constraint_handling", "feasibility_rules")),
        "aiming_mode": material_bridge.system_aiming_mode(problem_system),
    }


def finish_refine_like_feedback_case(
    prepared: dict[str, Any], run_result: dict[str, Any], args: argparse.Namespace
) -> dict[str, Any]:
    case_root = Path(prepared["case_dir"])
    case_out = Path(prepared["case_out"])
    score_path = case_out / "score.json"
    system_out_path = case_out / "system.json"
    score = read_json(score_path) if score_path.exists() else {}
    system_out = (
        read_json(system_out_path) if system_out_path.exists() else prepared["source_system"]
    )
    row = feedback_cycles.score_to_row(
        cycle=1,
        source_rank=int(prepared["source_rank"]),
        source_dir=Path(prepared["source_dir"]),
        source_name=str(prepared["source_name"]),
        material_mode=str(prepared["material_mode"]),
        algorithm=str(prepared["algorithm"]),
        parent_score=prepared["parent_score"],
        score=score,
        system=system_out,
        run_result=run_result,
        out_dir=case_out,
        args=args,
        merit=prepared["merit"],
    )
    row["repair_mode"] = "refine_like"
    row["optimizer_contract"] = "pure_adaptive_de_no_dls"
    row["execution_mode"] = run_result.get("execution_mode", "serial_pure_de")
    row["dls_iterations"] = int(finite_float(run_result.get("dls_iterations"), 0.0))
    row["dls_accepted_steps"] = int(finite_float(run_result.get("dls_accepted_steps"), 0.0))
    row["dls_rejected_steps"] = int(finite_float(run_result.get("dls_rejected_steps"), 0.0))
    row["outer"] = int(getattr(args, "refine_outer", 80))
    row["de_gen"] = int(getattr(args, "refine_de_gen", args.generations))
    row["case_dir"] = str(case_root)
    row["parent_merit"] = prepared["parent_score"].get("merit")
    row["parent_constraint_group_error"] = prepared["parent_score"].get("constraint_group_error")
    row["parent_min_edge"] = prepared["parent_score"].get("min_edge")
    row["parent_trace_coverage"] = prepared["parent_score"].get("trace_coverage")
    row["parent_min_field_coverage"] = prepared["parent_score"].get("min_field_coverage")
    row["locked_parent_score_path"] = str(prepared.get("locked_parent_score_path") or "")
    if isinstance(prepared.get("source_parent_score"), dict):
        row["source_parent_merit"] = prepared["source_parent_score"].get("merit")
    for key in (
        "optimizer_best_merit",
        "materialized_merit",
        "materialization_merit_delta",
        "materialization_merit_relative_difference",
        "materialization_merit_relative_tolerance",
        "cpu_cuda_materialization_consistent",
        "materialization_valid",
    ):
        if key in run_result:
            row[key] = run_result.get(key)
    for key in (
        "seed_policy",
        "seed_key",
        "optimizer_seed_base",
        "population_seed_base",
        "optimizer_seed",
        "population_seed",
    ):
        row[key] = prepared[key]
    feedback_cycles.write_json(case_root / "summary.json", row)
    print(
        f"  -> mean={finite_float(row.get('mean_spot_um')):.3f}um merit={finite_float(row.get('merit')):.6g} dist={finite_float(row.get('max_distortion')):.4g} constraint={finite_float(row.get('constraint_group_error')):.4g} edge={finite_float(row.get('min_edge'), -math.inf):.4g} return={run_result.get('returncode')} seconds={finite_float(run_result.get('seconds'), 0.0):.1f}",
        flush=True,
    )
    return row


def run_prepared_refine_like_feedback_serial(
    prepared: dict[str, Any], args: argparse.Namespace
) -> dict[str, Any]:
    result = feedback_cycles.run_command(
        list(prepared["command"]),
        feedback_cycles.ROOT,
        Path(prepared["case_dir"]) / "run.log",
        int(args.timeout),
        int(getattr(args, "de_resolved_child_cpu_workers", 0)),
    )
    result.update(
        {
            "execution_mode": "serial_pure_de",
            "dls_iterations": 0,
            "dls_accepted_steps": 0,
            "dls_rejected_steps": 0,
        }
    )
    return finish_refine_like_feedback_case(prepared, result, args)


def run_refine_like_feedback_case(
    *,
    source_rank: int,
    source_dir: Path,
    material_mode: str,
    algorithm: str,
    out_root: Path,
    glasses: list[dict[str, Any]],
    args: argparse.Namespace,
    _version: MaterialPolicy,
) -> dict[str, Any]:
    prepared = _version.prepare_refine_like_feedback_case(
        source_rank=source_rank,
        source_dir=source_dir,
        material_mode=material_mode,
        algorithm=algorithm,
        out_root=out_root,
        glasses=glasses,
        args=args,
    )
    return run_prepared_refine_like_feedback_serial(prepared, args)
