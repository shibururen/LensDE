"""Publish the retained workflow result, manifests and summaries without running another optimization."""
from __future__ import annotations
from .. import commands as _lensde_commands
from .. import defaults as _lensde_defaults
from .. import materials as _lensde_materials
from .. import populations as _lensde_populations
from .. import provenance as _lensde_provenance
from .contracts import SearchState, PhaseResult

def run_phase(_flow: SearchState) -> PhaseResult:
    """Publish the retained workflow result, manifests and summaries without running another optimization."""
    profile_summary = _lensde_defaults.initial_search_profile_summary(_flow.settings.options)
    _flow.settings.de_dls_summary['initial_search_run_profile'] = profile_summary['profile']
    _flow.settings.de_dls_summary['profile_summary'] = profile_summary
    _lensde_provenance.write_json(_flow.inputs.run_dir / 'de_dls_material_funnel_stage.json', _flow.settings.de_dls_summary)
    global_summary = _flow.exploration.funnel_summary['global'][-1] if _flow.exploration.funnel_summary['global'] else {}
    restart_summary = _lensde_defaults.restart_decision_summary(_flow.settings.options, _flow.settings.de_dls_summary, global_summary)
    _lensde_provenance.write_json(_flow.inputs.run_dir / 'restart_decision_summary.json', restart_summary)
    final_returncode = -9 if _flow.exploration.timed_out else _flow.exploration.code
    effective_final_merit = _lensde_provenance.read_json(_flow.inputs.run_dir / 'merit.json') if (_flow.inputs.run_dir / 'merit.json').exists() else _flow.optics.downstream_funnel_merit
    scores = _lensde_populations.read_population_scores_csv(_flow.exploration.global_explore_dir / 'population_scores.csv') if (_flow.exploration.global_explore_dir / 'population_scores.csv').exists() else []
    summary = {'strategy': 'initial_structure_search', 'optimizer': _flow.settings.search_optimizer_mode, 'initial_search_run_profile': profile_summary['profile'], 'profile_summary': profile_summary, 'restart_decision_summary': restart_summary, 'flow': _flow.settings.flow, 'final_stage': 'de_dls_material_funnel', 'flat_start': bool(_flow.settings.options.get('initial_search_flat_start', True)), 'target_efl': _flow.inputs.target_efl, 'target_ttl': _flow.inputs.target_ttl, 'target_bfl': _flow.inputs.target_bfl, 'population': _flow.settings.population, 'seed_rows': len(_flow.seeding.seed_rows), 'initial_population_mode': _flow.seeding.seed_strategy, 'mechanical_stratified_direct': _flow.settings.baseline_seed_config if _flow.settings.mechanical_direct_seed else None, 'variable_count': len(_flow.optics.variables), 'global_explore': {'dir': str(_flow.exploration.global_explore_dir), 'outer': _flow.exploration.global_explore_outer, 'de_gen': _flow.exploration.global_explore_de_gen, 'summary': global_summary}, 'de_dls_material_funnel': _flow.settings.de_dls_summary, 'compact_outputs': bool(_flow.settings.options.get('initial_search_de_dls_compact_outputs', True)), 'continuous_glass_proximity_constraints': _flow.optics.continuous_glass_constraints_count, 'population_summary': _lensde_commands.population_score_summary(scores), 'score': _lensde_provenance.read_json(_flow.inputs.run_dir / 'score.json') if (_flow.inputs.run_dir / 'score.json').exists() else None, 'search_score': _lensde_provenance.read_json(_flow.exploration.global_explore_dir / 'score.json') if (_flow.exploration.global_explore_dir / 'score.json').exists() else None}
    _lensde_provenance.write_json(_flow.inputs.run_dir / 'strategy_summary.json', summary)
    _lensde_provenance.write_json(_flow.inputs.run_dir / 'funnel_summary.json', {**_flow.exploration.funnel_summary, 'initial_search_run_profile': profile_summary['profile'], 'profile_summary': profile_summary, 'restart_decision_summary': restart_summary, 'de_dls_material_funnel': _flow.settings.de_dls_summary})
    _flow.settings.run_meta = {'strategy': 'initial_structure_search', 'optimizer': _flow.settings.search_optimizer_mode, 'initial_search_run_profile': profile_summary['profile'], 'profile_summary': profile_summary, 'restart_decision_summary': restart_summary, 'flow': _flow.settings.flow, 'timeout_seconds': _flow.runtime.timeout_seconds, 'timeout_source': 'project.options.cpp_timeout', 'use_cuda': _flow.runtime.use_cuda, 'executable': str(_flow.runtime.exe), 'stages': _flow.runtime.stages, 'options': _flow.settings.options}
    _lensde_provenance.write_json(_flow.inputs.run_dir / 'run_meta.json', _flow.settings.run_meta)
    (_flow.inputs.run_dir / 'command.txt').write_text('\n\n'.join((' '.join(stage.get('command', [])) for stage in _flow.runtime.stages)), encoding='utf-8')
    _lensde_provenance.write_repro_manifest(_flow.inputs.run_dir, _flow.inputs.project, _flow.settings.options, _flow.runtime.stages, _flow.runtime.exe, strategy='initial_structure_search', optimizer=_flow.settings.search_optimizer_mode, merit_strategy=_flow.optics.merit_strategy, flow=_flow.settings.flow, run_meta=_flow.settings.run_meta, returncode=final_returncode, timed_out=_flow.exploration.timed_out)
    return PhaseResult(True, _lensde_materials.collect_run_result(_flow.inputs.run_dir, effective_final_merit, final_returncode, ''.join(_flow.runtime.stdout_parts), '\n'.join(_flow.runtime.stderr_parts), timed_out=_flow.exploration.timed_out, timeout_seconds=_flow.runtime.timeout_seconds if _flow.runtime.timeout_seconds > 0 else None, checkpoint_restored=_flow.exploration.timed_out and (_flow.inputs.run_dir / 'score.json').exists()))
    return PhaseResult(False, None)
