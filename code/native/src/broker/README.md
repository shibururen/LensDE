# Multi-island material search

This directory runs the multi-island material-search executable. It reads a TSV job table with each island's lens prescription, design variables, merit settings, seed population, search budget, and output paths. Each island runs its own optimizer. In batched mode the islands run concurrently and share a CUDA evaluator that combines scoring requests and returns scores to the originating island. The executable writes each island's final population, merit scores, selected lens, and run summary. [Executable entry point](../multi_island_experiment_main.cpp)

## Island job inputs

- Reads the TSV rows, including paths, seeds, generation limits, and optional material or trace settings. [`job_table.cpp`](job_table.cpp)
- Defines the job fields, loaded optical inputs, and the result or error recorded for each island. [`jobs.hpp`](jobs.hpp)
- Loads each lens, merit configuration, design variables, optional glass-search settings, and seed population. It checks material wavelength coverage and population dimensions before the island starts. [`job_loading.cpp`](job_loading.cpp)
- Updates asphere coefficient bounds in the post-asphere stages to preserve edge-sag limits at the current reference radius. [`post_asphere_scaling.hpp`](post_asphere_scaling.hpp)

## Island execution and shared scoring

- Loads the job table, creates the shared evaluator when batching is enabled, and starts execution and result publication. In batched mode it takes the common merit configuration from the first island, so the input jobs must use the same merit settings. [`application.cpp`](application.cpp)
- Reads execution flags and configures the asphere root policy, CUDA aiming setup, and ray task layout before the islands run. [`cli.cpp`](cli.cpp)
- Builds optimizer options for the requested search stage using each island's seed, population size, generation budget, variable scaling, and trace requirements. [`stage_options.cpp`](stage_options.cpp)
- Runs the island optimizers sequentially or in parallel, writes their progress timelines, and records results in job-table order. [`island_execution.cpp`](island_execution.cpp)
- Declares the multi-island executable entry point. [`application.hpp`](application.hpp)
- Declares the private interfaces for job loading, stage setup, execution, and output. [`interfaces.hpp`](interfaces.hpp)

The shared evaluator collects requests from the island optimizers, combines compatible candidate rows into GPU batches, and maps the returned scores to their original rows. [Shared CUDA population evaluator](../population_broker.cpp)

## Result files and diagnostics

- Writes each island's population and merits in matching row order. It applies the selected variables and aperture settings to the returned lens, writes the system and score, and reports batch statistics when requested. [`result_output.cpp`](result_output.cpp)
- Runs a requested asphere-intersection fixture and reports the calculated intersections against the expected results. [`asphere_oracle.cpp`](asphere_oracle.cpp)
