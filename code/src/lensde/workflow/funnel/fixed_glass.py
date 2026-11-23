"""Assemble the locked-glass feedback configuration and choose its cycle/refine-like route."""
from __future__ import annotations
from typing import Any
from .contracts import MaterialPolicy
from pathlib import Path
import argparse
from lensde.helpers import run_explore_dls_feedback_cycles as feedback_cycles
from .common import read_csv
from .material import source_dirs_from_material_ranked
from .common import write_csv
from .common import write_json
from .settings import FIXED_GLASS_SEED_POLICY_STABLE
from .common import finite_float
import json
import math
from .common import read_json
from lensde.engine import core as server
from .settings import DE_DLS_TRACE_COVERAGE_BASELINE


def run_fixed_glass_feedback_repair(
    args: argparse.Namespace,
    material_dir: Path | None,
    direct_source_rows: list[dict[str, Any]] | None = None,
    *,
    _version: MaterialPolicy,
) -> Path | None:
    if not bool(getattr(args, "fixed_glass_feedback_enabled", False)):
        return None
    direct_source_rows = list(direct_source_rows or [])
    if direct_source_rows:
        source_limit = int(getattr(args, "fixed_glass_feedback_source_count", 2))
        source_dirs = [
            Path(str(row.get("out_dir"))).resolve()
            for row in direct_source_rows[:source_limit]
            if str(row.get("out_dir") or "").strip()
        ]
    else:
        if material_dir is None:
            return None
        source_dirs = source_dirs_from_material_ranked(
            material_dir, int(getattr(args, "fixed_glass_feedback_source_count", 2))
        )
    inherited_min_edge, feedback_dir, repair_mode, config = prepare_fixed_glass_feedback_contract(
        source_dirs, args, direct_source_rows, material_dir, _version=_version
    )
    if not source_dirs:
        write_json(
            feedback_dir / "fixed_glass_feedback_skipped.json",
            {**config, "skipped": True, "reason": "no ranked material source dirs"},
        )
        return feedback_dir
    (namespace,) = fixed_glass_feedback_namespace(
        args, source_dirs, feedback_dir, inherited_min_edge, _version=_version
    )
    if repair_mode == "refine_like":
        print(
            f"=== fixed-glass feedback repair: mode=refine_like sources={len(source_dirs)} algorithms={namespace.algorithms} modes={namespace.material_modes} outer={namespace.refine_outer} de_gen={namespace.refine_de_gen} ===",
            flush=True,
        )
        _version.run_refine_like_feedback(namespace, source_dirs, feedback_dir)
    else:
        print(
            f"=== fixed-glass feedback repair: mode=cycle sources={len(source_dirs)} cycles={namespace.cycles} algorithms={namespace.algorithms} modes={namespace.material_modes} gen={namespace.generations} ===",
            flush=True,
        )
        feedback_cycles.run(namespace)
        ranked = feedback_dir / "cycle_summary.csv"
        if ranked.exists():
            rows = read_csv(ranked)
            write_csv(feedback_dir / "fixed_glass_feedback_ranked.csv", rows)
    return feedback_dir


