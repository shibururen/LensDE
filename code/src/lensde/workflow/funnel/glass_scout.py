"""Prepare discrete glass-choice islands, execute/reuse waves and admit materialized results."""
from __future__ import annotations
import argparse
from .settings import DE_DLS_TRACE_COVERAGE_BASELINE
from typing import Any
from .common import finite_float
from .feedback_contracts import canonical_contract_fingerprint
from .feedback_contracts import ordered_compatibility_cohorts
from .contracts import MaterialPolicy
from pathlib import Path
from lensde.helpers import run_explore_dls_feedback_cycles as feedback_cycles
from .feedback_contracts import glass_scout_source_system
from lensde.engine import core as server
from .common import write_json
from .common import _row_truthy
from .settings import GLASS_SCOUT_CPU_CUDA_MERIT_REL_TOL
from .feedback_contracts import cpu_cuda_materialization_consistency
from .feedback_contracts import fixed_glass_feedback_admission
from .feedback_contracts import materialize_glass_source_paths
import math
from .common import read_json
from .common import write_csv


def resolved_glass_scout_rounds(args: argparse.Namespace, glass_group_count: int) -> int:
    mode = str(getattr(args, "glass_scout_round_mode", "auto") or "auto").strip().lower()
    if mode == "fixed":
        return max(1, int(getattr(args, "glass_scout_fixed_rounds", 3)))
    if glass_group_count <= 3:
        return 3
    if glass_group_count <= 6:
        return 5
    return 6


def glass_scout_namespace(args: argparse.Namespace) -> argparse.Namespace:
    return argparse.Namespace(
        glass_candidates=max(1, int(getattr(args, "glass_scout_candidates", 14))),
        glass_mutation_rate=float(getattr(args, "glass_scout_mutation_rate", 0.3)),
        glass_random_rate=float(getattr(args, "glass_scout_random_rate", 0.06)),
        glass_pbest_inherit_rate=float(getattr(args, "glass_scout_pbest_inherit_rate", 0.3)),
        glass_neighbor_count=max(0, int(getattr(args, "glass_scout_neighbor_count", 5))),
        glass_neighbor_nd_scale=float(getattr(args, "glass_scout_neighbor_nd_scale", 0.012)),
        glass_neighbor_vd_scale=float(getattr(args, "glass_scout_neighbor_vd_scale", 4.0)),
        seed_glass_keep_rate=float(getattr(args, "glass_scout_seed_glass_keep_rate", 0.55)),
        seed_glass_neighbor_rate=float(getattr(args, "glass_scout_seed_glass_neighbor_rate", 0.35)),
        continuous_glass_neighbor_rate=0.0,
        continuous_glass_neighbor_topk=1,
        continuous_glass_neighbor_pick=1,
        continuous_glass_nd_noise=0.0,
        continuous_glass_vd_noise=0.0,
        trace_rays=max(1, int(getattr(args, "glass_scout_trace_rays", 3))),
        trace_arms=max(1, int(getattr(args, "glass_scout_trace_arms", 6))),
        trace_distribution=str(
            getattr(args, "glass_scout_trace_distribution", "gaussian_ring_arm")
        ),
        drop_glass_proximity_for_true_glass=True,
        inherit_source_merit=True,
        min_trace_coverage=float(
            getattr(args, "glass_scout_min_trace_coverage", DE_DLS_TRACE_COVERAGE_BASELINE)
        ),
        min_field_coverage=float(
            getattr(args, "glass_scout_min_field_coverage", DE_DLS_TRACE_COVERAGE_BASELINE)
        ),
    )


def scout_to_locked_boundary_minima(args: argparse.Namespace) -> tuple[float, float]:
    """Return the pre-repair Scout handoff coverage contract.

    This boundary writes strict APG/Zemax-style vignetting coefficients before
    locked-glass DE, but it is not the post-repair 0.97 admission boundary.
    Applying the downstream minimum here deletes a lineage that locked-glass
    DE is specifically intended to repair.
    """
    return (
        float(getattr(args, "glass_scout_min_trace_coverage", DE_DLS_TRACE_COVERAGE_BASELINE)),
        float(getattr(args, "glass_scout_min_field_coverage", DE_DLS_TRACE_COVERAGE_BASELINE)),
    )


