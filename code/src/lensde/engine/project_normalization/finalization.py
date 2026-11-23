"""Commit cross-domain optical synchronization and the final seed-protocol application."""
from __future__ import annotations
from lensde.seeds import protocol as seed_protocol
from .. import geometry as _lensde_geometry

def finalize_optical_and_seed_contracts(
    project: dict,
    system: dict,
    merit: dict,
    options: dict,
    filtered_variables: list[dict],
    *,
    ensure_conic_coefficient_merit_bounds,
    ensure_vignetting_variables_for_system,
    sync_unified_ray_aiming_options,
    vignetting_minimum_relative_illumination,
    vignetting_optimization_enabled,
) -> None:
    """Commit vignetting variables and aiming/bounds updates, then apply the seed protocol last."""
    options['vignetting_optimization_enabled'] = vignetting_optimization_enabled(merit)
    options['vignetting_minimum_relative_illumination'] = vignetting_minimum_relative_illumination(merit)
    project['variables'] = ensure_vignetting_variables_for_system(system, filtered_variables, merit)
    sync_unified_ray_aiming_options(options)
    _lensde_geometry.sync_stop_aperture(project)
    ensure_conic_coefficient_merit_bounds(project)
    _lensde_geometry.apply_variable_bounds(project)
    seed_protocol.apply_options(options)
