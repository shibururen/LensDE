"""Order the first layer, material terminal, locked DLS and authoritative final publication."""
from __future__ import annotations
from .contracts import FunnelStages
from .contracts import MaterialPolicy
from pathlib import Path
from .common import align_prescout_material_sampling
import argparse
from lensde.workflow import run_checkpoint_de400_layer as checkpoint_de400
from lensde.workflow import run_basin_dls_potential_sweep as dls_sweep
from .common import read_csv
from lensde.engine import core as server
from typing import Any
from .settings import DE_DLS_TRACE_COVERAGE_BASELINE
from .common import _row_truthy
from .common import fixed_glass_feedback_trace_sampling
from .common import record_candidate_shortfall
from .glass_scout import scout_to_locked_boundary_minima
from .calibration_policy import stage_boundary_vignetting_recalibration_enabled
from .calibration_policy import terminal_zemax_vignetting_finalization_enabled
from .settings import TERMINAL_ZEMAX_VIGNETTING_CONTRACT
from lensde.helpers import run_explore_dls_feedback_cycles as feedback_cycles
from .common import finite_float
from .common import max_local_iter_per_restart
from .common import write_json
from .common import read_json


def run_once(args: argparse.Namespace, *, _version: MaterialPolicy, _stages: FunnelStages) -> Path:
    args.basin_run_dir = args.basin_run_dir.resolve()
    args.out_dir = args.out_dir.resolve()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    prescout_sampling = align_prescout_material_sampling(args)
    (
        reuse_material_funnel,
        checkpoint_outer_root,
        checkpoint_reuse_run_manifest,
        reuse_restart_ranked,
        latest_rows,
        selected,
        reference_run,
        sweep_dir,
        material_dir,
        material_entry_rows,
    ) = run_first_layer_and_material(args, _version=_version, _stages=_stages)
    (
        terminal_mode,
        glass_scout_dir,
        fixed_glass_source_rows,
        fixed_glass_feedback_dir,
    ) = run_terminal_glass_stage(
        args,
        material_entry_rows,
        material_dir,
        prescout_sampling,
        _version=_version,
        _stages=_stages,
    )
    (final_locked_glass_dls_dir,) = run_terminal_locked_dls(
        terminal_mode,
        fixed_glass_feedback_dir,
        args,
        fixed_glass_source_rows,
        _version=_version,
        _stages=_stages,
    )
    (
        layer2_trace_sampling,
        terminal_zemax_vignetting_dir,
        terminal_zemax_vignetting_rows,
    ) = finalize_terminal_vignetting(
        args,
        final_locked_glass_dls_dir,
        fixed_glass_feedback_dir,
        material_dir,
        _version=_version,
        _stages=_stages,
    )
    publish_funnel_contract(
        args,
        reference_run,
        sweep_dir,
        checkpoint_outer_root,
        checkpoint_reuse_run_manifest,
        reuse_restart_ranked,
        reuse_material_funnel,
        selected,
        latest_rows,
        material_dir,
        terminal_mode,
        glass_scout_dir,
        fixed_glass_feedback_dir,
        final_locked_glass_dls_dir,
        terminal_zemax_vignetting_dir,
        terminal_zemax_vignetting_rows,
        prescout_sampling,
        layer2_trace_sampling,
        _version=_version,
    )
    print(args.out_dir)
    return args.out_dir


