"""Post-asphere RI selection and bounded pupil calibration.

No optimizer, prescription perturbation, physical penalty override, or pupil
contraction is performed here. Only invalid tracing/metrics or an excessive
aperture change reject the calibration and retain the scored parent.
"""
from __future__ import annotations

import copy
import math
from pathlib import Path
import shutil
import time

INDICES = 'minimum_relative_illumination_field_indices'
RADII = 'minimum_relative_illumination_field_radii'
CONTRACT = 'post_asphere_bounded_vignetting_calibration_full_except_cover_v5'
COEFFICIENTS = ('fvcx', 'fvcy', 'fvdx', 'fvdy')
MAX_ROUNDS = 10
STABLE_RADIUS_UM = 0.01
STABLE_COEFFICIENT = 1e-6
STABLE_ROUNDS = 2
# Native system JSON has 12 significant digits. Recomputing/synchronizing the
# stop radius from a rounded input can move its last serialized digit. This is
# a comparison tolerance only; it does not change physical aperture limits.
APERTURE_ROUNDTRIP_REL_TOL = 2e-11


def field_radii(system):
    return [math.hypot(float(f.get('hx', 0)), float(f.get('hy', 0)))
            for f in system.get('fields', [])]


def selected_fields(system, merit):
    radii = field_radii(system)
    if merit.get(INDICES) is not None and merit.get(RADII) is not None:
        raise ValueError('RI indices and reference radii are mutually exclusive')
    if merit.get(RADII) is not None:
        references = merit[RADII]
        if not isinstance(references, list) or not references or not radii:
            raise ValueError('RI reference radii require a nonempty field contract')
        if any(isinstance(x, bool) or not isinstance(x, (int, float)) or
               not math.isfinite(x) or not 0 <= x <= 1000000 for x in references):
            raise ValueError('invalid RI reference radius')
        return sorted({min(range(len(radii)), key=lambda i: abs(radii[i]-x))
                       for x in references})
    if merit.get(INDICES) is not None:
        indices = merit[INDICES]
        if not isinstance(indices, list) or not indices or any(
            isinstance(i, bool) or not isinstance(i, (int, float)) or
            not math.isfinite(i) or i != int(i) or not 0 <= i < len(radii)
            for i in indices
        ):
            raise ValueError('invalid RI field indices')
        return sorted(set(map(int, indices)))
    return list(range(len(radii)))


def configure_illumination(merit, options, reference_system=None):
    result = copy.deepcopy(merit)
    mode = str(options.get('initial_search_post_asphere_illumination_supervision', 'key_fields')).lower()
    if mode not in ('key_fields', 'all_fields', 'key_fields_then_all_fields', 'late_all_fields', 'inherit'):
        raise ValueError('post-asphere RI supervision must be key_fields, all_fields, key_fields_then_all_fields, late_all_fields or inherit')
    if mode == 'inherit':
        return result
    result.pop(INDICES, None)
    if mode == 'all_fields' or not 0 < float(result.get('minimum_relative_illumination', 0) or 0) <= 1:
        result.pop(RADII, None)
        return result
    references = result.get(RADII, [0.0, 0.25, 0.5, 0.75, 1.0])
    if references is None:
        references = [0.0, 0.25, 0.5, 0.75, 1.0]
    if reference_system is not None:
        source_radii = sorted(set(field_radii(reference_system)))
        if 1 < len(source_radii) <= 5:
            references = source_radii
        elif source_radii and references == [0.0, 0.25, 0.5, 0.75, 1.0]:
            references = [max(source_radii)*x/4 for x in range(5)]
    result[RADII] = list(references)
    return result


def metrics(score):
    contributions = score.get('contributions', [])
    return dict(merit=float(score['merit']), image=float(score['image_penalty']),
                mean_spot=float(score['mean_spot']), wavefront=float(score['mean_wavefront_rms']),
                rms=[float(c['value']) for c in contributions if c.get('name') == 'rms_spot'],
                ri=list(score['relative_illumination_proxy_by_field']),
                matching=sum(float(c.get('penalty', 0)) for c in contributions
                             if c.get('name') == 'vignetting_boundary_aperture_support'),
                manufacturing={name:sum(float(c.get('penalty', 0)) for c in contributions
                                       if c.get('name') == name)
                               for name in ('surface_bl0', 'surface_tangent_axis_angle_deg')})


