# Native command execution

This directory runs the native commands used to trace and optimize lenses. It reads the lens prescription, merit settings, and any supplied design variables or population, then selects the requested operation. It writes scores and optical outputs, along with populations and checkpoints for optimization runs. The executable accepts a single command or serves repeated commands through its standard input and output. [Command entry points](../../include/lensde/cli/README.md)

## Command entry and request state

- Creates a fresh command context for each request in either execution mode. [`command.cpp`](command.cpp)
- Parses quoted arguments in server mode and returns a completion marker and status for each request. [`server_protocol.cpp`](server_protocol.cpp)
- Holds the request's lens, merit configuration, command options, and initial and final scores. [`command_context.hpp`](command_context.hpp)
- Runs input preparation, initial evaluation, optional optimization, and output in order and propagates the status from each stage. [`command_execution.cpp`](command_execution.cpp)
- Declares the internal helpers shared by command preparation, scoring, and output. [`cli_internal.hpp`](cli_internal.hpp)

## Lens, aiming, and merit inputs

- Loads the input lens, sets its asphere root and aperture policies, and dispatches standalone queries. [`command_input.cpp`](command_input.cpp)
- Applies command-line overrides for calibrated paraxial and three-step aiming. [`command_aiming.cpp`](command_aiming.cpp)
- Loads the merit JSON and applies command-line overrides for sampling, targets, constraints, and weights. [`command_merit.cpp`](command_merit.cpp)
- Prepares vignetting settings and runs automatic vignetting when requested. [`command_prepare_vignetting.cpp`](command_prepare_vignetting.cpp)
- Resolves the selected operation, iteration and population limits, scaling mode, and output options. [`command_options.cpp`](command_options.cpp)
- Resolves the project glass database and prepares output paths and directories. [`resources.cpp`](resources.cpp)
- Provides the built-in example lens, variables, and merit defaults, and prints command help. [`demo_system.cpp`](demo_system.cpp)
- Implements the explicit command checks for material scaling and asphere intersection. [`self_tests.cpp`](self_tests.cpp)

## Variables, seed selection, and population scoring

- Reads physical values from a lens and prepares asphere references and bounds for local optimization. [`variable_configuration.cpp`](variable_configuration.cpp)
- Loads and validates design variables, glass choices, and optional initial population rows. It also handles population-only scoring requests. [`optimizer_input.cpp`](optimizer_input.cpp)
- Parses population generation settings for initialization and refresh and reads the supplied refresh seed pool. [`generation_options.cpp`](generation_options.cpp)
- Reads physical variable rows from CSV and writes the final population in the same variable order with 17-digit precision. [`population_csv.cpp`](population_csv.cpp)
- Runs seed selection from a supplied candidate pool and writes the chosen rows and summary. [`command_select_seed_population.cpp`](command_select_seed_population.cpp)
- Computes scaled coordinates, row identities, and active-curvature masks to compare seed rows. [`seed_coordinates.cpp`](seed_coordinates.cpp)
- Loads candidate pools and selects seed rows using the configured activity and diversity rules. [`seed_selection.cpp`](seed_selection.cpp)
- Writes the seed-selection summary, source-row identities, and selection diagnostics. [`seed_selection_output.cpp`](seed_selection_output.cpp)
- Evaluates supplied population rows on CPU while preserving row order across scoring workers. [`population_scoring.cpp`](population_scoring.cpp)
- Writes one CSV row per candidate using cached scores or newly evaluated scores. [`population_score_output.cpp`](population_score_output.cpp)

## Starting the optimizer

- Holds the variables, solver options, and checkpoint state for the current optimization request. [`optimization_command.hpp`](optimization_command.hpp)
- Builds the option objects and dispatches standard DE, hybrid DE–DLS, or adaptive DE–DLS. The result stage also handles standalone DLS. [`optimizer_dispatch.cpp`](optimizer_dispatch.cpp) · [Optimizer implementations](../optimizer/README.md)
- Reads adaptive population and generation limits, constraint options, scaling, and stopping rules. [`adaptive_search_options.cpp`](adaptive_search_options.cpp)
- Reads basin archive settings, including archive capacity, distance, and eligibility rules. [`adaptive_basin_options.cpp`](adaptive_basin_options.cpp)
- Loads avoidance and geometry-memory inputs and reads settings for mutation near stored designs. [`adaptive_memory_options.cpp`](adaptive_memory_options.cpp)
- Reads adaptive material tolerance, refresh generation, and local refinement settings. [`adaptive_refinement_options.cpp`](adaptive_refinement_options.cpp)
- Runs the adaptive optimizer and connects its progress callbacks to trace output and checkpoint validation. [`adaptive_execution.cpp`](adaptive_execution.cpp)
- Prints the adaptive configuration selected for the run. [`adaptive_logging.cpp`](adaptive_logging.cpp)

## Checkpoints and final optical outputs

- Sets checkpoint scoring and acceptance rules, records the initial design as a fallback when it passes validation, and forwards optimizer progress to checkpoint saving. [`optimizer_checkpoint.cpp`](optimizer_checkpoint.cpp)
- Builds the checkpoint lens from the candidate variables, rescores it when required, and writes the system, score, and metadata for accepted candidates. [`checkpoint_save.cpp`](checkpoint_save.cpp)
- Reads associated checkpoint input data and serializes score summaries and state used by subsequent stages. [`checkpoint_state.cpp`](checkpoint_state.cpp)
- Runs standalone or requested final DLS refinement, validates the candidate for publication, and writes the optimizer outputs. It can recover the latest validated result if the final candidate fails the checks. [`optimizer_results.cpp`](optimizer_results.cpp)
- Scores the initial lens and writes the final system and score, with spot, ray-path, layout, and relative illumination outputs according to the command options. [`command_evaluation.cpp`](command_evaluation.cpp)

## Progress records and diagnostics

- Writes the final basin archive as CSV and JSON, including the representative designs and their scores. [`basin_output.cpp`](basin_output.cpp)
- Appends hybrid and adaptive progress events and requested family-transition records to JSON Lines files. [`trace_output.cpp`](trace_output.cpp)
- Provides shared JSON formatting, option-name normalization, and checks for CUDA merit support. [`json_output.cpp`](json_output.cpp)
- Prints merit and optical-health summaries for the results already computed. [`score_console.cpp`](score_console.cpp)
- Prints optimizer and backend timings and counters. [`timing_output.cpp`](timing_output.cpp)
- Writes the requested aiming diagnostic counters, residual summaries, and timings. [`aiming_diagnostics.cpp`](aiming_diagnostics.cpp)
- Exports CSV measurements for the requested chief-ray and edge-ray aiming probes. [`aiming_probes.cpp`](aiming_probes.cpp)
