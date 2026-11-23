# Python engine for ray tracing and lens optimization

This package writes the optical system, design variables, and merit settings to JSON files and calls the C++ or CUDA backend to trace or optimize the lens. It reads the resulting optical system and merit scores and provides links to output files such as the spot data. For an initial structure search, it also samples and filters a population of lens designs and passes selected candidates between optimization stages.

## Engine entry points and shared state

- Exposes the engine functions shared by the command line and workbench. [`core.py`](core.py)
- Holds shared paths, constants, and caches, including the location of the material catalog. [`state.py`](state.py)

## Project settings and optical inputs

- Fills in missing settings and reconciles the optical system, variables, merit settings, and run options. [`configuration.py`](configuration.py) · [Project normalization](project_normalization/README.md)
- Defines the default project and presets for search modes and image-quality metrics. [`defaults.py`](defaults.py)
- Builds merit operands for optical and manufacturing constraints and synchronizes variable values with the optical system. [`geometry.py`](geometry.py)
- Resamples field positions for optimization and validation and transfers vignetting data between field grids. [`fields.py`](fields.py)
- Prepares clear apertures and calls the CPU backend to recompute field vignetting factors. [`apertures.py`](apertures.py)
- Looks up catalog glasses, checks wavelength coverage, prepares the glass search, and collects run results. [`materials.py`](materials.py)

## Initial populations and structure search

- Sets up aspheric design variables, samples initial coefficients, and prepares restart populations. [`initialization.py`](initialization.py)
- Provides the entry points for sampling, filtering, and saving populations of candidate designs. [`populations.py`](populations.py) · [Population algorithms](seed_population/README.md)
- Starts an initial structure search and passes control to the phase runner. [`orchestration.py`](orchestration.py) · [Search stages](initial_search/README.md)

## Backend execution and output files

- Writes `input_system.json`, `variables.json`, and `merit.json`, then calls the backend to trace or optimize the optical system. [`tracing.py`](tracing.py)
- Adapts JADE and local population controls in ordinary DE to native optimizer arguments. [Ordinary DE](ordinary_de.py) · [Local population](../population/README.md)
- Builds backend command arguments and maps candidate values when the active variables change between stages. [`commands.py`](commands.py)
- Runs backend stages, captures their logs, and enforces the remaining time budget. [`execution.py`](execution.py)
- Records the settings, commands, and files used in a run and exports lens prescriptions in CODE V and Zemax formats. [`provenance.py`](provenance.py)

## Asphere refinement after material selection

- Defines the variables to keep fixed and the criteria for scoring and accepting refined designs. [`asphere_contracts.py`](asphere_contracts.py)
- Builds and refreshes candidate populations for asphere refinement. [`asphere_population.py`](asphere_population.py)
- Runs the refinement stages and rechecks promising designs before accepting them. [`asphere_workflow.py`](asphere_workflow.py)