def glass_scout_variable_layout_signature(
    variables_doc: dict[str, Any]
) -> tuple[tuple[str, int, int], ...]:
    return tuple(
        (
            (
                str(variable.get("kind") or "").strip().lower(),
                int(finite_float(variable.get("index"), -1)),
                int(finite_float(variable.get("term"), 0)),
            )
            for variable in variables_doc.get("variables", []) or []
        )
    )


def glass_scout_batch_policy(
    requested: bool,
    variables_docs: list[dict[str, Any]],
    systems: list[dict[str, Any]] | None = None,
    merits: list[dict[str, Any]] | None = None,
) -> dict[str, Any]:
    systems = systems or [{} for _ in variables_docs]
    merits = merits or [{} for _ in variables_docs]
    if not len(variables_docs) == len(systems) == len(merits):
        raise ValueError("GlassChoice Scout compatibility inputs must have equal lengths")
    layout_signatures = [
        glass_scout_variable_layout_signature(document) for document in variables_docs
    ]
    keys = [
        (len(system.get("surfaces") or []), layout, canonical_contract_fingerprint(merit))
        for system, layout, merit in zip(systems, layout_signatures, merits, strict=True)
    ]
    cohort_indices = ordered_compatibility_cohorts(keys)
    fallback_reasons: list[str] = []
    if not variables_docs:
        fallback_reasons.append("no GlassChoice Scout islands")
    cohorts: list[dict[str, Any]] = []
    for position, member_indices in enumerate(cohort_indices, start=1):
        reference = member_indices[0]
        used = bool(requested and len(member_indices) >= 2)
        reasons = [] if len(member_indices) >= 2 else ["fewer than two compatible islands"]
        cohorts.append(
            {
                "cohort_id": f"cohort_{position:02d}",
                "member_indices": member_indices,
                "members": [index + 1 for index in member_indices],
                "island_count": len(member_indices),
                "surface_count": len(systems[reference].get("surfaces") or []),
                "variable_count": len(variables_docs[reference].get("variables") or []),
                "layout_fingerprint": canonical_contract_fingerprint(layout_signatures[reference]),
                "merit_fingerprint": canonical_contract_fingerprint(merits[reference]),
                "used": used,
                "execution_mode": "batched_multi_island" if used else "serial",
                "fallback_reasons": reasons,
            }
        )
    used = any((cohort["used"] for cohort in cohorts))
    if requested and variables_docs and (not used):
        fallback_reasons.append("no compatible cohort has at least two islands")
    return {
        "requested": bool(requested),
        "used": used,
        "fully_batched": bool(used and all((cohort["used"] for cohort in cohorts))),
        "unique_variable_layout_count": len(set(layout_signatures)),
        "cohort_count": len(cohorts),
        "batched_island_count": sum(
            (cohort["island_count"] for cohort in cohorts if cohort["used"])
        ),
        "serial_island_count": sum(
            (cohort["island_count"] for cohort in cohorts if not cohort["used"])
        ),
        "cohorts": cohorts,
        "fallback_reasons": fallback_reasons,
    }


