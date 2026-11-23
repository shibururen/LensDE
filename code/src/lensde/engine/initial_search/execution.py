"""Synchronous native stage execution, vignetting handoff and completed-stage reuse."""
from __future__ import annotations
import math
import os
import time
from pathlib import Path
from lensde.runtime import processes as subprocess
from lensde.runtime import native_io_paths
from .. import configuration as _lensde_configuration
from .. import defaults as _lensde_defaults
from .. import execution as _lensde_execution
from .. import provenance as _lensde_provenance
from . import basin_selection as _m_basin_selection
from . import stage_commands as _m_stage_commands
from .contracts import SearchState, PhaseResult

def run_funnel_stage(_flow: SearchState, name: str, args: list[str], stage_dir: Path, checkpoint_snapshot_dir: Path | None=None) -> tuple[int, bool, dict]:
    """Run one owned native stage with the shared deadline and append its logs and receipt in call order."""
    stage_dir.mkdir(parents=True, exist_ok=True)
    stage_started = time.time()
    stage_env = _lensde_provenance.initial_search_stage_environment(args) or os.environ.copy()
    if checkpoint_snapshot_dir is not None:
        checkpoint_snapshot_dir.mkdir(parents=True, exist_ok=True)
        stage_env['OPTILAND_CHECKPOINT_SNAPSHOT_DIR'] = str(checkpoint_snapshot_dir.resolve())
    code, out, err, timed_out = _lensde_execution.run_stage(args, _flow.runtime.timeout_seconds, _flow.runtime.started_at, env=stage_env)
    elapsed = max(0.0, time.time() - stage_started)
    error_message = (err or '').strip()
    stage_record = {'name': name, 'command': args, 'returncode': code, 'timed_out': timed_out, 'elapsed_seconds': elapsed}
    if checkpoint_snapshot_dir is not None:
        stage_record['checkpoint_snapshot_dir'] = str(checkpoint_snapshot_dir.resolve())
    if error_message:
        stage_record['error_message'] = error_message[-4000:]
    _flow.runtime.stages.append(stage_record)
    _flow.runtime.stdout_parts.append(f'\n[stage:{name}]\n' + out)
    if err:
        _flow.runtime.stderr_parts.append(f'[stage:{name}]\n' + err)
    if code != 0 or timed_out:
        (stage_dir / 'stdout.log').write_text(out or '', encoding='utf-8', errors='replace')
        (stage_dir / 'stderr.log').write_text(err or f'Stage {name} exited with code {code}.\n', encoding='utf-8', errors='replace')
    summary = _m_basin_selection.score_summary(stage_dir)
    if error_message:
        summary['error_message'] = error_message[-4000:]
    if summary:
        _flow.runtime.stdout_parts.append(f"[funnel] {name}: merit={_lensde_configuration.finite_float(summary.get('merit'), float('nan')):.6g}, mean_spot={_lensde_configuration.finite_float(summary.get('mean_spot'), float('nan')):.6g}, elapsed={elapsed:.2f}s\n")
    return (code, timed_out, summary)

