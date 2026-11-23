"""Initial seed CPU scoring and trace gates, sharing the same gate diagnostics."""
from __future__ import annotations
import math
import time
from pathlib import Path
from lensde.runtime import native_io_paths
from .. import configuration as _lensde_configuration
from .. import execution as _lensde_execution
from .. import geometry as _lensde_geometry
from .. import initialization as _lensde_initialization
from .. import materials as _lensde_materials
from .. import populations as _lensde_populations
from .contracts import SearchState, PhaseResult

def score_seed_candidate_rows(_flow: SearchState, name: str, rows: list[list[float]], *, gate_dir: Path, cpu_recheck_deficit: int=0) -> tuple[list[dict], dict]:
    """Score seed rows through the existing native gate, retaining row identities and diagnostic accumulation."""
    stage_dir = native_io_paths.named_child(gate_dir, name)
    stage_dir.mkdir(parents=True, exist_ok=True)
    pool_path = stage_dir / 'seed_pool.csv'
    score_dir = stage_dir / 'score'
    score_dir.mkdir(parents=True, exist_ok=True)
    write_started = time.time()
    _lensde_populations.write_population_csv(pool_path, rows)
    write_elapsed = max(0.0, time.time() - write_started)
    score_args = [str(_flow.runtime.exe), '--system', str(_flow.optics.system_path), '--merit', str(_flow.seeding.seed_gate_merit_path), '--aiming', _flow.settings.aiming_mode, '--aiming-iter', str(int(_flow.settings.options.get('aiming_iter', 20) or 20)), '--trace-policy', str(_flow.seeding.seed_gate_merit.get('trace_policy', 'soft_coverage')), '--rays', str(int(_flow.seeding.seed_gate_merit.get('num_rays', _flow.optics.search_merit['num_rays']))), '--arms', str(int(_flow.seeding.seed_gate_merit.get('pupil_arms', _flow.optics.search_merit['pupil_arms']))), '--distribution', str(_flow.seeding.seed_gate_merit.get('pupil_distribution', _flow.optics.search_merit['pupil_distribution'])), '--sampling-seed', str(int(_flow.seeding.seed_gate_merit.get('sampling_seed', 1))), '--spot-rays', str(int(_flow.settings.options.get('spot_rays', 41))), '--spot-arms', str(int(_flow.settings.options.get('spot_arms', 64))), '--spot-distribution', str(_flow.settings.options.get('spot_distribution', 'uniform')), '--spot-sampling-seed', str(int(_flow.settings.options.get('spot_sampling_seed', _flow.seeding.seed_gate_merit.get('sampling_seed', 1)))), '--variables', str(_flow.optics.variables_path), '--initial-population', str(pool_path), '--score-population-only', '--out-dir', str(score_dir)]
    _lensde_initialization.append_asphere_reference_policy_args(score_args, _flow.optics.variables, 'input')
    _lensde_materials.append_aiming_policy_args(score_args, _flow.settings.options)
    if _flow.settings.options.get('clear_aperture_update', True):
        score_args.append('--clear-aperture-update')
    if _flow.settings.options.get('sync_stop_aperture', True):
        score_args.append('--sync-stop-aperture')
    _lensde_materials.append_glass_search_args(score_args, _flow.optics.glass_search_path, _flow.optics.glass_search_config, _flow.settings.options)
    if not _flow.runtime.use_cuda:
        score_args.append('--cpu')
    stage_started = time.time()
    code, out, err, timed_out = _lensde_execution.run_stage(score_args, _flow.runtime.timeout_seconds, _flow.runtime.started_at)
    elapsed = max(0.0, time.time() - stage_started)
    stage_output = f'\n[stage:{name}]\n' + out
    _flow.seeding.seed_gate_stdout_parts.append(stage_output)
    if _flow.runtime.stdout_parts is not None:
        _flow.runtime.stdout_parts.append(stage_output)
    if err:
        _flow.seeding.seed_gate_stderr_parts.append(f'[stage:{name}]\n' + err)
        if _flow.runtime.stderr_parts is not None:
            _flow.runtime.stderr_parts.append(f'[stage:{name}]\n' + err)
    score_used_cuda = 'Score population evaluator: CUDA' in out
    records = [] if timed_out or code != 0 else _lensde_materials.read_population_score_records(score_dir / 'population_scores.csv')
    objects: list[dict] = []
    rejected_records: list[tuple[dict, int]] = []
    for record in records:
        try:
            row_index = int(float(record.get('index', '')))
        except (TypeError, ValueError):
            continue
        if not 0 <= row_index < len(rows):
            continue
        trace_feasible = _lensde_materials.score_record_trace_feasible(record, _flow.seeding.seed_gate_merit)
        budget_feasible = _lensde_materials.score_record_seed_budget_feasible(record, _flow.seeding.seed_ttl_projection, allow_trace_upper=True)
        material_region_count = _lensde_populations.material_seed_row_region_count(rows[row_index], _flow.optics.variables, _flow.seeding.material_region_sampling_plan)
        material_region_diverse = not _flow.seeding.material_region_diversity_required or material_region_count >= 2
        strict = trace_feasible and budget_feasible and material_region_diverse
        obj = {'row': rows[row_index], 'record': record, 'strict': strict, 'trace_feasible': trace_feasible, 'seed_budget_feasible': budget_feasible, 'material_region_count': material_region_count, 'material_region_diverse': material_region_diverse, 'merit': _lensde_configuration.finite_float(record.get('merit'), float('inf')), 'trace_coverage': _lensde_configuration.finite_float(record.get('trace_coverage'), 0.0), 'min_field_coverage': _lensde_configuration.finite_float(record.get('min_field_coverage'), 0.0), 'source_stage': name, 'source_index': row_index}
        if _lensde_geometry.variable_list_has_asphere_coeff(_flow.optics.variables):
            profile_item = _flow.seeding.asphere_profile_metadata.get(_lensde_initialization.initial_asphere_profile_key(rows[row_index], _flow.optics.variables), {})
            obj['asphere_profile'] = _lensde_initialization.initial_asphere_profile_label(rows[row_index], _flow.optics.variables, _flow.seeding.asphere_profile_metadata)
            obj['asphere_profile_sibling'] = bool(profile_item.get('sibling', False))
        objects.append(obj)
        if not trace_feasible and budget_feasible:
            rejected_records.append((record, row_index))
    cpu_recheck_elapsed = 0.0
    cpu_recheck_count = 0
    cpu_recheck_accepted = 0
    if _flow.runtime.use_cuda and score_used_cuda and (cpu_recheck_deficit > 0) and rejected_records:
        cpu_recheck_limit = int(_flow.settings.options.get('initial_search_population_cpu_recheck_max', 0) or 0)
        if cpu_recheck_limit <= 0:
            cpu_recheck_limit = min(len(rejected_records), max(256, cpu_recheck_deficit * 4))

        def record_number(record: dict, key: str, default: float) -> float:
            return _lensde_configuration.finite_float(record.get(key), default)
        ranked_rejected = sorted(rejected_records, key=lambda item: (-record_number(item[0], 'trace_coverage', 0.0), -record_number(item[0], 'min_field_coverage', 0.0), -record_number(item[0], 'trace_progress', 0.0), record_number(item[0], 'merit', float('inf'))))
        recheck_items = ranked_rejected[:max(0, cpu_recheck_limit)]
        recheck_rows = [rows[row_index] for _record, row_index in recheck_items]
        if recheck_rows:
            cpu_stage = f'{name}_cpu_recheck'
            cpu_dir = native_io_paths.named_child(gate_dir, cpu_stage)
            cpu_dir.mkdir(parents=True, exist_ok=True)
            cpu_pool_path = cpu_dir / 'seed_pool.csv'
            cpu_score_dir = cpu_dir / 'score'
            cpu_score_dir.mkdir(parents=True, exist_ok=True)
            _lensde_populations.write_population_csv(cpu_pool_path, recheck_rows)
            cpu_score_args = list(score_args)
            for index, value in enumerate(cpu_score_args):
                if value == '--initial-population' and index + 1 < len(cpu_score_args):
                    cpu_score_args[index + 1] = str(cpu_pool_path)
                elif value == '--out-dir' and index + 1 < len(cpu_score_args):
                    cpu_score_args[index + 1] = str(cpu_score_dir)
            if '--cpu' not in cpu_score_args:
                cpu_score_args.append('--cpu')
            cpu_started = time.time()
            cpu_code, cpu_out, cpu_err, cpu_timed_out = _lensde_execution.run_stage(cpu_score_args, _flow.runtime.timeout_seconds, _flow.runtime.started_at)
            cpu_recheck_elapsed = max(0.0, time.time() - cpu_started)
            cpu_recheck_count = len(recheck_rows)
            _flow.seeding.seed_gate_stdout_parts.append(f'\n[stage:{cpu_stage}]\n' + cpu_out)
            if cpu_err:
                _flow.seeding.seed_gate_stderr_parts.append(f'[stage:{cpu_stage}]\n' + cpu_err)
            if not cpu_timed_out and cpu_code == 0:
                by_key = {_lensde_populations.variable_aware_seed_row_key(obj['row'], _flow.optics.variables): obj for obj in objects}
                for cpu_record in _lensde_materials.read_population_score_records(cpu_score_dir / 'population_scores.csv'):
                    try:
                        recheck_index = int(float(cpu_record.get('index', '')))
                    except (TypeError, ValueError):
                        continue
                    if not 0 <= recheck_index < len(recheck_rows):
                        continue
                    if not (_lensde_materials.score_record_trace_feasible(cpu_record, _flow.seeding.seed_gate_merit) and _lensde_materials.score_record_seed_budget_feasible(cpu_record, _flow.seeding.seed_ttl_projection, allow_trace_upper=True)):
                        continue
                    key = _lensde_populations.variable_aware_seed_row_key(recheck_rows[recheck_index], _flow.optics.variables)
                    target_obj = by_key.get(key)
                    if target_obj is not None:
                        material_ok = bool(target_obj.get('material_region_diverse', True))
                        if not target_obj.get('strict') and material_ok:
                            cpu_recheck_accepted += 1
                        target_obj['strict'] = material_ok
                        target_obj['trace_feasible'] = True
                        target_obj['seed_budget_feasible'] = True
                        target_obj['record'] = cpu_record
                        target_obj['merit'] = _lensde_configuration.finite_float(cpu_record.get('merit'), target_obj.get('merit', float('inf')))
                        target_obj['trace_coverage'] = _lensde_configuration.finite_float(cpu_record.get('trace_coverage'), 0.0)
                        target_obj['min_field_coverage'] = _lensde_configuration.finite_float(cpu_record.get('min_field_coverage'), 0.0)
                        target_obj['source_stage'] = cpu_stage
    strict_count = sum((1 for obj in objects if obj.get('strict')))
    trace_feasible_count = sum((1 for obj in objects if obj.get('trace_feasible')))
    seed_budget_feasible_count = sum((1 for obj in objects if obj.get('seed_budget_feasible')))
    material_region_diverse_count = sum((1 for obj in objects if obj.get('material_region_diverse')))
    summary = {'name': name, 'scored': len(rows), 'records': len(records), 'strict_count': strict_count, 'trace_feasible_count': trace_feasible_count, 'seed_budget_feasible_count': seed_budget_feasible_count, 'material_region_diversity_required': _flow.seeding.material_region_diversity_required, 'material_region_diverse_count': material_region_diverse_count, 'strict_rate': strict_count / len(rows) if rows else 0.0, 'elapsed_seconds': elapsed, 'candidates_per_second': len(rows) / elapsed if elapsed > 0.0 else None, 'accepted_per_second': strict_count / elapsed if elapsed > 0.0 else None, 'out_dir': str(stage_dir), 'returncode': code, 'timed_out': timed_out, 'score_used_cuda': score_used_cuda, 'csv_write_seconds': write_elapsed, 'cpu_recheck_candidates': cpu_recheck_count, 'cpu_recheck_accepted': cpu_recheck_accepted, 'cpu_recheck_elapsed_seconds': cpu_recheck_elapsed}
    _flow.seeding.seed_gate_stages.append({'name': name, 'command': score_args, **summary})
    _flow.seeding.seed_gate_stdout_parts.append(f"[seed_trace_gate_v2] {name}: strict={strict_count}/{len(rows)}, rate={summary['strict_rate']:.6g}, elapsed={elapsed:.3f}s\n")
    return (objects, summary)