def run_glass_choice_scout(
    args: argparse.Namespace, source_rows: list[dict[str, Any]], *, _version: MaterialPolicy
) -> tuple[list[dict[str, Any]], Path]:
    """Run one short, batched GlassChoice wave and materialize its glass choices."""
    scout_dir = server.native_io_paths.named_child(
        args.out_dir.resolve(), "layer2_glass_choice_scout"
    )
    scout_dir.mkdir(parents=True, exist_ok=True)
    source_dirs = [
        Path(str(row.get("out_dir"))).resolve()
        for row in source_rows
        if str(row.get("out_dir") or "").strip()
    ]
    if not source_dirs:
        raise RuntimeError("GlassChoice Scout received no bridge candidates")
    policy_context = Path(str(args.basin_run_dir)).resolve()
    systems = [glass_scout_source_system(source_dir, policy_context) for source_dir in source_dirs]
    scout_libraries = feedback_cycles.glass_libraries_for_systems(systems)
    glasses = feedback_cycles.load_glasses(
        Path(str(getattr(args, "glass_scout_glass_database", feedback_cycles.DEFAULT_DATABASE))),
        scout_libraries,
        feedback_cycles.systems_wavelengths_um(systems),
    )
    scout_args = glass_scout_namespace(args)
    site_sets = [feedback_cycles.glass_choice_sites(system) for system in systems]
    reference_indices = [int(site.front_surface) for site in site_sets[0]]
    for sites in site_sets[1:]:
        if [int(site.front_surface) for site in sites] != reference_indices:
            raise ValueError("GlassChoice Scout islands have incompatible lens-site layouts")
    glass_group_count = len(reference_indices)
    rounds = resolved_glass_scout_rounds(args, glass_group_count)
    population_size = max(20, int(getattr(args, "glass_scout_population", 240)))
    generations = max(1, int(getattr(args, "glass_scout_generations", 200)))
    field_fractions = str(
        getattr(args, "glass_scout_field_hy_fractions", "0,0.25,0.5,0.75,1") or "0,0.25,0.5,0.75,1"
    )
    common_merit = feedback_cycles.adjusted_merit(source_dirs[0], scout_args, "true_glass")
    jobs: list[dict[str, Any]] = []
    contexts: list[dict[str, Any]] = []
    prepare_scout_islands(
        source_dirs,
        source_rows,
        systems,
        glasses,
        scout_args,
        field_fractions,
        scout_dir,
        common_merit,
        population_size,
        args,
        jobs,
        generations,
        rounds,
        contexts,
    )
    (
        columns,
        multi_exe,
        batch_policy,
        batched,
        timeout,
        reuse_completed_wave,
        run_results_by_index,
        cohort_runs,
        commands,
    ) = prepare_scout_wave(
        scout_dir,
        jobs,
        args,
        contexts,
        common_merit,
        glass_group_count,
        rounds,
        generations,
        population_size,
        scout_args,
        _version=_version,
    )
    (wave_result,) = execute_scout_wave(
        reuse_completed_wave,
        contexts,
        batch_policy,
        run_results_by_index,
        scout_dir,
        jobs,
        columns,
        multi_exe,
        args,
        commands,
        timeout,
        cohort_runs,
    )
    (rows,) = materialize_scout_results(
        contexts,
        run_results_by_index,
        wave_result,
        scout_args,
        common_merit,
        rounds,
        generations,
        glass_group_count,
        scout_libraries,
    )
    return publish_scout_results(
        rows,
        scout_args,
        scout_dir,
        args,
        rounds,
        glass_group_count,
        population_size,
        generations,
        batch_policy,
        batched,
        scout_libraries,
        glasses,
        wave_result,
    )


def prepare_scout_islands(
    source_dirs,
    source_rows,
    systems,
    glasses,
    scout_args,
    field_fractions,
    scout_dir,
    common_merit,
    population_size,
    args,
    jobs,
    generations,
    rounds,
    contexts,
):
    """Prepare islands and consume per-source seeds in the original source order."""
    for position, (source_row, source_dir, source_system) in enumerate(
        zip(source_rows, source_dirs, systems, strict=True), start=1
    ):
        basin_rank = int(finite_float(source_row.get("basin_rank"), position))
        problem_system, variables_doc, glass_search = feedback_cycles.true_glass_problem(
            source_dir=source_dir,
            system=source_system,
            glasses=glasses,
            args=scout_args,
            common_groups=None,
        )
        input_catalog_audit = feedback_cycles.audit_true_glass_catalog_contract(
            problem_system, glass_search
        )
        problem_system = feedback_cycles.with_field_hy_fractions(problem_system, field_fractions)
        case_root = server.native_io_paths.named_child(
            scout_dir / "islands", f"cand_{position:02d}_rank_{basin_rank:04d}"
        )
        run_dir = case_root / "run"
        snapshot_dir = case_root / "snapshots"
        system_path = case_root / "system_input.json"
        variables_path = case_root / "variables.json"
        merit_path = case_root / "merit.json"
        population_path = case_root / "seed_population.csv"
        glass_path = case_root / "glass_search_config.json"
        write_json(system_path, problem_system)
        write_json(variables_path, variables_doc)
        write_json(merit_path, common_merit)
        write_json(glass_path, glass_search)
        write_json(case_root / "glass_catalog_contract_input.json", input_catalog_audit)
        population = feedback_cycles.build_population(
            system=problem_system,
            variables_doc=variables_doc,
            glasses=glasses,
            material_mode="true_glass",
            population=population_size,
            seed=int(getattr(args, "glass_scout_rng_seed", 104729)) + position * 1009,
            scale=str(getattr(args, "glass_scout_population_scale", "medium")),
            args=scout_args,
        )
        if len(population) != population_size:
            raise RuntimeError(
                f"GlassChoice Scout island {position} produced {len(population)}/{population_size} population rows"
            )
        feedback_cycles.write_population(population_path, population)
        jobs.append(
            {
                "island_id": position - 1,
                "system": system_path,
                "variables": variables_path,
                "merit": merit_path,
                "population_file": population_path,
                "snapshot_dir": snapshot_dir,
                "output_dir": run_dir,
                "seed": int(getattr(args, "glass_scout_seed", 20260726)) + position * 101,
                "population": population_size,
                "generations": generations,
                "profile": f"glass_scout_{rounds}",
                "internal_scaling": "edge_sag_normalized",
                "glass_search": glass_path,
            }
        )
        contexts.append(
            {
                "position": position,
                "basin_rank": basin_rank,
                "source_row": source_row,
                "source_dir": source_dir,
                "source_system": source_system,
                "problem_system": problem_system,
                "case_root": case_root,
                "run_dir": run_dir,
                "variables_doc": variables_doc,
                "glass_search": glass_search,
                "input_catalog_audit": input_catalog_audit,
            }
        )