def prepare_fixed_glass_feedback_contract(
    source_dirs, args, direct_source_rows, material_dir, *, _version: MaterialPolicy
):
    """Inherit source edge bounds and persist the selected locked-glass repair contract."""
    inherited_edge_constraints: dict[str, float] = {}
    for source_dir in source_dirs:
        try:
            source_merit = read_json(feedback_cycles.file_from_source(source_dir, "merit.json"))
        except (FileNotFoundError, OSError, ValueError, json.JSONDecodeError):
            continue
        for key in [
            "weight_edge",
            "min_edge_thickness",
            "glass_edge_thickness_lower",
            "glass_edge_thickness_upper",
            "air_edge_thickness_lower",
            "air_edge_thickness_upper",
        ]:
            value = finite_float(source_merit.get(key))
            if math.isfinite(value):
                inherited_edge_constraints[key] = value
        break
    inherited_min_edge = inherited_edge_constraints.get("min_edge_thickness", 0.0)
    feedback_dir_arg = getattr(args, "fixed_glass_feedback_out_dir", None)
    feedback_dir = (
        Path(feedback_dir_arg).resolve()
        if feedback_dir_arg
        else server.native_io_paths.named_child(args.out_dir.resolve(), "layer2_direct_glass_de")
        if direct_source_rows
        else server.native_io_paths.named_child(material_dir, "fixed_glass_feedback_repair")
    )
    repair_mode = (
        str(getattr(args, "fixed_glass_feedback_mode", "cycle") or "cycle").strip().lower()
    )
    if repair_mode not in {"cycle", "refine_like"}:
        repair_mode = "cycle"
    config = {
        "enabled": True,
        "mode": repair_mode,
        "material_dir": str(material_dir) if material_dir else "",
        "direct_bridge_sources": bool(direct_source_rows),
        "source_dirs": [str(path) for path in source_dirs],
        "source_count": int(getattr(args, "fixed_glass_feedback_source_count", 2)),
        "algorithms": str(getattr(args, "fixed_glass_feedback_algorithms", "adaptive_jade")),
        "material_modes": str(getattr(args, "fixed_glass_feedback_material_modes", "continuous")),
        "glass_candidates": int(getattr(args, "fixed_glass_feedback_glass_candidates", 14)),
        "population": int(getattr(args, "fixed_glass_feedback_population", 240)),
        "trace_rays": max(1, int(getattr(args, "fixed_glass_feedback_trace_rays", 3))),
        "trace_arms": max(1, int(getattr(args, "fixed_glass_feedback_trace_arms", 6))),
        "trace_distribution": str(
            getattr(args, "fixed_glass_feedback_trace_distribution", "gaussian_ring_arm")
        ),
        "field_hy_fractions": feedback_cycles.parse_field_hy_fractions(
            str(getattr(args, "fixed_glass_feedback_field_hy_fractions", "") or "")
        ),
        "de_evaluator": "CUDA"
        if bool(getattr(args, "fixed_glass_feedback_de_use_cuda", False))
        else "CPU",
        "optimizer_contract": "pure_adaptive_de_no_dls",
        "multi_island": {
            "default_enabled": True,
            "requested": bool(getattr(args, "fixed_glass_feedback_multi_island", True)),
            "coalesce_us": max(
                0, int(getattr(args, "fixed_glass_feedback_multi_island_coalesce_us", 1000))
            ),
        },
        "de_executable": str(
            Path(
                str(
                    getattr(args, "fixed_glass_feedback_de_cuda_exe", "")
                    or _version.cpp_dir / "build" / "lensde_de_cuda.exe"
                )
            ).resolve()
            if bool(getattr(args, "fixed_glass_feedback_de_use_cuda", False))
            else Path(args.exe).resolve()
        ),
        "de_workers": max(1, int(getattr(args, "fixed_glass_feedback_de_workers", 1))),
        "de_child_cpu_workers": max(
            0, int(getattr(args, "fixed_glass_feedback_de_child_cpu_workers", 0))
        ),
        "de_cuda_min_batch": max(
            1, int(getattr(args, "fixed_glass_feedback_de_cuda_min_batch", 1))
        ),
        "seed_policy": str(
            getattr(args, "fixed_glass_feedback_seed_policy", FIXED_GLASS_SEED_POLICY_STABLE)
        ),
        "seed_profile": str(getattr(args, "seed_profile", server.seed_protocol.GENERIC_PROFILE)),
        "master_seed": getattr(args, "master_seed", None),
        "constraint_handling": str(
            getattr(args, "fixed_glass_feedback_constraint_handling", "feasibility_rules")
        ),
        "hybrid_parents": int(getattr(args, "fixed_glass_feedback_hybrid_parents", 10)),
        "polish_candidates": 0,
        "local_iter": 0,
        "merit_policy": {
            "source": "inherited_per_source_candidate",
            "inherited": [
                "image_quality",
                "distortion",
                "first_order",
                "manufacturing_constraints",
            ],
            "feedback_overrides": ["fields"],
            "ray_sampling": "shared_across_pure_de_material_stages",
        },
        "min_edge": inherited_min_edge,
        "edge_constraints": {"source": "inherited_global_merit", **inherited_edge_constraints},
    }
    if repair_mode == "cycle":
        config["cycle"] = {
            "cycles": int(getattr(args, "fixed_glass_feedback_cycles", 2)),
            "elites_per_cycle": int(getattr(args, "fixed_glass_feedback_elites_per_cycle", 2)),
            "generations": int(getattr(args, "fixed_glass_feedback_generations", 420)),
        }
    else:
        config["refine_like"] = {
            "refine_outer": int(getattr(args, "fixed_glass_feedback_refine_outer", 80)),
            "refine_de_gen": int(getattr(args, "fixed_glass_feedback_refine_de_gen", 400)),
            "polish_refill_max_extra": 0,
            "early_stop": bool(getattr(args, "fixed_glass_feedback_refine_early_stop", True)),
            "early_stop_min_epochs": int(
                getattr(args, "fixed_glass_feedback_refine_early_stop_min_epochs", 4)
            ),
            "early_stop_window": int(
                getattr(args, "fixed_glass_feedback_refine_early_stop_window", 3)
            ),
            "early_stop_min_rel_improve": float(
                getattr(args, "fixed_glass_feedback_refine_early_stop_min_rel_improve", 0.01)
            ),
            "early_stop_min_abs_improve": float(
                getattr(args, "fixed_glass_feedback_refine_early_stop_min_abs_improve", 0.01)
            ),
            "early_stop_min_lpsr_progress": float(
                getattr(args, "fixed_glass_feedback_refine_early_stop_min_lpsr_progress", 0.35)
            ),
            "early_stop_tail_epochs": int(
                getattr(args, "fixed_glass_feedback_refine_early_stop_tail_epochs", 0)
            ),
            "lpsr_horizon_epochs": int(
                getattr(args, "fixed_glass_feedback_refine_lpsr_horizon_epochs", 24)
            ),
        }
    write_json(feedback_dir / "fixed_glass_feedback_config.json", config)
    return (inherited_min_edge, feedback_dir, repair_mode, config)