def run_first_layer_and_material(args, *, _version: MaterialPolicy, _stages: FunnelStages):
    """Resolve mutually exclusive reuse, checkpoint and restart routes before material entry."""
    reuse_material_funnel = (
        args.reuse_material_funnel.resolve()
        if getattr(args, "reuse_material_funnel", None)
        else None
    )
    checkpoint_outer_root = (
        args.checkpoint_outer_root.resolve()
        if getattr(args, "checkpoint_outer_root", None)
        else None
    )
    checkpoint_reuse_run_manifest = (
        args.checkpoint_reuse_run_manifest.resolve()
        if getattr(args, "checkpoint_reuse_run_manifest", None)
        else None
    )
    reuse_restart_ranked = (
        args.reuse_restart_ranked.resolve() if getattr(args, "reuse_restart_ranked", None) else None
    )
    # The reuse and new-material branches initialize their own handoff values;
    # neither value is read before the selected route has produced it.
    if reuse_material_funnel is not None:
        ranked_path = reuse_material_funnel / "funnel_final_ranked.csv"
        if not ranked_path.is_file():
            raise FileNotFoundError(f"reused material funnel ranking does not exist: {ranked_path}")
        latest_ranked_path = (
            server.native_io_paths.named_child(args.out_dir, "layer1_checkpoint_de400")
            / "checkpoint_de_latest_ranked.csv"
        )
        latest_rows = read_csv(latest_ranked_path) if latest_ranked_path.is_file() else []
        selected = latest_rows
        reference_run = None
        sweep_dir = None
        material_dir = reuse_material_funnel
        material_entry_path = (
            reuse_material_funnel.parent
            / "layer2_material_entry_selection"
            / "material_entry_candidates.csv"
        )
        material_entry_rows = (
            read_csv(material_entry_path)
            if material_entry_path.is_file()
            else read_csv(ranked_path)
        )
        if not material_entry_rows:
            raise RuntimeError(
                f"reused material funnel has no material-entry rows: {(material_entry_path if material_entry_path.is_file() else ranked_path)}"
            )
        print(
            f"=== layer 1/material: reuse completed material funnel ({ranked_path}) ===", flush=True
        )
    elif checkpoint_outer_root is not None or checkpoint_reuse_run_manifest is not None:
        print(
            "=== layer 1: Outer checkpoint selection + basin-free DE continuation ===", flush=True
        )
        latest_rows = checkpoint_de400.run_once(args)
        selected = latest_rows
        reference_run = None
        sweep_dir = None
    elif reuse_restart_ranked is not None:
        if not reuse_restart_ranked.exists():
            raise FileNotFoundError(
                f"reused restart ranking does not exist: {reuse_restart_ranked}"
            )
        latest_rows = read_csv(reuse_restart_ranked)
        if not latest_rows:
            raise ValueError(f"reused restart ranking has no candidates: {reuse_restart_ranked}")
        missing: list[str] = []
        for row in latest_rows:
            candidate_dir = Path(str(row.get("out_dir") or "")).resolve()
            if not candidate_dir.is_dir():
                missing.append(str(candidate_dir))
                continue
            for name in ("system.json", "score.json"):
                if not (candidate_dir / name).is_file():
                    missing.append(str(candidate_dir / name))
        if missing:
            preview = "\n".join(missing[:10])
            raise FileNotFoundError(
                f"reused restart ranking has {len(missing)} missing candidate artifacts:\n{preview}"
            )
        reference_run = args.reference_dls_run.resolve() if args.reference_dls_run else None
        sweep_dir = None
        selected = latest_rows
        print(
            f"=== layer 1: reuse completed sparse DLS restart candidates ({len(latest_rows)} rows from {reuse_restart_ranked}) ===",
            flush=True,
        )
    else:
        reference_run = (
            args.reference_dls_run.resolve()
            if args.reference_dls_run
            else _stages.make_three_field_geometry_reference(args)
        )
        sweep_dir = args.out_dir / "layer1_dls_potential_sweep"
        sweep_args = argparse.Namespace(
            basin_run_dir=args.basin_run_dir,
            reference_dls_run=reference_run,
            out_dir=sweep_dir,
            max_basins=args.max_basins,
            timeout=args.timeout,
            force_rerun=args.force_rerun,
            exe=args.exe.resolve() if args.exe else None,
            workers=getattr(args, "workers", 1),
            child_cpu_workers=getattr(args, "child_cpu_workers", 0),
        )
        print(
            f"=== layer 1A: 3-field DLS potential sweep ({args.max_basins or 'all'} basins) ===",
            flush=True,
        )
        dls_sweep.run_once(sweep_args)
        selected = _stages.select_candidates(args, sweep_dir)
        latest_rows = _stages.run_restart_layer(args, reference_run, selected, sweep_dir)
    if reuse_material_funnel is None:
        material_dir, material_entry_rows = _stages.run_material_layer(args, latest_rows)
    return (
        reuse_material_funnel,
        checkpoint_outer_root,
        checkpoint_reuse_run_manifest,
        reuse_restart_ranked,
        latest_rows,
        selected,
        reference_run,
        sweep_dir,
        material_dir,
        material_entry_rows,
    )