def run_outer_vignetting_two_stage(_flow: SearchState, name: str, args: list[str], stage_dir: Path, outer_epochs: int, generations_per_epoch: int, checkpoint_snapshot_dir: Path | None=None) -> tuple[int, bool, dict]:
    """Synchronous native stage execution, vignetting handoff and completed-stage reuse. Implements `run_outer_vignetting_two_stage` for this responsibility."""
    'Run the validated loose+strict Outer schedule without adding budget.'
    if not _flow.exploration.outer_two_stage_enabled:
        result = run_funnel_stage(_flow, name, args, stage_dir, checkpoint_snapshot_dir)
        _lensde_provenance.write_json(stage_dir / 'vignetting_two_stage_summary.json', {'mode': 'single_stage', 'total_generations': outer_epochs * generations_per_epoch, 'minimum_relative_illumination': _lensde_configuration.vignetting_minimum_relative_illumination(_flow.optics.initial_stage_merit), 'reason': 'illumination_optimization_disabled', 'returncode': result[0], 'timed_out': result[1]})
        return result
    plan = _lensde_defaults.outer_vignetting_phase_plan(outer_epochs, generations_per_epoch, _flow.settings.options.get('initial_search_funnel_outer_vignetting_strict_fraction', 1.0 / 3.0))
    if int(plan['loose_total_generations']) <= 0:
        code, timed_out, summary = run_funnel_stage(_flow, name, args, stage_dir, checkpoint_snapshot_dir)
        _lensde_provenance.write_json(stage_dir / 'vignetting_two_stage_summary.json', {**plan, 'mode': 'strict_only', 'returncode': code, 'timed_out': timed_out})
        return (code, timed_out, summary)
    loose_name = f'{name}_vignetting_loose'
    loose_dir = stage_dir / 'vignetting_loose'
    loose_args = list(args)
    _m_stage_commands.set_arg(loose_args, '--merit', _flow.optics.outer_loose_merit_path)
    _m_stage_commands.set_arg(loose_args, '--out-dir', loose_dir)
    _m_stage_commands.set_arg(loose_args, '--hybrid-outer', int(plan['loose_outer_epochs']))
    _m_stage_commands.set_arg(loose_args, '--hybrid-de-gen', int(plan['loose_generations_per_epoch']))
    _m_stage_commands.set_arg(loose_args, '--gen', int(plan['loose_generations_per_epoch']))
    if '--progress-every' in loose_args:
        _m_stage_commands.set_arg(loose_args, '--progress-every', math.gcd(50, int(plan['loose_generations_per_epoch'])))
    if '--no-polish' not in loose_args:
        loose_args.append('--no-polish')
    loose_snapshot_dir = loose_dir / 'checkpoint_candidate_snapshots' if checkpoint_snapshot_dir is not None else None
    resumed_loose = resume_existing_funnel_stage(_flow, loose_name, loose_dir, require_checkpoint_snapshots=loose_snapshot_dir is not None)
    if resumed_loose is not None:
        loose_code, loose_timed_out, loose_summary = resumed_loose
    else:
        loose_code, loose_timed_out, loose_summary = run_funnel_stage(_flow, loose_name, loose_args, loose_dir, loose_snapshot_dir)
    loose_population_path = loose_dir / 'population.csv'
    if loose_code != 0 or loose_timed_out or (not loose_population_path.is_file()):
        failure_code = loose_code if loose_code != 0 else 12
        _lensde_provenance.write_json(stage_dir / 'vignetting_two_stage_summary.json', {**plan, 'mode': 'loose_then_strict', 'loose_returncode': loose_code, 'loose_timed_out': loose_timed_out, 'failure': 'loose_phase_did_not_produce_population'})
        return (failure_code, loose_timed_out, loose_summary)
    strict_args = list(args)
    _m_stage_commands.set_arg(strict_args, '--merit', _flow.optics.merit_path)
    _m_stage_commands.set_arg(strict_args, '--out-dir', stage_dir)
    _m_stage_commands.set_arg(strict_args, '--initial-population', loose_population_path)
    _m_stage_commands.set_arg(strict_args, '--hybrid-outer', int(plan['strict_outer_epochs']))
    _m_stage_commands.set_arg(strict_args, '--hybrid-de-gen', int(plan['strict_generations_per_epoch']))
    _m_stage_commands.set_arg(strict_args, '--gen', int(plan['strict_generations_per_epoch']))
    if '--progress-every' in strict_args:
        _m_stage_commands.set_arg(strict_args, '--progress-every', math.gcd(50, int(plan['strict_generations_per_epoch'])))
    code, timed_out, summary = run_funnel_stage(_flow, f'{name}_vignetting_strict', strict_args, stage_dir, checkpoint_snapshot_dir)
    two_stage_summary = {**plan, 'mode': 'loose_then_strict', 'loose_merit': str(_flow.optics.outer_loose_merit_path), 'strict_merit': str(_flow.optics.merit_path), 'loose_population': str(loose_population_path), 'loose_returncode': loose_code, 'loose_timed_out': loose_timed_out, 'strict_returncode': code, 'strict_timed_out': timed_out}
    _lensde_provenance.write_json(stage_dir / 'vignetting_two_stage_summary.json', two_stage_summary)
    summary = {**summary, 'vignetting_two_stage': two_stage_summary}
    return (code, timed_out, summary)

def funnel_stage_outputs_complete(stage_dir: Path) -> bool:
    """Synchronous native stage execution, vignetting handoff and completed-stage reuse. Implements `funnel_stage_outputs_complete` for this responsibility."""
    return subprocess.stage_complete(stage_dir) and (stage_dir / 'system.json').exists() and (stage_dir / 'score.json').exists() and ((stage_dir / 'basin_archive.json').exists() or (stage_dir / 'population.csv').exists())

