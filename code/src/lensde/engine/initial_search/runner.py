"""Execute the retained paper initial-search phases in their original order."""
from __future__ import annotations
from .contracts import SearchInputs, SearchState
from . import preparation, seed_preparation, funnel_configuration, exploration, material_handoff, publication

def run_search(project, run_dir, run_merit, target_efl, target_ttl, target_bfl):
    """Share explicit phase state while preserving early-return and deadline behavior."""
    current = SearchState(SearchInputs(project=project, run_dir=run_dir, run_merit=run_merit,
                                      target_efl=target_efl, target_ttl=target_ttl, target_bfl=target_bfl))
    result = preparation.run_phase(current)
    if result.finished:
        return result.value
    result = seed_preparation.run_phase(current)
    if result.finished:
        return result.value
    result = funnel_configuration.run_phase(current)
    if result.finished:
        return result.value
    result = exploration.run_phase(current)
    if result.finished:
        return result.value
    result = material_handoff.run_phase(current)
    if result.finished:
        return result.value
    result = publication.run_phase(current)
    if not result.finished:
        raise RuntimeError('Initial-search publication did not produce a result')
    return result.value