def run_terminal_glass_stage(
    args,
    material_entry_rows,
    material_dir,
    prescout_sampling,
    *,
    _version: MaterialPolicy,
    _stages: FunnelStages,
):
    """Choose continuous, direct-glass or Scout handoffs before locked-glass repair."""
    terminal_mode = (
        str(getattr(args, "material_terminal_mode", "funnel") or "funnel").strip().lower()
    )
    glass_scout_dir: Path | None = None
    fixed_glass_source_rows: list[dict[str, Any]] | None = None
    if terminal_mode == "direct_glass_de":
        args.fixed_glass_feedback_enabled = True
        args.fixed_glass_feedback_material_modes = "true_glass"
        args.fixed_glass_feedback_source_count = max(1, len(material_entry_rows))
        fixed_glass_source_rows = material_entry_rows
    elif terminal_mode == "scout_lock_fixed_de":
        args.fixed_glass_feedback_enabled = True
        args.fixed_glass_feedback_material_modes = "locked_glass"
        scout_source_rows = _stages.continuous_material_scout_sources(
            args, material_dir, material_entry_rows
        )
        if stage_boundary_vignetting_recalibration_enabled(args):
            scout_source_rows = _stages.recalibrate_vignetting_source_rows(
                args,
                scout_source_rows,
                stage_name="layer2_stage_boundary_material_to_scout_vignetting",
                sampling=prescout_sampling,
                min_trace=float(
                    getattr(args, "glass_scout_min_trace_coverage", DE_DLS_TRACE_COVERAGE_BASELINE)
                ),
                min_field=float(
                    getattr(args, "glass_scout_min_field_coverage", DE_DLS_TRACE_COVERAGE_BASELINE)
                ),
            )
        scout_rows, glass_scout_dir = _stages.run_glass_choice_scout(args, scout_source_rows)
        record_candidate_shortfall(args, "glass_choice_scout", len(scout_source_rows), scout_rows)
        fixed_glass_source_rows = [
            row for row in scout_rows if _row_truthy(row.get("admission_ok"))
        ]
        record_candidate_shortfall(
            args, "glass_choice_scout_admission", len(scout_source_rows), fixed_glass_source_rows
        )
        if stage_boundary_vignetting_recalibration_enabled(args):
            fixed_sampling = fixed_glass_feedback_trace_sampling(args)
            scout_handoff_min_trace, scout_handoff_min_field = scout_to_locked_boundary_minima(args)
            fixed_glass_source_rows = _stages.recalibrate_vignetting_source_rows(
                args,
                fixed_glass_source_rows,
                stage_name="layer2_stage_boundary_scout_to_locked_de_vignetting",
                sampling=fixed_sampling,
                min_trace=scout_handoff_min_trace,
                min_field=scout_handoff_min_field,
            )
        args.fixed_glass_feedback_source_count = len(fixed_glass_source_rows)
        if getattr(args, "fixed_glass_feedback_out_dir", None) is None:
            args.fixed_glass_feedback_out_dir = server.native_io_paths.named_child(
                args.out_dir.resolve(), "layer2_locked_glass_de"
            )
    fixed_glass_feedback_dir = _stages.run_fixed_glass_feedback_repair(
        args, material_dir, fixed_glass_source_rows
    )
    return (terminal_mode, glass_scout_dir, fixed_glass_source_rows, fixed_glass_feedback_dir)


