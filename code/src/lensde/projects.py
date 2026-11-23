"""Shared project preparation for CLI, experiments, and the workbench."""
from __future__ import annotations
import copy
from lensde.config import BenchmarkCatalog
from lensde.engine import core,state
from lensde.resources import ProjectPaths
from lensde.seeds import BENCHMARK_PROFILES,configure_seed_only


def localize(project):
    result=copy.deepcopy(project)
    for location,target in ProjectPaths(state.PROJECT_ROOT).material_sources(result).items():
        container=result
        for part in location[:-1]:container=container[part]
        container[location[-1]]=str(target)
    return result


def normalize(project):
    project=copy.deepcopy(project)
    if isinstance(project.get('variables'),dict):project['variables']=project['variables']['variables']
    return core.normalise_project(localize(project))


def benchmark(task):
    saved=BenchmarkCatalog(state.PROJECT_ROOT).load(task)
    project=normalize(saved.scientific_configuration)
    optical={key:copy.deepcopy(project[key]) for key in ('system','variables','merit')}
    profile=BENCHMARK_PROFILES[task]
    project=normalize(configure_seed_only(project,profile))
    if any(project[key]!=optical[key] for key in optical):
        raise RuntimeError('Seed-only preparation changed the saved optical inputs')
    return {'project':project,'metadata':{'benchmark_id':task,'source_kind':'saved_formal_inputs',
            'seed_profile':profile}}
