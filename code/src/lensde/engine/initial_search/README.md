# Searching for initial lens structures

This package runs an initial structure search from an optical system, design variables, merit settings, and targets such as effective focal length, total track length, and back focal length. It builds and screens a population of candidates, calls the backend to explore lens structures, and sends selected candidates to the configured glass selection and refinement stages. It returns the resulting optical system, merit scores, and links to run files. [Search entry points](../orchestration.py)

## Search order and shared run data

- Calls the six phases in order and returns as soon as a phase produces the final result. [`runner.py`](runner.py)
- Defines the data passed between phases, including optical targets, variable lists, candidate files, and stage results. [`contracts.py`](contracts.py)

## Preparing and screening the starting population

- Sets up the optical inputs, merit function, variable bounds, and seeding options for the search. [`preparation.py`](preparation.py)
- Chooses the seeding strategy, builds or loads the starting population, and returns the population files when the run is configured to stop before optimization. [`seed_preparation.py`](seed_preparation.py)
- Generates and replenishes candidate rows and adjusts them to fit the total track length budget. [`seed_generation.py`](seed_generation.py)
- Calls the CPU backend to score candidates and filters them by ray trace coverage. [`seed_scoring.py`](seed_scoring.py)
- Writes CSV files for the starting pool and variable ranges while preserving the row and column order. [`seed_io.py`](seed_io.py)

## Exploration and candidate selection

- Sets up the exploration budgets, restart policy, and limits for chief ray angle. [`funnel_configuration.py`](funnel_configuration.py)
- Runs or resumes the global search and passes its results and chief ray angle settings to the later stages. [`exploration.py`](exploration.py)
- Runs the restart cycles, avoids re-exploring recorded geometries, and merges their candidate archives. [`restart_exploration.py`](restart_exploration.py)
- Loads archived candidates and their stage-specific variables and computes optical topology descriptors used in comparisons. [`stage_context.py`](stage_context.py)
- Ranks the search basins and selects representative designs based on merit and differences in optical layout. [`basin_selection.py`](basin_selection.py)

## Backend stages, refinement, and results

- Builds backend command lines from the stage input files, optimizer options, ray sampling, and aiming settings. [`stage_commands.py`](stage_commands.py)
- Runs backend stages, passes vignetting settings between stages, and reuses completed results when their recorded inputs match. [`execution.py`](execution.py)
- Passes selected candidates to the configured material and refinement workflow and processes the returned designs. [`material_handoff.py`](material_handoff.py)
- Writes summaries, command lines, and run metadata and collects the final optical system and merit scores. [`publication.py`](publication.py)