def run_terminal_locked_dls(
    terminal_mode,
    fixed_glass_feedback_dir,
    args,
    fixed_glass_source_rows,
    *,
    _version: MaterialPolicy,
    _stages: FunnelStages,
):
    """Admit final DLS sources and retain terminal-specific seed and sampling policy."""
    final_locked_glass_dls_dir: Path | None = None
    if (
        terminal_mode == "scout_lock_fixed_de"
        and fixed_glass_feedback_dir is not None
        and bool(getattr(args, "final_locked_glass_dls_enabled", True))
    ):
        final_dls_source_rows = _stages.locked_glass_final_dls_source_rows(
            args, fixed_glass_feedback_dir
        )
        expected_final_dls_sources = min(
            max(1, int(getattr(args, "final_locked_glass_dls_source_count", 4))),
            len(fixed_glass_source_rows or []),
        )
        record_candidate_shortfall(
            args, "locked_glass_final_dls", expected_final_dls_sources, final_dls_source_rows
        )
        if stage_boundary_vignetting_recalibration_enabled(args):
            layer2_sampling = fixed_glass_feedback_trace_sampling(args)
            final_dls_source_rows = _stages.recalibrate_vignetting_source_rows(
                args,
                final_dls_source_rows,
                stage_name="layer2_stage_boundary_locked_de_to_final_dls_vignetting",
                sampling=layer2_sampling,
                min_trace=float(
                    getattr(
                        args,
                        "final_locked_glass_dls_min_trace_coverage",
                        DE_DLS_TRACE_COVERAGE_BASELINE,
                    )
                ),
                min_field=float(
                    getattr(
                        args,
                        "final_locked_glass_dls_min_field_coverage",
                        DE_DLS_TRACE_COVERAGE_BASELINE,
                    )
                ),
            )
        final_locked_glass_dls_dir = _stages.run_final_locked_glass_dls_tail(
            args, fixed_glass_feedback_dir, final_dls_source_rows
        )
    return (final_locked_glass_dls_dir,)


def finalize_terminal_vignetting(
    args,
    final_locked_glass_dls_dir,
    fixed_glass_feedback_dir,
    material_dir,
    *,
    _version: MaterialPolicy,
    _stages: FunnelStages,
):
    """Choose the terminal ranking and apply the authoritative final pupil measurement."""
    layer2_trace_sampling = fixed_glass_feedback_trace_sampling(args)
    terminal_zemax_vignetting_dir: Path | None = None
    terminal_zemax_vignetting_rows: list[dict[str, Any]] = []
    if terminal_zemax_vignetting_finalization_enabled(args):
        terminal_source_rows: list[dict[str, Any]] = []
        terminal_source_path: Path | None = None
        if final_locked_glass_dls_dir is not None:
            terminal_source_path = final_locked_glass_dls_dir / "final_locked_glass_dls_ranked.csv"
        elif fixed_glass_feedback_dir is not None:
            terminal_source_path = fixed_glass_feedback_dir / "fixed_glass_feedback_ranked.csv"
        elif material_dir is not None:
            terminal_source_path = material_dir / "funnel_final_ranked.csv"
        if terminal_source_path is not None and terminal_source_path.is_file():
            terminal_source_rows = read_csv(terminal_source_path)
        if terminal_source_rows:
            terminal_zemax_vignetting_rows = _stages.finalize_terminal_zemax_vignetting_rows(
                args,
                terminal_source_rows,
                stage_name="layer2_terminal_zemax_vignetting",
                sampling=layer2_trace_sampling,
                min_trace=float(
                    getattr(
                        args,
                        "final_locked_glass_dls_min_trace_coverage",
                        DE_DLS_TRACE_COVERAGE_BASELINE,
                    )
                ),
                min_field=float(
                    getattr(
                        args,
                        "final_locked_glass_dls_min_field_coverage",
                        DE_DLS_TRACE_COVERAGE_BASELINE,
                    )
                ),
            )
            terminal_zemax_vignetting_dir = server.native_io_paths.named_child(
                args.out_dir.resolve(), "layer2_terminal_zemax_vignetting"
            )
        elif terminal_source_path is not None:
            raise RuntimeError(
                f"terminal Zemax vignetting finalization found no source rows: {terminal_source_path}"
            )
    return (layer2_trace_sampling, terminal_zemax_vignetting_dir, terminal_zemax_vignetting_rows)