def fixed_glass_feedback_namespace(
    args, source_dirs, feedback_dir, inherited_min_edge, *, _version: MaterialPolicy
):
    """Translate the repair contract into the existing cycle/refine-like driver arguments."""
    namespace = argparse.Namespace(
        exe=args.exe.resolve()
        if getattr(args, "exe", None)
        else _version.cpp_dir / "build" / "lensde_cpu.exe",
        run_dir=str(args.out_dir),
        source_dirs=";".join((str(path) for path in source_dirs)),
        out_dir=str(feedback_dir),
        cycles=int(getattr(args, "fixed_glass_feedback_cycles", 2)),
        initial_source_limit=int(getattr(args, "fixed_glass_feedback_source_count", 2)),
        default_continuous_sources=0,
        default_final_sources=0,
        elites_per_cycle=int(getattr(args, "fixed_glass_feedback_elites_per_cycle", 2)),
        algorithms=str(getattr(args, "fixed_glass_feedback_algorithms", "adaptive_jade")),
        material_modes=str(getattr(args, "fixed_glass_feedback_material_modes", "continuous")),
        population=int(getattr(args, "fixed_glass_feedback_population", 240)),
        trace_rays=max(1, int(getattr(args, "fixed_glass_feedback_trace_rays", 3))),
        trace_arms=max(1, int(getattr(args, "fixed_glass_feedback_trace_arms", 6))),
        trace_distribution=str(
            getattr(args, "fixed_glass_feedback_trace_distribution", "gaussian_ring_arm")
        ),
        field_hy_fractions=str(getattr(args, "fixed_glass_feedback_field_hy_fractions", "") or ""),
        population_scale=str(getattr(args, "fixed_glass_feedback_population_scale", "medium")),
        generations=int(getattr(args, "fixed_glass_feedback_generations", 420)),
        allow_short_generations=False,
        outer=1,
        refine_outer=int(getattr(args, "fixed_glass_feedback_refine_outer", 80)),
        refine_de_gen=int(getattr(args, "fixed_glass_feedback_refine_de_gen", 400)),
        refine_early_stop=bool(getattr(args, "fixed_glass_feedback_refine_early_stop", True)),
        refine_early_stop_min_epochs=int(
            getattr(args, "fixed_glass_feedback_refine_early_stop_min_epochs", 4)
        ),
        refine_early_stop_window=int(
            getattr(args, "fixed_glass_feedback_refine_early_stop_window", 3)
        ),
        refine_early_stop_min_rel_improve=float(
            getattr(args, "fixed_glass_feedback_refine_early_stop_min_rel_improve", 0.01)
        ),
        refine_early_stop_min_abs_improve=float(
            getattr(args, "fixed_glass_feedback_refine_early_stop_min_abs_improve", 0.01)
        ),
        refine_early_stop_min_lpsr_progress=float(
            getattr(args, "fixed_glass_feedback_refine_early_stop_min_lpsr_progress", 0.35)
        ),
        refine_early_stop_tail_epochs=int(
            getattr(args, "fixed_glass_feedback_refine_early_stop_tail_epochs", 0)
        ),
        refine_lpsr_horizon_epochs=int(
            getattr(args, "fixed_glass_feedback_refine_lpsr_horizon_epochs", 24)
        ),
        hybrid_parents=int(getattr(args, "fixed_glass_feedback_hybrid_parents", 10)),
        polish_candidates=0,
        polish_refill_max_extra=0,
        local_iter=0,
        local_lambda=str(getattr(args, "fixed_glass_feedback_local_lambda", "0.03")),
        local_fd=str(getattr(args, "fixed_glass_feedback_local_fd", "0.0001")),
        local_tol=str(getattr(args, "fixed_glass_feedback_local_tol", "0.00008")),
        local_max_reject=int(getattr(args, "fixed_glass_feedback_local_max_reject", 100)),
        method=str(getattr(args, "fixed_glass_feedback_method", "trf")),
        local_bounds_mode=str(getattr(args, "fixed_glass_feedback_local_bounds_mode", "safety")),
        local_residual_mode=str(
            getattr(args, "fixed_glass_feedback_local_residual_mode", "expanded")
        ),
        refresh=str(getattr(args, "fixed_glass_feedback_refresh", "0.25")),
        de_variant=str(
            getattr(args, "fixed_glass_feedback_de_variant", "jade_pbest_archive_lpsr_feas")
        ),
        de_strategy=str(getattr(args, "fixed_glass_feedback_de_strategy", "rand1bin")),
        seed=int(getattr(args, "fixed_glass_feedback_seed", 20260704)),
        rng_seed=int(getattr(args, "fixed_glass_feedback_rng_seed", 104729)),
        seed_policy=str(
            getattr(args, "fixed_glass_feedback_seed_policy", FIXED_GLASS_SEED_POLICY_STABLE)
        ),
        seed_profile=str(getattr(args, "seed_profile", server.seed_protocol.GENERIC_PROFILE)),
        master_seed=getattr(args, "master_seed", None),
        de_use_cuda=bool(getattr(args, "fixed_glass_feedback_de_use_cuda", False)),
        multi_island_enabled=bool(getattr(args, "fixed_glass_feedback_multi_island", True)),
        multi_island_coalesce_us=max(
            0, int(getattr(args, "fixed_glass_feedback_multi_island_coalesce_us", 1000))
        ),
        multi_island_exe=str(getattr(args, "fixed_glass_feedback_multi_island_exe", "") or ""),
        de_workers=max(1, int(getattr(args, "fixed_glass_feedback_de_workers", 1))),
        de_child_cpu_workers=max(
            0, int(getattr(args, "fixed_glass_feedback_de_child_cpu_workers", 0))
        ),
        de_cuda_min_batch=max(1, int(getattr(args, "fixed_glass_feedback_de_cuda_min_batch", 1))),
        de_cuda_exe=str(getattr(args, "fixed_glass_feedback_de_cuda_exe", "") or ""),
        constraint_handling=str(
            getattr(args, "fixed_glass_feedback_constraint_handling", "feasibility_rules")
        ),
        timeout=int(
            finite_float(getattr(args, "fixed_glass_feedback_timeout", 0), 0.0)
            or finite_float(args.timeout, 3600.0)
            or 3600
        ),
        glass_database=str(
            getattr(args, "fixed_glass_feedback_glass_database", feedback_cycles.DEFAULT_DATABASE)
        ),
        glass_candidates=int(getattr(args, "fixed_glass_feedback_glass_candidates", 14)),
        glass_mutation_rate=float(getattr(args, "fixed_glass_feedback_glass_mutation_rate", 0.3)),
        glass_random_rate=float(getattr(args, "fixed_glass_feedback_glass_random_rate", 0.06)),
        glass_pbest_inherit_rate=float(
            getattr(args, "fixed_glass_feedback_glass_pbest_inherit_rate", 0.3)
        ),
        glass_neighbor_count=int(getattr(args, "fixed_glass_feedback_glass_neighbor_count", 5)),
        glass_neighbor_nd_scale=float(
            getattr(args, "fixed_glass_feedback_glass_neighbor_nd_scale", 0.012)
        ),
        glass_neighbor_vd_scale=float(
            getattr(args, "fixed_glass_feedback_glass_neighbor_vd_scale", 4.0)
        ),
        seed_glass_keep_rate=float(
            getattr(args, "fixed_glass_feedback_seed_glass_keep_rate", 0.55)
        ),
        seed_glass_neighbor_rate=float(
            getattr(args, "fixed_glass_feedback_seed_glass_neighbor_rate", 0.35)
        ),
        continuous_glass_neighbor_rate=float(
            getattr(args, "fixed_glass_feedback_continuous_glass_neighbor_rate", 0.45)
        ),
        continuous_glass_neighbor_topk=int(
            getattr(args, "fixed_glass_feedback_continuous_glass_neighbor_topk", 8)
        ),
        continuous_glass_neighbor_pick=int(
            getattr(args, "fixed_glass_feedback_continuous_glass_neighbor_pick", 5)
        ),
        continuous_glass_nd_noise=float(
            getattr(args, "fixed_glass_feedback_continuous_glass_nd_noise", 0.0008)
        ),
        continuous_glass_vd_noise=float(
            getattr(args, "fixed_glass_feedback_continuous_glass_vd_noise", 0.18)
        ),
        inherit_source_merit=True,
        drop_glass_proximity_for_true_glass=True,
        edge_weight_override=-1.0,
        min_edge_thickness_override=-1.0,
        spot_weight_override=-1.0,
        quality_spot_scale_override=0.0,
        rms_mode_override="inherit",
        quality_wavefront_scale_override=0.0,
        wavefront_reference_override="",
        distortion_upper_override=-1.0,
        sa_step_start=str(getattr(args, "fixed_glass_feedback_sa_step_start", "0.08")),
        sa_step_end=str(getattr(args, "fixed_glass_feedback_sa_step_end", "0.01")),
        sa_temp_start=str(getattr(args, "fixed_glass_feedback_sa_temp_start", "0.08")),
        sa_temp_end=str(getattr(args, "fixed_glass_feedback_sa_temp_end", "0.002")),
        sa_temp_power=str(getattr(args, "fixed_glass_feedback_sa_temp_power", "1.6")),
        target_mean_spot=math.inf,
        max_distortion=math.inf,
        max_constraint_error=math.inf,
        min_edge=inherited_min_edge,
        min_trace_coverage=float(
            getattr(args, "fixed_glass_feedback_min_trace_coverage", DE_DLS_TRACE_COVERAGE_BASELINE)
        ),
        min_field_coverage=float(
            getattr(args, "fixed_glass_feedback_min_field_coverage", DE_DLS_TRACE_COVERAGE_BASELINE)
        ),
    )
    return (namespace,)
