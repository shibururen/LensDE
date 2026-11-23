"""Adapt ordinary DE controls to retained native solvers and local seed helpers.

This module only serves optimizer_mode=de. It does not change the paper workflow,
whose stages own their initialization, transferred populations and JADE variants.
Legacy projects without de_algorithm keep their classical DE behavior.
"""
from __future__ import annotations

from argparse import Namespace
import copy
from pathlib import Path

from lensde.population import build_population, write_population_csv
from lensde.population.local import clamp_row
from .seed_population.sizing import population_sizing_variable_count


def append_jade_args(args: list[str], options: dict, scaling: str) -> None:
    """Run fixed-population JADE in one native epoch without DLS or refresh.

    JADE uses the C++ adaptive entry point also used by the paper, with explicit
    controls preventing a saved hybrid/polish recipe from entering ordinary DE.
    F/CR sampling centers, adaptation and archive capacity follow Supplement S2.3.
    """
    multiplier = max(1, int(options.get('population', 15)))
    generations = max(1, int(options.get('generations', 24)))
    args.extend([
        '--adaptive-hybrid-de-dls', '--popsize', str(multiplier),
        '--gen', str(generations), '--hybrid-outer', '1',
        '--hybrid-de-gen', str(generations), '--hybrid-refresh', '0',
        '--hybrid-local-iter', '0', '--no-polish', '--basin-disable',
        '--adaptive-no-topology-summary', '--adaptive-soft-first-order',
        '--de-variant', 'jade_pbest_archive',
        '--constraint-handling', 'scalar_merit',
        '--adaptive-search-purpose', 'ordinary_de',
        '--optimizer-internal-scaling', scaling,
        '--pbest-scope', 'global', '--pbest-fraction', '0.1',
        '--de-mating-scope', 'global', '--hybrid-no-inject-best',
        '--archive-size-ratio', '1', '--adaptive-mu-f', '0.5',
        '--adaptive-mu-cr', '0.8', '--adaptive-rate', '0.1',
        '--seed', str(int(options.get('seed', 20260515))),
    ])
    progress = int(options.get('progress_every', 0) or 0)
    if progress > 0:
        args.extend(['--progress-every', str(progress)])


def append_population_args(args: list[str], options: dict, system: dict,
                           variables: list[dict], run_dir: Path) -> None:
    """Supply local variants or retain native initialization for ordinary DE.

    The native solvers put the current prescription in row zero, then inject
    seed-file rows starting at row one. Write exactly N-1 distinct variants so
    the current prescription is neither duplicated nor a random tail left over.
    Bounds sampling continues through the existing native LHS implementation.
    """
    initialization = options.get('de_population_init', 'saved')
    if initialization not in {'saved', 'bounds', 'local'}:
        raise ValueError('Unsupported ordinary DE population initialization')
    if initialization == 'saved':
        return
    # The initial-generation flag is appended by the caller. An explicit choice
    # must replace it, including an imported physics-constrained configuration.
    index = args.index('--initial-generation-mode')
    args[index + 1] = 'random'
    if initialization == 'bounds':
        return
    scale = options.get('de_local_scale', 'medium')
    if scale not in {'tight', 'medium', 'wide'}:
        raise ValueError('Unsupported ordinary DE local population scale')
    if not variables:
        raise ValueError('DE requires at least one optimization variable')
    count = max(5, max(1, int(options.get('population', 15))) *
                population_sizing_variable_count(variables))
    local_variables = copy.deepcopy(variables)
    parent = clamp_row([row['initial'] for row in local_variables], local_variables)
    for row, value in zip(local_variables, parent):
        row['initial'] = value
    rows = build_population(
        system=system, variables_doc={'variables': local_variables}, glasses=[],
        material_mode='continuous', population=count,
        seed=int(options.get('seed', 20260515)), scale=scale, args=Namespace(),
    )
    if len(rows) != count:
        raise ValueError('Local DE initialization could not construct enough distinct variants; '
                         'reduce the population multiplier or adjust variable bounds')
    path = run_dir / 'ordinary_initial_population.csv'
    write_population_csv(path, rows[1:])
    args.extend(['--initial-population', str(path)])