def publish_funnel_contract(
    args,
    reference_run,
    sweep_dir,
    checkpoint_outer_root,
    checkpoint_reuse_run_manifest,
    reuse_restart_ranked,
    reuse_material_funnel,
    selected,
    latest_rows,
    material_dir,
    terminal_mode,
    glass_scout_dir,
    fixed_glass_feedback_dir,
    final_locked_glass_dls_dir,
    terminal_zemax_vignetting_dir,
    terminal_zemax_vignetting_rows,
    prescout_sampling,
    layer2_trace_sampling,
    *,
    _version: MaterialPolicy,
):
    """Write the ordered final configuration and choose the authoritative result ranking."""
    write_json(
        args.out_dir / "two_layer_funnel_config.json",
        {
            "basin_run_dir": str(args.basin_run_dir),
            "out_dir": str(args.out_dir),
            "reference_dls_run": str(reference_run) if reference_run else "",
            "dls_potential_sweep": str(sweep_dir) if sweep_dir else "",
            "checkpoint_outer_root": str(checkpoint_outer_root) if checkpoint_outer_root else "",
            "checkpoint_reuse_run_manifest": str(checkpoint_reuse_run_manifest)
            if checkpoint_reuse_run_manifest
            else "",
            "layer1_route": "checkpoint_de"
            if checkpoint_outer_root or checkpoint_reuse_run_manifest
            else "legacy_dls_restart",
            "checkpoint_de": {
                "total_count": int(getattr(args, "checkpoint_total_count", 20)),
                "nonrank_count": int(getattr(args, "checkpoint_nonrank_count", 0)),
                "merit_relaxation_enabled": bool(
                    getattr(args, "checkpoint_merit_relaxation", True)
                ),
                "basin_fill_enabled": bool(getattr(args, "checkpoint_basin_fill", True)),
                "population": int(getattr(args, "checkpoint_de_population", 0)),
                "generations": int(getattr(args, "checkpoint_de_generations", 0)),
                "selected_count": int(getattr(args, "checkpoint_selected_count", 0)),
                "handoff_count": int(getattr(args, "checkpoint_handoff_count", 0)),
                "handoff_priority_quotas": {
                    "merit": int(getattr(args, "checkpoint_handoff_merit_count", 7)),
                    "potential": int(getattr(args, "checkpoint_handoff_potential_count", 0)),
                    "diversity": int(getattr(args, "checkpoint_handoff_diversity_count", 0)),
                },
                "strict_structure_selection_policy": str(
                    getattr(
                        args,
                        "strict_structure_selection_policy",
                        checkpoint_de400.strict_topology.LEGACY_DISTANCE_POLICY,
                    )
                ),
                "population_scale": str(getattr(args, "checkpoint_population_scale", "")),
                "use_cuda": bool(getattr(args, "checkpoint_de_use_cuda", False)),
                "multi_island_experimental": bool(
                    getattr(args, "checkpoint_multi_island_experimental", True)
                ),
                "multi_island_exe": str(
                    Path(
                        getattr(
                            args,
                            "checkpoint_multi_island_exe",
                            _version.cpp_dir / "build" / "lensde_multi_island.exe",
                        )
                    ).resolve()
                ),
                "multi_island_coalesce_us": int(
                    getattr(args, "checkpoint_multi_island_coalesce_us", 1000)
                ),
                "multi_island_default_enabled": True,
                "production_default_changed": True,
            },
            "reuse_restart_ranked": str(reuse_restart_ranked) if reuse_restart_ranked else "",
            "reuse_material_funnel": str(reuse_material_funnel) if reuse_material_funnel else "",
            "reuse_glass_choice_scout": bool(getattr(args, "reuse_glass_choice_scout", False)),
            "selected_ranks": [int(finite_float(row.get("basin_rank"), -1)) for row in selected],
            "latest_geometry_candidates": latest_rows,
            "material_funnel": str(material_dir) if material_dir else "",
            "material_terminal_mode": terminal_mode,
            "optimizer_contract": "continuous_material_dls_then_pure_de_then_final_dls"
            if terminal_mode == "scout_lock_fixed_de"
            else "pure_de_no_dls"
            if terminal_mode == "direct_glass_de"
            else "legacy_material_funnel",
            "glass_choice_scout_dir": str(glass_scout_dir) if glass_scout_dir else "",
            "max_basins": args.max_basins,
            "selection_count": args.selection_count,
            "restart_max_cycles": args.restart_max_cycles,
            "restart_local_iter_schedule": args.restart_local_iter_schedule,
            "max_local_iter_per_restart": max_local_iter_per_restart(args),
            "restart_min_delta": args.restart_min_delta,
            "restart_min_rel_delta": args.restart_min_rel_delta,
            "restart_patience": args.restart_patience,
            "workers": getattr(args, "workers", 1),
            "child_cpu_workers": getattr(args, "child_cpu_workers", 0),
            "material_entry_count": args.material_entry_count,
            "candidate_shortfalls": getattr(args, "candidate_shortfalls", []),
            "material_entry_ranks": args.material_entry_ranks,
            "material_entry_merit_count": args.material_entry_merit_count,
            "material_entry_potential_count": args.material_entry_potential_count,
            "material_entry_diversity_count": int(
                getattr(args, "material_entry_diversity_count", 0)
            ),
            "material_entry_quality_guard_ratio": float(
                getattr(args, "material_entry_quality_guard_ratio", 1.1)
            ),
            "material_entry_selection_mode": args.material_entry_selection_mode,
            "material_entry_max_final_merit": args.material_entry_max_final_merit,
            "material_bridge": {
                "enabled": bool(getattr(args, "material_bridge_enabled", True)),
                "dir": str(
                    server.native_io_paths.named_child(
                        args.out_dir.resolve(), "layer2_material5_bridge"
                    )
                )
                if bool(getattr(args, "material_bridge_enabled", True))
                else "",
                "field_hy_fractions": str(getattr(args, "material_bridge_field_hy_fractions", "")),
                "trace_rays": int(getattr(args, "material_bridge_trace_rays", 3)),
                "trace_arms": int(getattr(args, "material_bridge_trace_arms", 6)),
                "stable_count": int(getattr(args, "material_bridge_stable_count", -1)),
                "repair_count": int(getattr(args, "material_bridge_repair_count", -1)),
                "diversity_count": int(getattr(args, "material_bridge_diversity_count", -1)),
                "constraint_threshold": float(
                    getattr(args, "material_bridge_constraint_threshold", 2.5)
                ),
                "max_image_growth": float(getattr(args, "material_bridge_max_image_growth", 0.15)),
                "repair_probe_count": int(getattr(args, "material_bridge_repair_probe_count", 4)),
                "repair_population": int(getattr(args, "material_bridge_repair_population", 240)),
                "repair_generations": int(getattr(args, "material_bridge_repair_generations", 150)),
                "repair_population_scale": str(
                    getattr(args, "material_bridge_repair_population_scale", "tight")
                ),
                "minimum_trace_coverage": float(
                    getattr(
                        args,
                        "material_bridge_repair_min_trace_coverage",
                        DE_DLS_TRACE_COVERAGE_BASELINE,
                    )
                ),
                "deepen_enabled": bool(getattr(args, "material_bridge_deepen_enabled", True)),
                "deepen_population": int(getattr(args, "material_bridge_deepen_population", 400)),
                "deepen_generations": int(getattr(args, "material_bridge_deepen_generations", 250)),
                "deepen_population_scale": str(
                    getattr(args, "material_bridge_deepen_population_scale", "medium")
                ),
                "extension_count": int(getattr(args, "material_bridge_extension_count", 2)),
                "extension_target_generations": int(
                    getattr(args, "material_bridge_extension_target_generations", 400)
                ),
                "de_use_cuda": bool(getattr(args, "material_bridge_de_use_cuda", True)),
                "multi_island_enabled": bool(
                    getattr(args, "material_bridge_multi_island_enabled", True)
                ),
                "multi_island_exe": str(
                    Path(
                        getattr(
                            args,
                            "material_bridge_multi_island_exe",
                            _version.cpp_dir / "build" / "lensde_multi_island.exe",
                        )
                    ).resolve()
                ),
                "multi_island_coalesce_us": int(
                    getattr(args, "material_bridge_multi_island_coalesce_us", 1000)
                ),
            },
            "material_chromatic_repair": {
                "enabled": bool(getattr(args, "material_chromatic_repair_enabled", False)),
                "dir": str(
                    server.native_io_paths.named_child(
                        args.out_dir.resolve(), "layer2_material_chromatic_repair"
                    )
                ),
                "seed_mode": "single_shared_seed",
                "seed": int(getattr(args, "material_chromatic_repair_seed", 20260804)),
                "population": int(getattr(args, "material_chromatic_repair_population", 320)),
                "generations": int(getattr(args, "material_chromatic_repair_generations", 1200)),
                "population_scale": str(
                    getattr(args, "material_chromatic_repair_population_scale", "wide")
                ),
                "acceptance_rule": "common_merit_strict_improvement",
                "constraint_group_error_role": "audit_only",
            },
            "fixed_glass_feedback_repair": str(fixed_glass_feedback_dir)
            if fixed_glass_feedback_dir
            else "",
            "fixed_glass_feedback_enabled": bool(
                getattr(args, "fixed_glass_feedback_enabled", False)
            ),
            "final_locked_glass_dls": str(final_locked_glass_dls_dir)
            if final_locked_glass_dls_dir
            else "",
            "final_locked_glass_dls_enabled": bool(
                getattr(args, "final_locked_glass_dls_enabled", True)
            ),
            "terminal_zemax_vignetting": {
                "enabled": terminal_zemax_vignetting_finalization_enabled(args),
                "dir": str(terminal_zemax_vignetting_dir)
                if terminal_zemax_vignetting_dir is not None
                else "",
                "accepted_count": len(terminal_zemax_vignetting_rows),
                "contract": TERMINAL_ZEMAX_VIGNETTING_CONTRACT,
                "authoritative_final_system": bool(terminal_zemax_vignetting_rows),
                "selection_basis": "pre_vignetting_calibration_order",
            },
            "stage_boundary_vignetting_recalibration": {
                "enabled": stage_boundary_vignetting_recalibration_enabled(args),
                "contract": "stage_boundary_vignetting_recalibration_v1",
                "minimum_trace_coverage": DE_DLS_TRACE_COVERAGE_BASELINE,
                "minimum_field_coverage": DE_DLS_TRACE_COVERAGE_BASELINE,
                "optimizer_hot_path_unchanged": True,
                "resume_policy": "reuse_material_funnel_from_earliest_affected_endpoint",
                "material_to_scout_dir": str(
                    server.native_io_paths.named_child(
                        args.out_dir.resolve(), "layer2_stage_boundary_material_to_scout_vignetting"
                    )
                ),
                "scout_to_locked_de_dir": str(
                    server.native_io_paths.named_child(
                        args.out_dir.resolve(),
                        "layer2_stage_boundary_scout_to_locked_de_vignetting",
                    )
                ),
                "locked_de_to_final_dls_dir": str(
                    server.native_io_paths.named_child(
                        args.out_dir.resolve(),
                        "layer2_stage_boundary_locked_de_to_final_dls_vignetting",
                    )
                ),
                "final_dls_pass_dir": str(
                    server.native_io_paths.named_child(
                        args.out_dir.resolve(), "layer2_final_locked_glass_dls_vignetting"
                    )
                ),
            },
            "prescout_material_sampling_contract": {
                **prescout_sampling,
                "source": "glass_choice_scout",
                "aligned_stages": [
                    "material_bridge",
                    "continuous_material_funnel_dls",
                    "glass_choice_scout_de",
                ],
            },
            "continuous_material_dls_trace_sampling": {
                **prescout_sampling,
                "source": "glass_choice_scout",
            },
            "final_locked_glass_dls_trace_sampling": {
                **layer2_trace_sampling,
                "source": "fixed_glass_feedback",
            },
            "geometry_field_hy_fractions": feedback_cycles.parse_field_hy_fractions(
                str(getattr(args, "geometry_field_hy_fractions", "") or "")
            ),
            "fixed_glass_feedback_field_hy_fractions": feedback_cycles.parse_field_hy_fractions(
                str(getattr(args, "fixed_glass_feedback_field_hy_fractions", "") or "")
            ),
        },
    )


