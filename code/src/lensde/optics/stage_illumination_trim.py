"""Bounded physical-aperture proposals at vignetting stage boundaries only."""
from __future__ import annotations
import copy
import csv
import math
from pathlib import Path
import time

CONTRACT = 'stage_illumination_margin_trim_v2'
DEFAULTS = dict(enabled=True, edge_min_radius=0.85, activation_margin=0.03,
                safety_margin=0.01, max_radius_fraction=0.03,
                window_fraction=0.5, max_trials=4, max_surfaces=2, max_feedback_passes=1,
                radius_match_tolerance=0.005)


def policy(merit):
    result = dict(DEFAULTS)
    supplied = merit.get('stage_illumination_trim', {})
    if not isinstance(supplied, dict):
        raise ValueError('stage_illumination_trim must be an object')
    result.update({k: supplied[k] for k in result if k in supplied})
    for key in result:
        if key == 'enabled':
            if not isinstance(result[key], bool):
                raise ValueError('stage_illumination_trim.enabled must be boolean')
            continue
        value = float(result[key])
        if not math.isfinite(value) or value < 0:
            raise ValueError(f'invalid stage_illumination_trim.{key}')
        result[key] = value
    if not 0 < result['edge_min_radius'] <= 1 or not 0 < result['window_fraction'] <= 1 or not 0 < result['max_radius_fraction'] < 1:
        raise ValueError('invalid stage illumination trim fractions')
    result['max_trials'] = min(8, int(result['max_trials']))
    result['max_surfaces'] = min(4, int(result['max_surfaces']))
    result['max_feedback_passes'] = min(2, int(result['max_feedback_passes']))
    return result


def eligible_fields(system, score, floor, config):
    fields = system.get('fields', [])
    values = score.get('relative_illumination_proxy_by_field', [])
    if len(values) != len(fields):
        return []
    radii = [math.hypot(f.get('hx', 0), f.get('hy', 0)) for f in fields]
    maximum = max(radii, default=0)
    floors = score.get('relative_illumination_floor_by_field')
    return [i for i, (radius, value) in enumerate(zip(radii, values))
            if radius > 1e-12 and radius >= maximum * config['edge_min_radius']
            and (floors is None or (i < len(floors) and floors[i] is not None))
            and isinstance(value, (int, float)) and math.isfinite(value)
            and value > floor + config['activation_margin']]


def automatic_surface(surface):
    return not (surface.get('kind') in ('object', 'image') or surface.get('is_stop')
                or surface.get('clear_aperture_fixed') or surface.get('aperture_radius', 0) > 0
                or surface.get('aperture'))


def scan_axis(system):
    # The native RI tool interpolates on one radial cardinal scan. Do not
    # pretend that this measures a mixed-axis/azimuthal system.
    fields = system.get('fields', [])
    for axis, dx, dy in [('+y', 0, 1), ('-y', 0, -1), ('+x', 1, 0), ('-x', -1, 0)]:
        if all(abs(f.get('hx', 0)*dy-f.get('hy', 0)*dx) < 1e-12
               and f.get('hx', 0)*dx+f.get('hy', 0)*dy >= 0 for f in fields):
            if abs(max((math.hypot(f.get('hx', 0), f.get('hy', 0)) for f in fields), default=0)-1) < 1e-12:
                return axis
    return None


def set_arg(command, flag, value):
    if flag in command:
        command[command.index(flag)+1] = str(value)
    else:
        command.extend([flag, str(value)])


def dense_values(directory):
    with (directory/'relative_illumination.csv').open(encoding='utf-8-sig', newline='') as stream:
        rows = list(csv.DictReader(stream))
    values = [float(r['relative_illumination']) for r in rows]
    if not values or not all(math.isfinite(v) for v in values):
        raise RuntimeError(f'invalid illumination output: {directory}')
    return rows, values


def score_summary(score):
    return dict(merit=score.get('merit'), trace_coverage=score.get('trace_coverage'),
                minimum_field_coverage=score.get('min_field_coverage'),
                relative_illumination=score.get('relative_illumination_proxy_by_field'),
                mean_spot=score.get('mean_spot'),
                physical_penalty=sum(c.get('penalty', 0) for c in score.get('contributions', [])
                                     if c.get('name') == 'vignetting_boundary_aperture_support'))


