"""Run or resume the global exploration phase, then preserve its CRA handoff."""
from __future__ import annotations
from .. import commands as _lensde_commands
from .. import provenance as _lensde_provenance
from . import execution as _m_execution
from . import restart_exploration as _m_restart_exploration
from . import stage_commands as _m_stage_commands
from .contracts import SearchState, PhaseResult

def run_phase(_flow: SearchState) -> PhaseResult:
    """Run or resume the global exploration phase, then preserve its CRA handoff."""
    if _flow.settings.probe_cycle_enabled:
        cycle_explore_outer = max(1, int(_flow.settings.options.get('initial_search_funnel_probe_cycle_global_outer', 4) or 4))
        cycle_develop_outer = max(1, int(_flow.settings.options.get('initial_search_funnel_probe_cycle_global_develop_outer', 1) or 1))
        cycle_de_gen = max(1, int(_flow.settings.options.get('initial_search_funnel_probe_cycle_global_de_gen', 80) or 80))
        _flow.settings.options['initial_search_funnel_global_explore_outer'] = cycle_explore_outer
        _flow.settings.options['initial_search_funnel_global_develop_outer'] = cycle_develop_outer
        _flow.settings.options['initial_search_funnel_global_explore_de_gen'] = cycle_de_gen
        _flow.settings.options['initial_search_funnel_global_develop_de_gen'] = cycle_de_gen
        _flow.runtime.stdout_parts.append(f"[GUI] Funnel probe-cycle mode enabled: cycles={int(_flow.settings.options.get('initial_search_funnel_probe_cycle_count', 3) or 3)}, global_outer={cycle_explore_outer}, develop_outer={cycle_develop_outer}, de_gen={cycle_de_gen}\n")
    _flow.exploration.global_explore_dir = _flow.inputs.run_dir / 'funnel_global_explore'
    _flow.exploration.global_explore_outer = max(1, int(_flow.settings.options.get('initial_search_funnel_global_explore_outer', 24) or 24))
    _flow.exploration.global_explore_de_gen = max(1, int(_flow.settings.options.get('initial_search_funnel_global_explore_de_gen', 1000) or 1000))
    global_explore_de_use_cuda = _lensde_commands.option_truthy(_flow.settings.options.get('initial_search_funnel_global_explore_de_use_cuda'), True)
    explore_seed_stride = max(1, int(_flow.settings.options.get('initial_search_explore_develop_seed_stride', 10000) or 10000))
    global_explore_full_restart_enabled = bool(_flow.settings.options.get('initial_search_funnel_global_explore_full_restart_enabled', False))
    can_resume_global_explore = not global_explore_full_restart_enabled or _m_execution.full_restart_merged_outputs_complete(_flow, _flow.exploration.global_explore_outer)
    if _flow.exploration.resume_existing_funnel_stages and global_explore_full_restart_enabled and (not can_resume_global_explore):
        _flow.runtime.stdout_parts.append('[GUI] Existing merged full-restart output does not cover all requested outers; resuming completed outer stages individually and rebuilding the merge.\n')
    resumed_global_explore = _m_execution.resume_existing_funnel_stage(_flow, 'funnel_global_explore', _flow.exploration.global_explore_dir, require_checkpoint_snapshots=True and (not global_explore_full_restart_enabled)) if can_resume_global_explore else None
    if resumed_global_explore is not None:
        _flow.exploration.code, _flow.exploration.timed_out, summary_score = resumed_global_explore
        _flow.exploration.funnel_summary['global'].append({'name': 'funnel_global_explore', 'dir': str(_flow.exploration.global_explore_dir), **summary_score})
    elif global_explore_full_restart_enabled:
        _flow.exploration.code, _flow.exploration.timed_out, summary_score = _m_restart_exploration.run_full_restart_global_explore(_flow, _flow.exploration.global_explore_outer, _flow.exploration.global_explore_de_gen, explore_seed_stride)
        _flow.exploration.funnel_summary['global'].append({'name': 'funnel_global_explore', 'dir': str(_flow.exploration.global_explore_dir), 'full_restart': True, 'de_evaluator': 'CUDA' if global_explore_de_use_cuda else 'CPU', 'returncode': _flow.exploration.code, 'timed_out': _flow.exploration.timed_out, **summary_score})
    else:
        global_explore_args = _m_stage_commands.build_initial_search_stage_args(_flow, _flow.exploration.global_explore_dir, _flow.optics.system_path, _flow.optics.merit_path, _flow.optics.initial_stage_merit, _flow.seeding.seed_path, _flow.exploration.global_explore_outer, 1009, stage_population=_flow.settings.population, stage_de_gen=_flow.exploration.global_explore_de_gen, stage_de_use_cuda=global_explore_de_use_cuda)
        _flow.exploration.code, _flow.exploration.timed_out, summary_score = _m_execution.run_outer_vignetting_two_stage(_flow, 'funnel_global_explore', _m_stage_commands.configure_funnel_global_args(_flow, global_explore_args, no_lpsr=not False, no_polish=not _flow.exploration.funnel_global_polish_enabled, disable_basins=False), _flow.exploration.global_explore_dir, _flow.exploration.global_explore_outer, _flow.exploration.global_explore_de_gen, _flow.exploration.global_explore_dir / 'checkpoint_candidate_snapshots' if True else None)
        _flow.exploration.funnel_summary['global'].append({'name': 'funnel_global_explore', 'dir': str(_flow.exploration.global_explore_dir), 'de_evaluator': 'CUDA' if global_explore_de_use_cuda else 'CPU', **summary_score})
    if _flow.exploration.cra_tighten_enabled and (not _flow.exploration.timed_out) and (_flow.exploration.code == 0):
        _flow.optics.merit_path = _flow.optics.downstream_funnel_merit_path
        _flow.optics.initial_stage_merit = _flow.optics.downstream_funnel_merit
        _flow.optics.final_stage_merit = _flow.optics.downstream_funnel_merit
        _flow.optics.final_merit_path = _flow.optics.downstream_funnel_merit_path
        _lensde_provenance.write_json(_flow.inputs.run_dir / 'merit.json', _flow.optics.downstream_funnel_merit)
    _lensde_commands.copy_stage_outputs(_flow.exploration.global_explore_dir, _flow.inputs.run_dir)
    _flow.settings.de_dls_summary: dict = {}
    return PhaseResult(False, None)