def delta(before, after):
    radii = []
    for i, (a, b) in enumerate(zip(before['surfaces'], after['surfaces'])):
        if a.get('kind') in ('object', 'image'):
            continue
        old, new = float(a['semi_aperture']), float(b['semi_aperture'])
        radii.append(dict(surface=i, before_mm=old, after_mm=new,
                          delta_um=(new-old)*1000, fraction=(new-old)/max(abs(old), 1e-12),
                          mechanical_diameter_delta_um=2000*(float(b.get('mechanical_aperture', new))-
                                                            float(a.get('mechanical_aperture', old)))))
    coefficients = [abs(float(b.get(key, 0))-float(a.get(key, 0)))
                    for a,b in zip(before['fields'],after['fields']) for key in COEFFICIENTS]
    return dict(apertures=radii, max_radius_change_um=max((abs(x['delta_um']) for x in radii), default=0),
                max_mechanical_radius_change_um=max((abs(x['mechanical_diameter_delta_um'])/2 for x in radii), default=0),
                max_radius_fraction=max((abs(x['fraction']) for x in radii), default=0),
                max_coefficient_change=max(coefficients, default=0))


def numerically_stable(step):
    return (step['max_radius_change_um'] <= STABLE_RADIUS_UM
            and step['max_mechanical_radius_change_um'] <= STABLE_RADIUS_UM
            and step['max_coefficient_change'] <= STABLE_COEFFICIENT)


def assert_prescription(before, after):
    def same(a, b, rel_tol=1e-12):
        if isinstance(a, dict) and isinstance(b, dict):
            return all(key in b and same(value, b[key], rel_tol) for key, value in a.items())
        if isinstance(a, list) and isinstance(b, list):
            return len(a) == len(b) and all(same(x, y, rel_tol) for x, y in zip(a, b))
        if isinstance(a, (int, float)) and isinstance(b, (int, float)):
            return math.isclose(a, b, rel_tol=rel_tol, abs_tol=1e-12)
        return a == b

    for a, b in zip(before['surfaces'], after['surfaces'], strict=True):
        for key in ('kind', 'radius', 'conic', 'thickness', 'asphere_coefficients',
                    'is_stop', 'material_after'):
            if key in a and not same(a[key], b.get(key)):
                raise RuntimeError(f'vignetting closure changed prescription: {key}')
        for key in ('clear_aperture_fixed', 'aperture_radius', 'aperture'):
            default = False if key == 'clear_aperture_fixed' else 0 if key == 'aperture_radius' else None
            if key == 'aperture_radius' and ((a.get(key, 0) > 0) != (b.get(key, 0) > 0)):
                raise RuntimeError(f'vignetting closure changed aperture policy: {key}')
            if not same(a.get(key, default), b.get(key, default), APERTURE_ROUNDTRIP_REL_TOL):
                raise RuntimeError(f'vignetting closure changed aperture policy: {key}')
        if (a.get('is_stop') or a.get('clear_aperture_fixed') or
                a.get('aperture_radius', 0) > 0 or a.get('aperture')):
            if not same(a.get('semi_aperture'), b.get('semi_aperture'), APERTURE_ROUNDTRIP_REL_TOL):
                raise RuntimeError('vignetting closure changed a fixed aperture')
    for key in ('wavelengths', 'aperture', 'ray_aiming'):
        if key in before and not same(before[key], after.get(key)):
            raise RuntimeError(f'vignetting closure changed system contract: {key}')
    if [(f.get('hx', 0), f.get('hy', 0)) for f in before['fields']] != [
            (f.get('hx', 0), f.get('hy', 0)) for f in after['fields']]:
        raise RuntimeError('vignetting closure changed field coordinates')


def publish(api, report, destination):
    """Copy only the accepted native system/score and its matching artifacts."""
    if not report.get('accepted'):
        return
    source = Path(report['accepted_score_dir'])
    for name in ('system.json', 'score.json', 'layout.svg', 'spot.csv', 'raytrace.csv',
                 'relative_illumination.csv', 'vignetting_audit.json'):
        if (source/name).is_file():
            shutil.copyfile(source/name, Path(destination)/name)
    api.write_json(Path(destination)/'vignetting_closure_report.json', report)


