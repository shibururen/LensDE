# C++ interfaces for lens optimization

This directory defines the interfaces LensDE uses to optimize a lens system from design variables, bounds, and merit settings. The optimizer generates and scores candidate values, reports progress, and returns the best values and their optical score along with the final population. Include `optiland/optimizer.hpp` to load these declarations in the required order. [`optiland/optimizer.hpp`](../optimizer.hpp)

## Optimizer entry points

- Declares `DifferentialEvolution` and its entry points for differential evolution (DE), damped least squares (DLS), and combined search and refinement. [`engine.hpp`](engine.hpp)
- Forward-declares the internal sessions used by the optimizer entry points. The sessions own the population, random state, and archive used during a run. [`session_fwd.hpp`](session_fwd.hpp) · [Session implementation](../../../src/optimizer/README.md)

## Algorithm settings

- Defines population size, generation budgets, mutation and crossover settings, and other controls for a DE run. [`de_options.hpp`](de_options.hpp)
- Defines the DE variants and the choices for guide selection, mating, constraint handling, and topology diversity. [`de_strategies.hpp`](de_strategies.hpp)
- Defines DLS refinement settings such as the iteration limit, damping, and residual model. [`dls_options.hpp`](dls_options.hpp)
- Defines how the hybrid optimizer alternates DE search and DLS refinement. [`hybrid_options.hpp`](hybrid_options.hpp)
- Defines the population, basin, and restart controls for adaptive DE and DLS. [`adaptive_options.hpp`](adaptive_options.hpp)

## Generating and comparing candidates

- Defines the internal variable scaling modes and distance measures used to compare lens designs. [`scaling.hpp`](scaling.hpp)
- Declares conversions between physical variable values and optimizer coordinates, including bounds and step sizes. [`variable_transform.hpp`](variable_transform.hpp)
- Defines how to sample or perturb populations, screen new candidates for trace feasibility, and describe their optical topology. [`generation.hpp`](generation.hpp)
- Stores each candidate's score, constraint violations, and topology information for ranking. [`fitness.hpp`](fitness.hpp)
- Defines how many search basins to retain, their distance and quality settings, and the records for their representative designs. [`basins.hpp`](basins.hpp)

## GPU batch scoring

- Declares the CUDA availability check and the functions that score a population on the GPU. [`cuda_api.hpp`](cuda_api.hpp)
- Defines the batch scores, timings, and interface used by the broker to supply scores to an optimizer population. [`cuda_evaluation.hpp`](cuda_evaluation.hpp)

## Progress and results

- Defines the callback that reports the generation, best score, and best variable values. [`progress.hpp`](progress.hpp)
- Stores the best result, final population, and designs retained from different basins. [`results.hpp`](results.hpp)
- Records time and evaluation counts for phases such as candidate generation, scoring, selection, and refinement. [`timing.hpp`](timing.hpp)
- Defines search events that record population changes, merit, and basin decisions during hybrid optimization. [`trace.hpp`](trace.hpp)
- Records the generations run by each local population, its surviving candidate, and any changes to the basin archive. [`micro_islands.hpp`](micro_islands.hpp)