def resume_final_locked_glass_dls(args, *, _version: MaterialPolicy, _stages: FunnelStages):
    args.out_dir = args.out_dir.resolve()
    feedback_dir = args.resume_final_locked_glass_dls_from.resolve()
    final_dls_source_rows = _stages.locked_glass_final_dls_source_rows(args, feedback_dir)
    if stage_boundary_vignetting_recalibration_enabled(args):
        final_dls_source_rows = _stages.recalibrate_vignetting_source_rows(
            args,
            final_dls_source_rows,
            stage_name="layer2_stage_boundary_locked_de_to_final_dls_vignetting",
            sampling=fixed_glass_feedback_trace_sampling(args),
            min_trace=float(args.final_locked_glass_dls_min_trace_coverage),
            min_field=float(args.final_locked_glass_dls_min_field_coverage),
        )
    final_dls_dir = _stages.run_final_locked_glass_dls_tail(
        args, feedback_dir, final_dls_source_rows
    )
    if final_dls_dir is not None and terminal_zemax_vignetting_finalization_enabled(args):
        ranked_path = final_dls_dir / "final_locked_glass_dls_ranked.csv"
        terminal_rows = read_csv(ranked_path) if ranked_path.is_file() else []
        if not terminal_rows:
            raise RuntimeError(
                f"resumed final locked-glass DLS produced no terminal rows: {ranked_path}"
            )
        terminal_rows = _stages.finalize_terminal_zemax_vignetting_rows(
            args,
            terminal_rows,
            stage_name="layer2_terminal_zemax_vignetting",
            sampling=fixed_glass_feedback_trace_sampling(args),
            min_trace=float(args.final_locked_glass_dls_min_trace_coverage),
            min_field=float(args.final_locked_glass_dls_min_field_coverage),
        )
    else:
        terminal_rows = []
    config_path = args.out_dir / "two_layer_funnel_config.json"
    config = read_json(config_path) if config_path.is_file() else {}
    terminal_mode = (
        str(getattr(args, "material_terminal_mode", "scout_lock_fixed_de") or "scout_lock_fixed_de")
        .strip()
        .lower()
    )
    config.update(
        {
            "basin_run_dir": str(args.basin_run_dir.resolve()),
            "out_dir": str(args.out_dir),
            "material_terminal_mode": terminal_mode,
            "optimizer_contract": "continuous_material_dls_then_pure_de_then_final_dls"
            if terminal_mode == "scout_lock_fixed_de"
            else "pure_de_no_dls"
            if terminal_mode == "direct_glass_de"
            else "legacy_material_funnel",
            "fixed_glass_feedback_repair": str(feedback_dir),
            "fixed_glass_feedback_enabled": True,
            "final_locked_glass_dls": str(final_dls_dir or ""),
            "final_locked_glass_dls_enabled": bool(final_dls_dir),
            "terminal_zemax_vignetting": {
                "enabled": terminal_zemax_vignetting_finalization_enabled(args),
                "dir": str(
                    server.native_io_paths.named_child(
                        args.out_dir.resolve(), "layer2_terminal_zemax_vignetting"
                    )
                ),
                "accepted_count": len(terminal_rows),
                "contract": TERMINAL_ZEMAX_VIGNETTING_CONTRACT,
                "authoritative_final_system": bool(terminal_rows),
                "selection_basis": "pre_vignetting_calibration_order",
            },
        }
    )
    write_json(config_path, config)
    return 0