def prepare_scout_wave(
    scout_dir,
    jobs,
    args,
    contexts,
    common_merit,
    glass_group_count,
    rounds,
    generations,
    population_size,
    scout_args,
    *,
    _version: MaterialPolicy,
):
    """Group compatible islands and decide whether all completed wave products can be reused."""
    manifest_path = scout_dir / "jobs.tsv"
    columns = [
        "island_id",
        "system",
        "variables",
        "merit",
        "population_file",
        "snapshot_dir",
        "output_dir",
        "seed",
        "population",
        "generations",
        "profile",
        "internal_scaling",
        "glass_search",
    ]
    manifest_path.write_text(
        "\t".join(columns)
        + "\n"
        + "\n".join(("\t".join((str(job[column]) for column in columns)) for job in jobs))
        + "\n",
        encoding="utf-8",
    )
    multi_exe = Path(
        getattr(
            args,
            "glass_scout_multi_island_exe",
            _version.cpp_dir / "build" / "lensde_multi_island.exe",
        )
    ).resolve()
    if not multi_exe.is_file():
        raise FileNotFoundError(f"GlassChoice Scout multi-island executable not found: {multi_exe}")
    broker_stats = scout_dir / "broker_stats.json"
    batch_policy = _version.glass_scout_batch_policy(
        bool(getattr(args, "glass_scout_multi_island_enabled", True)),
        [context["variables_doc"] for context in contexts],
        [context["problem_system"] for context in contexts],
        [common_merit for _context in contexts],
    )
    batched = bool(batch_policy["used"])
    print(
        f"=== GlassChoice Scout: islands={len(jobs)} groups={glass_group_count} rounds={rounds} gen/round={generations} pop={population_size} candidates/island/group={scout_args.glass_candidates} cohorts={batch_policy['cohort_count']} batched_islands={batch_policy['batched_island_count']} serial_islands={batch_policy['serial_island_count']} ===",
        flush=True,
    )
    timeout = int(
        finite_float(getattr(args, "glass_scout_timeout", 0), 0.0)
        or finite_float(getattr(args, "timeout", 0), 0.0)
        or 3600
    )
    reuse_completed_wave = bool(getattr(args, "reuse_glass_choice_scout", False))
    run_results_by_index: dict[int, dict[str, Any]] = {}
    cohort_runs: list[dict[str, Any]] = []
    commands: list[list[str]] = []
    return (
        columns,
        multi_exe,
        batch_policy,
        batched,
        timeout,
        reuse_completed_wave,
        run_results_by_index,
        cohort_runs,
        commands,
    )