def filter_strict_seed_objects(objects: list[dict]) -> list[dict]:
    """Initial seed CPU scoring and trace gates, sharing the same gate diagnostics. Implements `filter_strict_seed_objects` for this responsibility."""
    return [obj for obj in objects if obj.get('strict')]

def maybe_trace_gate_initial_seed_population(_flow: SearchState, rows: list[list[float]]) -> list[list[float]]:
    """Apply the configured initial seed gate and refill policy using the same stage inputs and shared failure state."""
    if not bool(_flow.settings.options.get('initial_search_population_trace_feasible_gate', False)):
        return rows
    target = int(_flow.settings.options.get('initial_search_population_target_count', 0) or 0)
    if target <= 0:
        target = _flow.seeding.seed_pool_target
    target = max(1, target)
    max_candidates = int(_flow.settings.options.get('initial_search_population_max_candidates', 0) or 0)
    if max_candidates <= 0:
        max_candidates = max(target, target * 32)
    max_seconds = max(0.0, _lensde_configuration.finite_float(_flow.settings.options.get('initial_search_population_max_seconds'), 0.0))
    batch_size = int(_flow.settings.options.get('initial_search_population_batch_size', 0) or 0)
    if batch_size <= 0:
        batch_floor = 8192 if _flow.runtime.use_cuda else 2048
        batch_size = min(max(max(target, 32), batch_floor), max_candidates)
    batch_size = max(1, batch_size)
    gate_dir = _flow.settings.search_dir / 'seed_trace_gate'
    gate_dir.mkdir(parents=True, exist_ok=True)
    accepted: list[list[float]] = []
    attempts = 0
    iteration = 0
    gate_start = time.time()
    while len(accepted) < target and attempts < max_candidates:
        if max_seconds > 0.0 and time.time() - gate_start >= max_seconds:
            break
        batch = min(batch_size, max_candidates - attempts)
        generation_started = time.time()
        if iteration == 0:
            candidate_rows = rows[:batch]
        else:
            candidate_rows = _lensde_populations.make_hybrid_initial_seed_population(_flow.optics.base_system, _flow.optics.variables, _flow.seeding.front_pairs, batch, _flow.settings.seed + 1909 + iteration, _flow.inputs.target_ttl, _flow.inputs.target_bfl, _flow.seeding.rest_cap, _flow.seeding.initial_curvature_bound, population_generation_mode=str(_flow.settings.options.get('initial_search_population_generation_mode', 'restricted_perturbation')), front_profile=str(_flow.settings.options.get('initial_search_front_profile', 'weak')), seed_air_thickness_cap=_flow.seeding.seed_air_thickness_cap, seed_glass_thickness_cap=_flow.seeding.seed_glass_thickness_cap, seed_material_nd_cap=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_seed_material_nd_cap'), 0.0), seed_material_vd_cap=_lensde_configuration.finite_float(_flow.settings.options.get('initial_search_seed_material_vd_cap'), 0.0), local_group_perturbation=bool(_flow.settings.options.get('initial_search_population_local_group_perturbation', False)), local_long_air_gap_prior=bool(_flow.settings.options.get('initial_search_population_local_long_air_gap_prior', True)), perturb_material_variables=bool(_flow.settings.options.get('initial_search_population_perturb_material', True)), perturb_other_variables=bool(_flow.settings.options.get('initial_search_population_perturb_other_variables', False)), target_efl=_flow.inputs.target_efl, asphere_profile_metadata=_flow.seeding.asphere_profile_metadata, **_flow.seeding.seed_ttl_projection_kwargs)
        generation_elapsed = max(0.0, time.time() - generation_started)
        if not candidate_rows:
            break
        attempts += len(candidate_rows)
        pool_path = gate_dir / f'seed_pool_{iteration:03d}.csv'
        score_dir = gate_dir / f'score_{iteration:03d}'
        score_dir.mkdir(parents=True, exist_ok=True)
        write_started = time.time()
        _lensde_populations.write_population_csv(pool_path, candidate_rows)
        write_elapsed = max(0.0, time.time() - write_started)
        score_args = [str(_flow.runtime.exe), '--system', str(_flow.optics.system_path), '--merit', str(_flow.seeding.seed_gate_merit_path), '--aiming', _flow.settings.aiming_mode, '--aiming-iter', str(int(_flow.settings.options.get('aiming_iter', 20) or 20)), '--trace-policy', str(_flow.seeding.seed_gate_merit.get('trace_policy', 'soft_coverage')), '--rays', str(int(_flow.seeding.seed_gate_merit.get('num_rays', _flow.optics.search_merit['num_rays']))), '--arms', str(int(_flow.seeding.seed_gate_merit.get('pupil_arms', _flow.optics.search_merit['pupil_arms']))), '--distribution', str(_flow.seeding.seed_gate_merit.get('pupil_distribution', _flow.optics.search_merit['pupil_distribution'])), '--sampling-seed', str(int(_flow.seeding.seed_gate_merit.get('sampling_seed', 1))), '--spot-rays', str(int(_flow.settings.options.get('spot_rays', 41))), '--spot-arms', str(int(_flow.settings.options.get('spot_arms', 64))), '--spot-distribution', str(_flow.settings.options.get('spot_distribution', 'uniform')), '--spot-sampling-seed', str(int(_flow.settings.options.get('spot_sampling_seed', _flow.seeding.seed_gate_merit.get('sampling_seed', 1)))), '--variables', str(_flow.optics.variables_path), '--initial-population', str(pool_path), '--score-population-only', '--out-dir', str(score_dir)]
        _lensde_initialization.append_asphere_reference_policy_args(score_args, _flow.optics.variables, 'input')
        _lensde_materials.append_aiming_policy_args(score_args, _flow.settings.options)
        if _flow.settings.options.get('clear_aperture_update', True):
            score_args.append('--clear-aperture-update')
        if _flow.settings.options.get('sync_stop_aperture', True):
            score_args.append('--sync-stop-aperture')
        _lensde_materials.append_glass_search_args(score_args, _flow.optics.glass_search_path, _flow.optics.glass_search_config, _flow.settings.options)
        if not _flow.runtime.use_cuda:
            score_args.append('--cpu')
        stage_started = time.time()
        code, out, err, timed_out = _lensde_execution.run_stage(score_args, _flow.runtime.timeout_seconds, _flow.runtime.started_at)
        stage_elapsed = max(0.0, time.time() - stage_started)
        _flow.seeding.seed_gate_stages.append({'name': f'seed_trace_gate_{iteration:03d}', 'command': score_args, 'returncode': code, 'timed_out': timed_out, 'elapsed_seconds': stage_elapsed, 'candidates_per_second': len(candidate_rows) / stage_elapsed if stage_elapsed > 0.0 else None, 'generation_seconds': generation_elapsed, 'csv_write_seconds': write_elapsed})
        _flow.seeding.seed_gate_stdout_parts.append(f'\n[stage:seed_trace_gate_{iteration:03d}]\n' + out)
        if err:
            _flow.seeding.seed_gate_stderr_parts.append(f'[stage:seed_trace_gate_{iteration:03d}]\n' + err)
        _flow.seeding.seed_gate_stages[-1]['score_used_cuda'] = 'Score population evaluator: CUDA' in out
        if timed_out or code != 0:
            break
        read_started = time.time()
        records = _lensde_materials.read_population_score_records(score_dir / 'population_scores.csv')
        read_elapsed = max(0.0, time.time() - read_started)
        accept_started = time.time()
        accepted_before = len(accepted)
        rejected_records: list[tuple[dict, int]] = []
        for record in records:
            try:
                row_index = int(float(record.get('index', '')))
            except (TypeError, ValueError):
                continue
            if not 0 <= row_index < len(candidate_rows):
                continue
            trace_feasible = _lensde_materials.score_record_trace_feasible(record, _flow.seeding.seed_gate_merit)
            budget_feasible = _lensde_materials.score_record_seed_budget_feasible(record, _flow.seeding.seed_ttl_projection)
            if trace_feasible and budget_feasible:
                if len(accepted) < target:
                    accepted.append(candidate_rows[row_index])
            elif budget_feasible:
                rejected_records.append((record, row_index))
        accept_elapsed = max(0.0, time.time() - accept_started)
        _flow.seeding.seed_gate_stages[-1]['score_read_seconds'] = read_elapsed
        _flow.seeding.seed_gate_stages[-1]['accept_filter_seconds'] = accept_elapsed
        cpu_recheck_elapsed = 0.0
        cpu_recheck_count = 0
        cpu_recheck_accepted = 0
        if _flow.runtime.use_cuda and _flow.seeding.seed_gate_stages[-1].get('score_used_cuda') and (len(accepted) < target) and rejected_records:
            deficit = max(1, target - len(accepted))
            cpu_recheck_limit = int(_flow.settings.options.get('initial_search_population_cpu_recheck_max', 0) or 0)
            if cpu_recheck_limit <= 0:
                cpu_recheck_limit = min(len(rejected_records), max(256, deficit * 4))

            def record_number(record: dict, key: str, default: float) -> float:
                try:
                    value = float(record.get(key, default))
                except (TypeError, ValueError):
                    return default
                return value if math.isfinite(value) else default
            ranked_rejected = sorted(rejected_records, key=lambda item: (-record_number(item[0], 'trace_coverage', 0.0), -record_number(item[0], 'min_field_coverage', 0.0), -record_number(item[0], 'trace_progress', 0.0), record_number(item[0], 'merit', float('inf'))))
            recheck_items = ranked_rejected[:max(0, cpu_recheck_limit)]
            recheck_rows = [candidate_rows[row_index] for _, row_index in recheck_items]
            if recheck_rows:
                recheck_pool_path = gate_dir / f'seed_pool_{iteration:03d}_cpu_recheck.csv'
                recheck_score_dir = gate_dir / f'score_{iteration:03d}_cpu_recheck'
                recheck_score_dir.mkdir(parents=True, exist_ok=True)
                _lensde_populations.write_population_csv(recheck_pool_path, recheck_rows)
                cpu_score_args = list(score_args)
                for index, value in enumerate(cpu_score_args):
                    if value == '--initial-population' and index + 1 < len(cpu_score_args):
                        cpu_score_args[index + 1] = str(recheck_pool_path)
                    elif value == '--out-dir' and index + 1 < len(cpu_score_args):
                        cpu_score_args[index + 1] = str(recheck_score_dir)
                if '--cpu' not in cpu_score_args:
                    cpu_score_args.append('--cpu')
                cpu_recheck_started = time.time()
                cpu_code, cpu_out, cpu_err, cpu_timed_out = _lensde_execution.run_stage(cpu_score_args, _flow.runtime.timeout_seconds, _flow.runtime.started_at)
                cpu_recheck_elapsed = max(0.0, time.time() - cpu_recheck_started)
                cpu_recheck_count = len(recheck_rows)
                _flow.seeding.seed_gate_stdout_parts.append(f'\n[stage:seed_trace_gate_{iteration:03d}_cpu_recheck]\n' + cpu_out)
                if cpu_err:
                    _flow.seeding.seed_gate_stderr_parts.append(f'[stage:seed_trace_gate_{iteration:03d}_cpu_recheck]\n' + cpu_err)
                _flow.seeding.seed_gate_stages[-1]['cpu_recheck_command'] = cpu_score_args
                _flow.seeding.seed_gate_stages[-1]['cpu_recheck_returncode'] = cpu_code
                _flow.seeding.seed_gate_stages[-1]['cpu_recheck_timed_out'] = cpu_timed_out
                _flow.seeding.seed_gate_stages[-1]['cpu_recheck_elapsed_seconds'] = cpu_recheck_elapsed
                _flow.seeding.seed_gate_stages[-1]['cpu_recheck_candidates'] = cpu_recheck_count
                if not cpu_timed_out and cpu_code == 0:
                    cpu_records = _lensde_materials.read_population_score_records(recheck_score_dir / 'population_scores.csv')
                    accepted_before_recheck = len(accepted)
                    for cpu_record in cpu_records:
                        try:
                            row_index = int(float(cpu_record.get('index', '')))
                        except (TypeError, ValueError):
                            continue
                        if 0 <= row_index < len(recheck_rows) and _lensde_materials.score_record_trace_feasible(cpu_record, _flow.seeding.seed_gate_merit) and _lensde_materials.score_record_seed_budget_feasible(cpu_record, _flow.seeding.seed_ttl_projection):
                            accepted.append(recheck_rows[row_index])
                            if len(accepted) >= target:
                                break
                    cpu_recheck_accepted = len(accepted) - accepted_before_recheck
                    _flow.seeding.seed_gate_stages[-1]['cpu_recheck_accepted'] = cpu_recheck_accepted
        _flow.seeding.seed_gate_stdout_parts.append(f'[seed_trace_gate] batch={iteration}, accepted_batch={len(accepted) - accepted_before}, accepted_total={len(accepted)}, target={target}, attempts={attempts}, gen={generation_elapsed:.3f}s, write={write_elapsed:.3f}s, score={stage_elapsed:.3f}s, read={read_elapsed:.3f}s, filter={accept_elapsed:.3f}s, cpu_recheck={cpu_recheck_count}/{cpu_recheck_accepted} in {cpu_recheck_elapsed:.3f}s, rate={(len(candidate_rows) / stage_elapsed if stage_elapsed > 0.0 else 0.0):.1f}/s\n')
        iteration += 1
    if len(accepted) < target:
        _flow.seeding.seed_gate_failed_message = f'[seed_trace_gate] trace-feasible seed search found {len(accepted)} / {target} after {attempts} candidates; increase initial_search_population_max_candidates or initial_search_population_max_seconds, or loosen the initial perturbation bounds.'
        _flow.seeding.seed_gate_stderr_parts.append(_flow.seeding.seed_gate_failed_message)
        return []
    gated_rows = accepted[:target]
    duplicate_fill = 0
    while len(gated_rows) < target:
        gated_rows.append(list(accepted[duplicate_fill % len(accepted)]))
        duplicate_fill += 1
    _flow.seeding.seed_gate_stdout_parts.append(f'[seed_trace_gate] accepted={len(accepted)}, target={target}, attempts={attempts}, duplicate_fill={duplicate_fill}\n')
    return gated_rows[:target]
