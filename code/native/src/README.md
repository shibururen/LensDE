# C++ implementation for tracing and lens optimization

This directory contains the host-side C++ code for the CPU optimizer, CUDA optimizer, and multi-island material search. The optimizer loads the optical system, design variables, and merit settings, then writes traced rays, merit scores, and optimized designs. The material-search broker can combine candidates from several search populations into one GPU scoring batch.

## Executable entry points and command arguments

- Starts the optimizer executable and selects single-command or persistent command mode. [`main.cpp`](main.cpp) · [Command pipeline](cli/README.md)
- Starts `lensde_multi_island.exe`, the broker used by the material-search workflow. [`multi_island_experiment_main.cpp`](multi_island_experiment_main.cpp) · [Broker application](broker/README.md)
- Reads argument values and converts option names into the modes used by the native commands and broker. [`cli_arguments.cpp`](cli_arguments.cpp)

## Optical inputs, evaluation, and outputs

- Reads merit settings from JSON and applies each group of settings to the native configuration. [`fileio.cpp`](fileio.cpp) · [System and settings readers](io/README.md)
- Calculates material dispersion, applies design variables to an optical system, and generates pupil samples. [`core.cpp`](core.cpp)
- Sets the aperture policy for a scoring run and calls the merit evaluator for the supplied lens. [`analysis.cpp`](analysis.cpp) · [Merit evaluation](analysis/README.md) · [Ray tracing and aiming](raytrace/README.md)
- Samples rear interval thicknesses, scores each focus position, and returns the best result. [`solves.cpp`](solves.cpp)
- Writes lens layouts as SVG and exports ray paths, spot data, and relative illumination as CSV. [`visualization.cpp`](visualization.cpp)

## Optimization and GPU scoring

- Starts an adaptive differential evolution and damped least-squares session for one optimization run. [`optimizer.cpp`](optimizer.cpp) · [Optimizer sessions](optimizer/README.md)
- Stores the evaluation certificate and scoring context separately for each host thread. [`evaluation_context.cpp`](evaluation_context.cpp)
- Combines candidate requests from multiple search populations into CUDA scoring batches and returns each population's own scores. [`population_broker.cpp`](population_broker.cpp)
- Reads and validates CUDA thread-block sizes for general scoring and calibrated ray aiming. [`cuda_launch_config.cpp`](cuda_launch_config.cpp)
