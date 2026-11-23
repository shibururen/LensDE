"""Public initial-search entry points; stage implementations live in initial_search/."""
from __future__ import annotations
import copy
import time
from pathlib import Path
from lensde.runtime import processes as subprocess
from . import state
from . import configuration as _lensde_configuration
from . import geometry as _lensde_geometry
from . import materials as _lensde_materials
from . import provenance as _lensde_provenance

def run_hybrid_initial_structure_search(project: dict, run_dir: Path, run_merit: dict, target_efl: float, target_ttl: float, target_bfl: float) -> dict:
    """Run the retained paper workflow through explicit preparation, search and publication phases."""
    from .initial_search.runner import run_search
    return run_search(project, run_dir, run_merit, target_efl, target_ttl, target_bfl)

def run_initial_structure_search(project: dict) -> dict:
    project = copy.deepcopy(project)
    _lensde_configuration.sync_unified_ray_aiming_options(project.setdefault('options', {}))
    _lensde_geometry.sync_variable_initials(project)
    stamp = time.strftime('%Y%m%d_%H%M%S')
    run_dir = subprocess.run_directory(state.WORK_DIR / f'run_{stamp}_{int(time.time() * 1000) % 1000:03d}')
    run_dir.mkdir(parents=True, exist_ok=True)
    options = project.get('options', {})
    run_merit = _lensde_geometry.auto_merit_config(project)
    target_efl = _lensde_configuration.finite_float(run_merit.get('target_efl'), 0.0)
    target_ttl = _lensde_configuration.finite_float(run_merit.get('target_ttl'), 0.0)
    target_bfl = _lensde_configuration.finite_float(run_merit.get('target_bfl'), 0.0)
    if abs(target_efl) <= state.TARGET_EFL_EPS:
        _lensde_provenance.write_json(run_dir / 'merit.json', run_merit)
        return _lensde_materials.collect_run_result(run_dir, run_merit, 8, '', '初始结构搜索需要设置非零 target_efl。')
    return run_hybrid_initial_structure_search(project, run_dir, run_merit, target_efl, target_ttl, target_bfl)