def execute_scout_wave(
    reuse_completed_wave,
    contexts,
    batch_policy,
    run_results_by_index,
    scout_dir,
    jobs,
    columns,
    multi_exe,
    args,
    commands,
    timeout,
    cohort_runs,
):
    """Execute the retained cohort order, using broker batches or serial fallback as declared."""
    if reuse_completed_wave:
        missing_artifacts: list[str] = []
        for context in contexts:
            run_dir = context["run_dir"]
            error_path = run_dir / "error.txt"
            if error_path.exists():
                missing_artifacts.append(f"unexpected failure marker: {error_path}")
            for name in ("system.json", "score.json", "result.json"):
                artifact = run_dir / name
                if not artifact.is_file():
                    missing_artifacts.append(str(artifact))
        if missing_artifacts:
            preview = "\n".join(missing_artifacts[:12])
            raise FileNotFoundError(
                f"cannot reuse GlassChoice Scout because completed island outputs are missing or invalid:\n{preview}"
            )
        print(
            f"=== GlassChoice Scout: reuse validated completed island outputs ({len(contexts)} islands) ===",
            flush=True,
        )
        wave_result = {
            "returncode": 0,
            "timed_out": False,
            "seconds": 0.0,
            "reused_completed_wave": True,
        }
        for cohort in batch_policy["cohorts"]:
            for index in cohort["member_indices"]:
                run_results_by_index[index] = {
                    **wave_result,
                    "cohort_id": cohort["cohort_id"],
                    "execution_mode": cohort["execution_mode"],
                }
    else:
        for cohort in batch_policy["cohorts"]:
            cohort_id = str(cohort["cohort_id"])
            cohort_dir = scout_dir / "cohorts" / cohort_id
            cohort_dir.mkdir(parents=True, exist_ok=True)
            cohort_manifest = cohort_dir / "jobs.tsv"
            cohort_jobs: list[dict[str, Any]] = []
            for local_id, index in enumerate(cohort["member_indices"]):
                job = dict(jobs[index])
                job["island_id"] = local_id
                cohort_jobs.append(job)
            cohort_manifest.write_text(
                "\t".join(columns)
                + "\n"
                + "\n".join(
                    ("\t".join((str(job[column]) for column in columns)) for job in cohort_jobs)
                )
                + "\n",
                encoding="utf-8",
            )
            command = [
                str(multi_exe),
                "--jobs",
                str(cohort_manifest),
                "--broker-stats",
                str(cohort_dir / "broker_stats.json"),
                "--coalesce-us",
                str(max(0, int(getattr(args, "glass_scout_multi_island_coalesce_us", 1000)))),
            ]
            if cohort["used"]:
                command.append("--batched")
            write_json(cohort_dir / "command.json", command)
            commands.append(command)
            print(
                f"=== GlassChoice Scout {cohort_id}: mode={cohort['execution_mode']} islands={cohort['island_count']} ===",
                flush=True,
            )
            result = feedback_cycles.run_command(
                command, feedback_cycles.ROOT, cohort_dir / "run.log", timeout, 0
            )
            cohort_run = {
                **result,
                "cohort_id": cohort_id,
                "execution_mode": cohort["execution_mode"],
                "members": cohort["members"],
            }
            cohort_runs.append(cohort_run)
            for index in cohort["member_indices"]:
                run_results_by_index[index] = cohort_run
        write_json(scout_dir / "commands.json", commands)
        if len(commands) == 1:
            write_json(scout_dir / "command.json", commands[0])
        failed_returncodes = [
            int(finite_float(run.get("returncode"), -1))
            for run in cohort_runs
            if int(finite_float(run.get("returncode"), -1)) != 0
        ]
        wave_result = {
            "returncode": failed_returncodes[0] if failed_returncodes else 0,
            "timed_out": any((_row_truthy(run.get("timed_out")) for run in cohort_runs)),
            "seconds": sum((finite_float(run.get("seconds"), 0.0) for run in cohort_runs)),
            "cohorts": cohort_runs,
        }
    return (wave_result,)