def checkpoint_snapshot_outputs_complete(stage_dir: Path) -> bool:
    """Synchronous native stage execution, vignetting handoff and completed-stage reuse. Implements `checkpoint_snapshot_outputs_complete` for this responsibility."""
    snapshot_dir = stage_dir / 'checkpoint_candidate_snapshots'
    return (snapshot_dir / 'snapshot_index.jsonl').is_file() and (snapshot_dir / 'candidate_events.jsonl').is_file()

def resume_existing_funnel_stage(_flow: SearchState, name: str, stage_dir: Path, *, require_checkpoint_snapshots: bool=False) -> tuple[int, bool, dict] | None:
    """Reuse a stage only when its requested completion files and policy permit it."""
    if not _flow.exploration.resume_existing_funnel_stages or not funnel_stage_outputs_complete(stage_dir) or (require_checkpoint_snapshots and (not checkpoint_snapshot_outputs_complete(stage_dir))):
        return None
    summary_score = _m_basin_selection.score_summary(stage_dir)
    summary_score.update({'returncode': 0, 'timed_out': False, 'resumed_existing_stage': True})
    _flow.runtime.stages.append({'name': name, 'command': [f'resume-existing:{name}'], 'returncode': 0, 'timed_out': False, 'resumed_existing_stage': True, **summary_score})
    _flow.runtime.stdout_parts.append(f"\n[stage:{name}]\nresume-existing output={stage_dir} merit={summary_score.get('merit')}\n")
    return (0, False, summary_score)

def full_restart_outer_outputs_complete(_flow: SearchState, expected_outer_count: int) -> bool:
    """Synchronous native stage execution, vignetting handoff and completed-stage reuse. Implements `full_restart_outer_outputs_complete` for this responsibility."""
    outer_root = native_io_paths.named_child(_flow.inputs.run_dir, 'funnel_global_explore_outers')
    return all((funnel_stage_outputs_complete(outer_root / f'outer_{outer_index:02d}') and (not _flow.exploration.cra_tighten_enabled or funnel_stage_outputs_complete(_flow.exploration.cra_tightened_outer_root / f'outer_{outer_index:02d}')) and (not True or checkpoint_snapshot_outputs_complete((_flow.exploration.cra_tightened_outer_root if _flow.exploration.cra_tighten_enabled else outer_root) / f'outer_{outer_index:02d}')) for outer_index in range(1, max(1, expected_outer_count) + 1)))

def full_restart_merged_outputs_complete(_flow: SearchState, expected_outer_count: int) -> bool:
    """Synchronous native stage execution, vignetting handoff and completed-stage reuse. Implements `full_restart_merged_outputs_complete` for this responsibility."""
    expected_outer_count = max(1, int(expected_outer_count))
    if not full_restart_outer_outputs_complete(_flow, expected_outer_count):
        return False
    summary_paths = [_flow.inputs.run_dir / 'funnel_global_explore_full_restart_summary.json', _flow.inputs.run_dir / 'funnel_global_explore' / 'full_restart_summary.json']
    for summary_path in summary_paths:
        summary = _lensde_provenance.read_json_optional(summary_path)
        if not isinstance(summary, dict):
            continue
        try:
            completed_outer_count = int(summary.get('completed_outer_count', 0) or 0)
        except (TypeError, ValueError):
            completed_outer_count = 0
        if bool(summary.get('cra_tighten_enabled', False)) != _flow.exploration.cra_tighten_enabled:
            continue
        if _flow.exploration.cra_tighten_enabled and (int(_lensde_configuration.finite_float(summary.get('cra_tighten_generations'), -1.0)) != _flow.exploration.cra_tighten_generations or summary.get('cra_source_merit_fingerprint') != _flow.exploration.cra_source_merit_fingerprint or summary.get('cra_tightened_merit_fingerprint') != _flow.exploration.cra_tightened_merit_fingerprint or (not math.isclose(_lensde_configuration.finite_float(summary.get('cra_tightened_tolerance'), float('nan')), _flow.exploration.cra_tightened_tolerance, rel_tol=0.0, abs_tol=1e-12))):
            continue
        if completed_outer_count >= expected_outer_count:
            return True
    return False