def suggest(api, source, source_score, merit, baseline_measurement, output, executable,
            base_command, timeout_seconds=0, child_cpu_workers=0, *, support_surface_indices=None):
    """Return a measured physical proposal, or the unchanged baseline proposal.

    Actual optimizer merit/trace semantics stay in the caller's command. The
    additional checks select an aperture suggestion only; they never remove a
    candidate or change optimizer ranking. No persistent aperture cap is added.
    """
    output = api.native_io_paths.python_path(output)
    config = policy(merit)
    report = dict(contract=CONTRACT, performed=False, policy=config, trials=[],
                  source_system_sha256=api.json_fingerprint(source))
    floor = float(merit.get('minimum_relative_illumination', 0))
    eligible = eligible_fields(source, source_score, floor, config)
    def finish(reason, measurement=baseline_measurement):
        report['reason'] = reason
        api.write_json(output/'report.json', report)
        return measurement, report
    if not config['enabled'] or floor <= 0 or not eligible or not config['max_trials']:
        return finish('disabled_or_no_edge_illumination_margin')
    axis = scan_axis(source)
    if axis is None:
        return finish('native_illumination_scan_does_not_cover_source_fields')
    if not api.vignetting_handoff_policy.score_usable(source_score):
        return finish('source_score_unavailable')
    for forbidden in ('--de', '--hybrid', '--polish', '--adaptive-hybrid', '--hammer'):
        if forbidden in base_command:
            raise ValueError('aperture trim requires a score-only stage command')
    output.mkdir(parents=True, exist_ok=True)
    api.write_json(output/'merit.json', merit)

    def evaluate(system, name, *, dynamic=True, distribution=None, rays=None, illumination=False):
        directory = api.native_io_paths.named_path(output, name)
        directory.mkdir(parents=True, exist_ok=True)
        api.write_json(directory/'input.json', system)
        command = [c for c in base_command if c not in
                   ('--score-only', '--relative-illumination', '--clear-aperture-update',
                    '--no-clear-aperture-update', '--sync-stop-aperture')]
        set_arg(command, '--system', directory/'input.json')
        set_arg(command, '--merit', output/'merit.json')
        set_arg(command, '--out-dir', directory)
        # Export size only; optimizer score sampling is unchanged.
        set_arg(command, '--spot-rays', 1)
        command.append('--clear-aperture-update' if dynamic else '--no-clear-aperture-update')
        if dynamic:
            command = api.dynamic_aperture_contract.apply_command(command)
        if distribution: set_arg(command, '--distribution', distribution)
        if rays: set_arg(command, '--rays', rays)
        if illumination:
            command += ['--relative-illumination', '--ri-keep-vignetting-factors', '--ri-normalize-on-axis']
            for flag, value in [('--ri-ray-density', 24), ('--ri-field-density', 201), ('--ri-scan', axis)]:
                set_arg(command, flag, value)
        result = api.run_command_logged(command, directory, timeout_seconds, time.time(), child_cpu_workers)
        if result.get('returncode') != 0:
            raise RuntimeError(f'stage aperture probe failed: {directory}')
        return api.read_json(directory/'system.json'), api.read_json(directory/'score.json'), directory

    _, baseline_score, directory = evaluate(source, 'source', dynamic=False, illumination=True)
    rows, baseline_dense = dense_values(directory)
    # Both proxy and neighboring physical integral samples must show margin.
    # All source fields are checked, including every edge guard.
    def nearby_min(field):
        radius = math.hypot(field.get('hx', 0), field.get('hy', 0))
        lo = max(0, min(199, int(radius*200)))
        return min(baseline_dense[lo:lo+2])
    eligible = [i for i in eligible if nearby_min(source['fields'][i]) > floor+config['activation_margin']]
    if not eligible or min(baseline_dense) < floor:
        return finish('physical_illumination_has_no_safe_margin')
    protected = [i for i, field in enumerate(source['fields'])
                 if math.hypot(field.get('hx', 0), field.get('hy', 0)) <= 1e-12]
    if not protected:
        return finish('center_field_envelope_unavailable')
    report.update(eligible_fields=eligible, protected_fields=protected,
                  source_score=score_summary(source_score),
                  source_dense_minimum=min(baseline_dense))
    envelopes = [[0.0]*len(source['surfaces']) for _ in source['fields']]
    directories = [directory]
    for name, distribution, rays in [('envelope4', 'gaussian_ring_arm', 4), ('cardinals', 'cross', 3)]:
        _, _, directory = evaluate(source, name, dynamic=False, distribution=distribution, rays=rays)
        directories.append(directory)
    for directory in directories:
        with (directory/'raytrace.csv').open(encoding='utf-8-sig', newline='') as stream:
            for row in csv.DictReader(stream):
                fi, si = int(row['field_index']), int(row['surface_index'])
                if row['valid'] != '1' or row['backward_propagation'] != '0':
                    if fi in protected:
                        return finish('protected_ray_envelope_unavailable')
                    continue
                # Spectral tracing separates geometric success from physical
                # transmission. Protect every transmitted central wavelength,
                # not rays already clipped by the source apertures. Check real
                # geometry failures first, even on an aperture-blocked ray.
                if row.get('aperture_blocked', '0') == '1':
                    continue
                radius = math.hypot(float(row['x']), float(row['y']))
                if not math.isfinite(radius):
                    return finish('nonfinite_ray_envelope')
                envelopes[fi][si] = max(envelopes[fi][si], radius)
    protection = [max((envelopes[i][si] for i in protected), default=0)
                  for si in range(len(source['surfaces']))]
    report['protected_aperture_radii'] = protection
    api.write_json(output/'field_envelopes.json', envelopes)
    owners = {int(c['term'])//2 for c in source_score.get('contributions', [])
              if c.get('name') == 'vignetting_boundary_aperture_support' and c.get('index') in eligible}
    candidates = []
    excluded = set(api.protective_glass_surface_indices(source))
    for si, surface in enumerate(source['surfaces']):
        if si in excluded:
            continue
        # A restricted handoff must not introduce a new blocking surface while
        # proposing a margin trim, or subsequent closure would drop that cut.
        if support_surface_indices is not None and si not in support_surface_indices:
            continue
        if not automatic_surface(surface):
            continue
        radius = surface.get('semi_aperture', 0)
        if radius > 0 and 0 < protection[si] < radius*(1-1e-6):
            candidates.append((si, radius, radius-protection[si]))
    # Follow optical surface order, prioritizing known edge owners.
    # This is independent of merit and does not assume a particular surface.
    candidates.sort(key=lambda item: (item[0] not in owners, item[0]))
    for si, original, window in candidates[:config['max_surfaces']]:
        delta = min(original*config['max_radius_fraction'], window*config['window_fraction'])
        for attempt in range(2):
            if len(report['trials']) >= config['max_trials']: break
            radius = original-delta/(2**attempt)
            trial = dict(surface_index=si, original_radius=original, proposed_radius=radius)
            report['trials'].append(trial)
            materialized = copy.deepcopy(baseline_measurement['system'])
            materialized['surfaces'][si].update(semi_aperture=radius, aperture_radius=radius,
                clear_aperture_fixed=True, aperture={'type':'RadialAperture','r_max':radius,'r_min':0})
            name = f'trial_{len(report["trials"]):02d}'
            feedback = []
            for feedback_pass in range(config['max_feedback_passes']+1):
                pass_name = name if feedback_pass == 0 else name+f'/feedback_{feedback_pass:02d}'
                try:
                    measurement = api.exact_apg_set_vignetting_system(materialized, api.native_io_paths.named_child(api.native_io_paths.named_path(output, pass_name), 'measurement'),
                        executable=executable, timeout_seconds=timeout_seconds, child_cpu_workers=child_cpu_workers,
                        support_surface_indices=support_surface_indices,
                        asphere_root_policy=(base_command[base_command.index('--asphere-root-policy')+1]
                                             if '--asphere-root-policy' in base_command else 'unified_certified'))
                except RuntimeError as exc:
                    measurement_dir = api.exact_apg_set_vignetting_output_dir(api.native_io_paths.named_child(api.native_io_paths.named_path(output, pass_name), 'measurement'))
                    if str(exc).startswith('exact APG Set Vignetting did not satisfy the full boundary trace contract:') or (
                        str(exc).startswith('exact APG Set Vignetting failed:') and api.vignetting_handoff_policy.optical_measurement_failure(api.native_io_paths.named_child(measurement_dir, 'set_vignetting'))):
                        trial.update(accepted=False, reason='optical_measurement_unavailable', error=str(exc))
                        measurement = None
                        break
                    raise
                physical, _ = api.zemax_set_vignetting_hard_aperture_copy(
                    measurement['system'], support_surface_indices=support_surface_indices)
                _, physical_score, physical_dir = evaluate(physical, pass_name+'/physical', illumination=True)
                restored = api.post_asphere_optimizer_system_from_measurement(source, measurement['system'])
                dynamic, dynamic_score, dynamic_dir = evaluate(restored, pass_name+'/dynamic', illumination=True)
                _, physical_dense = dense_values(physical_dir)
                _, dynamic_dense = dense_values(dynamic_dir)
                aperture_changes = [
                    dict(surface_index=j, measured_radius=measured_surface['semi_aperture'],
                         dynamic_radius=dynamic['surfaces'][j]['semi_aperture'],
                         relative_change=abs(dynamic['surfaces'][j]['semi_aperture']-measured_surface['semi_aperture'])/
                             max(abs(measured_surface['semi_aperture']), 1e-12))
                    for j, measured_surface in enumerate(measurement['system']['surfaces'])
                    if automatic_surface(source['surfaces'][j])]
                maximum_change = max((r['relative_change'] for r in aperture_changes), default=0)
                all_apertures_matched = maximum_change <= config['radius_match_tolerance']
                feedback.append(dict(pass_index=feedback_pass, aperture_changes=aperture_changes,
                    maximum_aperture_relative_change=maximum_change, all_apertures_matched=all_apertures_matched,
                    dynamic_merit=dynamic_score.get('merit')))
                if all_apertures_matched:
                    break
                # Update ALL automatic radii for the next physical measurement;
                # keep the proposed cut only in the measurement copy. Every CP
                # evaluation still restores the original source aperture flags.
                materialized = copy.deepcopy(dynamic)
                materialized['surfaces'][si].update(semi_aperture=radius, aperture_radius=radius,
                    clear_aperture_fixed=True, aperture={'type':'RadialAperture','r_max':radius,'r_min':0})
            trial['aperture_feedback'] = feedback
            if measurement is None:
                continue
            values = [*physical_score.get('relative_illumination_proxy_by_field', []),
                      *dynamic_score.get('relative_illumination_proxy_by_field', [])]
            illumination_ok = (len(values) == 2*len(source['fields']) and
                all(isinstance(v, (int,float)) and math.isfinite(v) and v >= floor+config['safety_margin'] for v in values) and
                min(physical_dense+dynamic_dense) >= floor+config['safety_margin'])
            actual_radius = dynamic['surfaces'][si]['semi_aperture']
            protection_ok = all(s.get('semi_aperture', 0)+1e-8 >= protection[j]
                                for j, s in enumerate(dynamic['surfaces']) if s.get('kind') not in ('object','image'))
            persistence_ok = (actual_radius < original-0.5*(original-radius) and
                              abs(actual_radius-radius) <= radius*config['radius_match_tolerance'])
            center_unchanged = all(
                abs(float(measurement['system']['fields'][i].get(k, 0))-float(source['fields'][i].get(k, 0))) <= 1e-8
                for i in protected for k in ('fvcx', 'fvcy', 'fvdx', 'fvdy'))
            usable = api.vignetting_handoff_policy.score_usable(dynamic_score)
            trial.update(physical_score=score_summary(physical_score), dynamic_score=score_summary(dynamic_score),
                physical_dense_minimum=min(physical_dense), dynamic_dense_minimum=min(dynamic_dense),
                dynamic_radius=actual_radius, illumination_ok=illumination_ok, protected_envelope_ok=protection_ok,
                trim_survives_dynamic_update=persistence_ok, optimizer_score_usable=usable,
                measurement_dir=measurement['output_dir'], dynamic_output_dir=str(dynamic_dir),
                all_automatic_apertures_matched=all_apertures_matched,
                center_pupil_preserved=center_unchanged,
                accepted=bool(illumination_ok and protection_ok and persistence_ok and usable and center_unchanged and all_apertures_matched))
            if trial['accepted']:
                report.update(performed=True, selected_surface=si, selected_trial=len(report['trials']),
                              measured_system_sha256=api.json_fingerprint(measurement['system']))
                return finish('physical_margin_trim_measured_and_dynamic_audited', measurement)
    return finish('no_usable_physical_trim_keep_baseline_measurement')