def materialize_scout_results(
    contexts,
    run_results_by_index,
    wave_result,
    scout_args,
    common_merit,
    rounds,
    generations,
    glass_group_count,
    scout_libraries,
):
    """Audit catalog glass choices and materialize each island before ranking."""
    rows: list[dict[str, Any]] = []
    for context_index, context in enumerate(contexts):
        case_root = context["case_root"]
        run_dir = context["run_dir"]
        error_path = run_dir / "error.txt"
        individual_ok = (
            int(finite_float(run_results_by_index.get(context_index, {}).get("returncode"), -1))
            == 0
            and (not error_path.exists())
            and (run_dir / "system.json").is_file()
            and (run_dir / "score.json").is_file()
        )
        run_result = dict(run_results_by_index.get(context_index, wave_result))
        if not individual_ok:
            run_result["returncode"] = -1
        score = read_json(run_dir / "score.json") if (run_dir / "score.json").is_file() else {}
        system_out = (
            read_json(run_dir / "system.json")
            if (run_dir / "system.json").is_file()
            else context["source_system"]
        )
        system_out = materialize_glass_source_paths(system_out, context["glass_search"])
        write_json(run_dir / "system.json", system_out)
        output_catalog_audit = (
            feedback_cycles.audit_true_glass_catalog_contract(system_out, context["glass_search"])
            if individual_ok
            else {"ok": False, "skipped": "glass_scout_island_failed"}
        )
        write_json(case_root / "glass_catalog_contract_output.json", output_catalog_audit)
        parent_score = read_json(
            feedback_cycles.file_from_source(context["source_dir"], "score.json")
        )
        row = feedback_cycles.score_to_row(
            cycle=1,
            source_rank=context["position"],
            source_dir=context["source_dir"],
            source_name=context["source_dir"].name,
            material_mode="true_glass_scout",
            algorithm="multi_island_adaptive_jade",
            parent_score=parent_score,
            score=score,
            system=system_out,
            run_result=run_result,
            out_dir=run_dir,
            args=scout_args,
            merit=common_merit,
        )
        row["basin_rank"] = context["basin_rank"]
        row["scout_rounds"] = rounds
        row["scout_generations_per_round"] = generations
        row["scout_glass_group_count"] = glass_group_count
        row["scout_candidate_quota_per_island_group"] = scout_args.glass_candidates
        row["scout_batched_multi_island"] = (
            run_result.get("execution_mode") == "batched_multi_island"
        )
        row["multi_island_cohort_id"] = run_result.get("cohort_id")
        row["glass_libraries"] = scout_libraries
        row["glass_catalog_contract"] = output_catalog_audit
        row["source_bridge_row"] = context["source_row"]
        materialization_result_path = run_dir / "result.json"
        materialization_result = (
            read_json(materialization_result_path) if materialization_result_path.is_file() else {}
        )
        optimizer_merit = finite_float(materialization_result.get("optimizer_best_merit"), math.inf)
        materialized_merit = finite_float(
            materialization_result.get("materialized_merit"),
            finite_float(score.get("merit"), math.inf),
        )
        materialization_merit_relative_difference = abs(materialized_merit - optimizer_merit) / max(
            1.0, abs(materialized_merit), abs(optimizer_merit)
        )
        row.update(
            {
                "backend_materialization_valid": bool(
                    materialization_result.get("materialization_valid", False)
                ),
                "materialization_valid": bool(
                    materialization_result.get("materialization_valid", False)
                ),
                "optimizer_best_merit": optimizer_merit,
                "materialized_merit": materialized_merit,
                "materialization_merit_delta": materialization_result.get(
                    "materialization_merit_delta"
                ),
                "materialization_merit_relative_difference": materialization_merit_relative_difference,
                "materialization_merit_relative_tolerance": GLASS_SCOUT_CPU_CUDA_MERIT_REL_TOL,
                "cpu_cuda_materialization_consistent": cpu_cuda_materialization_consistency(
                    materialization_result,
                    materialization_merit_relative_difference,
                    GLASS_SCOUT_CPU_CUDA_MERIT_REL_TOL,
                ),
            }
        )
        admitted, admission_reasons = fixed_glass_feedback_admission(row, scout_args)
        row["admission_ok"] = admitted
        row["admission_reasons"] = ";".join(admission_reasons)
        write_json(case_root / "summary.json", row)
        rows.append(row)
    return (rows,)


def publish_scout_results(
    rows,
    scout_args,
    scout_dir,
    args,
    rounds,
    glass_group_count,
    population_size,
    generations,
    batch_policy,
    batched,
    scout_libraries,
    glasses,
    wave_result,
):
    """Apply Scout admission and publish the ordered ranking and wave configuration."""
    ranked = feedback_cycles.sort_rows(rows, scout_args)
    write_csv(scout_dir / "glass_scout_ranked.csv", ranked)
    write_json(
        scout_dir / "glass_scout_report.json",
        {
            "policy": "island-local candidate tables with broker-side CUDA index remap; materialize then lock",
            "round_mode": str(getattr(args, "glass_scout_round_mode", "auto")),
            "resolved_rounds": rounds,
            "glass_group_count": glass_group_count,
            "candidate_quota_per_island_group": scout_args.glass_candidates,
            "population": population_size,
            "generations_per_round": generations,
            "batch_policy": batch_policy,
            "batched_multi_island": batched,
            "glass_libraries": scout_libraries,
            "loaded_glass_count": len(glasses),
            "wave": wave_result,
            "rows": ranked,
        },
    )
    if not any((_row_truthy(row.get("admission_ok")) for row in ranked)):
        raise RuntimeError("every GlassChoice Scout island failed materialization validation")
    return (ranked, scout_dir)