def validity_gates(api, score, system):
    try:
        last = metrics(score)
        count = len(system['fields'])
        values = [last[k] for k in ('merit','image','mean_spot','wavefront')]
        values += last['rms']+last['ri']
        valid = (len(last['rms']) == len(last['ri']) == count
                 and all(isinstance(x, (int,float)) and math.isfinite(x) for x in values))
        return dict(complete_metrics=valid,
            trace=api.vignetting_handoff_policy.score_usable(score))
    except (KeyError, TypeError, ValueError):
        return dict(complete_metrics=False)


def validity_gates_pass(gates):
    return all(gates.values())


def close(api, system, variables, merit, options, output, executable,
          timeout_seconds=0, started_at=None, child_cpu_workers=0, *, baseline=None,
          support_surface_indices=None):
    """Return (system, variables, score, report), retaining parent on rejection."""
    output = api.native_io_paths.python_path(Path(output))
    enabled = api.option_truthy(options.get('initial_search_post_asphere_vignetting_closure_enabled'), True)
    report = dict(contract=CONTRACT, performed=False, accepted=False, rounds=[],
                  no_optimization=True, no_pupil_contraction=True,
                  expansion_guard_is_not_mechanical_acceptance=True)
    # Capture the incoming owners once, before any dynamic radius/score update.
    if support_surface_indices is not None:
        support_surface_indices = sorted(set(support_surface_indices))
        report.update(measurement_apertures='original_support_union_plus_stop',
                      original_support_surface_indices=support_surface_indices,
                      support_set_policy='fixed_across_closure_rounds')
    else:
        report.update(
            measurement_apertures='all_displayed_apertures_except_protective_glass',
            excluded_surface_indices=api.protective_glass_surface_indices(system),
            support_set_policy='full_aperture_default_except_protective_glass',
        )
    parent, parent_variables = copy.deepcopy(system), copy.deepcopy(variables)
    if not enabled or not api.vignetting_optimization_enabled(merit):
        report['reason'] = 'disabled_or_no_vignetting'
        return parent, parent_variables, baseline, report
    normalized = api.vignetting_optimizer_aperture_copy(parent)
    if normalized != parent:
        # Removing an old cover-glass clip invalidates its paired score.
        parent, baseline = normalized, None
    limit = api.option_int_from(options, 'initial_search_post_asphere_vignetting_closure_max_rounds', MAX_ROUNDS, 1)
    if limit > MAX_ROUNDS:
        raise ValueError(f'vignetting closure is bounded to at most {MAX_ROUNDS} rounds')
    report.update(max_rounds=limit, cumulative_radius_fraction_guard=0.05,
                  acceptance_policy='last_valid_bounded_round',
                  quality_and_relative_illumination_gates=False,
                  convergence_gate=False, roundtrip_gate=False,
                  early_stop_enabled=True,
                  early_stop_thresholds=dict(radius_um=STABLE_RADIUS_UM,
                                             mechanical_radius_um=STABLE_RADIUS_UM,
                                             coefficient=STABLE_COEFFICIENT,
                                             consecutive_rounds=STABLE_ROUNDS),
                  source_system_sha256=api.json_fingerprint(parent))
    started_at = time.time() if started_at is None else started_at

    def remaining():
        return min(300, api.remaining_timeout_seconds(timeout_seconds, started_at)) if timeout_seconds > 0 else 300

    def evaluate(value, label, dynamic=True, artifacts=False):
        directory = output/label
        scoped_variables = api.sync_post_asphere_variables_for_system(
            value, copy.deepcopy(parent_variables), merit, rebase_asphere=True)
        api.write_json(directory/'input.json', value)
        api.write_json(directory/'variables.json', {'variables': scoped_variables})
        api.write_json(directory/'merit.json', merit)
        command = api.post_asphere_stage_base_args(executable, directory/'input.json',
            directory/'merit.json', directory/'variables.json', options, dynamic_apertures=dynamic)
        if artifacts:
            api.append_vignetting_audit_args(command, merit)
        else:
            command.append('--score-only')
        command += ['--out-dir', str(directory)]
        result = api.run_command_logged(command, directory, remaining(), time.time(), child_cpu_workers)
        if result.get('returncode') != 0:
            raise RuntimeError(f'vignetting closure score failed: {directory}')
        scored = api.read_json(directory/'system.json')
        # The native score is authoritative for automatic radii, but never allow
        # this calibration helper to change the optical prescription.
        assert_prescription(value, scored)
        synced = api.sync_variables_from_system(scoped_variables, scored, rebase_asphere=True)
        return scored, api.read_json(directory/'score.json'), synced, directory

    if baseline is None:
        parent, baseline, parent_variables, _ = evaluate(parent, 'base')
    report['baseline_score'] = copy.deepcopy(baseline)
    api.write_json(output/'baseline_system.json', parent)
    current = copy.deepcopy(parent)

    def finish(reason, accepted=False, value=None, score=None, scoped_variables=None):
        report.update(reason=reason, accepted=accepted, source_retained=not accepted)
        api.write_json(output/'report.json', report)
        return (value, scoped_variables, score, report) if accepted else (parent, parent_variables, baseline, report)

    if not validity_gates_pass(validity_gates(api, baseline, parent)):
        return finish('baseline_not_eligible_for_closure')
    report['performed'] = True
    accepted_value = accepted_score = accepted_variables = accepted_dir = None
    last_measurement = None
    stable_rounds = 0
    for iteration in range(1, limit+1):
        try:
            measurement = api.exact_apg_set_vignetting_system(current, output/f'r{iteration}_sv',
                executable=executable, timeout_seconds=remaining(), child_cpu_workers=child_cpu_workers,
                require_full_boundary_trace=False,
                support_surface_indices=support_surface_indices,
                asphere_root_policy=api.post_asphere_root_scoring_policy(options))
        except RuntimeError as exc:
            directory = api.exact_apg_set_vignetting_output_dir(output/f'r{iteration}_sv')
            if (str(exc).startswith('exact APG Set Vignetting failed:') and
                    api.vignetting_handoff_policy.optical_measurement_failure(
                        api.native_io_paths.named_child(directory, 'set_vignetting'))):
                report['measurement_error'] = str(exc)
                return finish('optical_measurement_unavailable_keep_parent')
            raise
        measured = measurement['system']
        if measured['surfaces'] != current['surfaces'] or measured.get('ray_aiming') != current.get('ray_aiming'):
            raise RuntimeError('exact Set Vignetting changed the prescription or aperture policy')
        dynamic, score, scoped_variables, scored_dir = evaluate(
            measured, f'r{iteration}_d', artifacts=iteration == limit)
        step, cumulative = delta(current, dynamic), delta(parent, dynamic)
        stable = numerically_stable(step)
        if stable and stable_rounds + 1 >= STABLE_ROUNDS and iteration < limit:
            # Emit matching artifacts from this round's measured input. Scoring
            # the already-updated output would silently add another aperture step.
            dynamic, score, scoped_variables, scored_dir = evaluate(
                measured, f'r{iteration}_d', artifacts=True)
            step, cumulative = delta(current, dynamic), delta(parent, dynamic)
            stable = numerically_stable(step)
        stable_rounds = stable_rounds + 1 if stable else 0
        gates = validity_gates(api, score, dynamic)
        gates['bounded_apertures'] = cumulative['max_radius_fraction'] <= 0.05
        row = dict(round=iteration, step=step, cumulative=cumulative,
                   numerical_stable=stable, consecutive_stable_rounds=stable_rounds,
                   score=metrics(score), acceptance_gates=gates,
                   measurement_report=measurement['report'], scored_dir=str(scored_dir))
        report['rounds'].append(row)
        api.write_json(output/'report.json', report)
        if not validity_gates_pass(gates):
            return finish('proposal_failed_validity_or_aperture_bound')
        current = dynamic
        accepted_value, accepted_score = dynamic, score
        accepted_variables, accepted_dir = scoped_variables, scored_dir
        last_measurement = measurement
        if stable_rounds >= STABLE_ROUNDS:
            break
    report['completed_rounds'] = len(report['rounds'])
    report['numerically_stable'] = stable_rounds >= STABLE_ROUNDS
    report['accepted_score_dir'] = str(accepted_dir)
    report['last_measurement'] = last_measurement
    api.write_json(output/'system.json', accepted_value)
    api.write_json(output/'variables.json', {'variables': accepted_variables})
    api.write_json(output/'score.json', accepted_score)
    api.build_vignetting_audit(accepted_dir, merit)
    reason = ('converged_apertures_and_vignetting' if report['numerically_stable']
              else 'completed_max_rounds_valid_and_bounded')
    return finish(reason, True,
                  accepted_value, accepted_score, accepted_variables)
