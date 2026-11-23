# Building the initial population of lens designs

This package samples candidate lens designs from the optical system, design variable bounds, target dimensions, and a random seed. Each row contains the variable values for one candidate. The package selects candidates using backend trace scores and population diversity, writes the population to CSV, and reads scores back by candidate index. The engine calls these functions through `populations.py`. [Population entry points](../populations.py)

## Design variables and sampling limits

- Builds the variable list used for initial search, including the curvature ranges and the aspheric terms enabled at each stage. [`variables.py`](variables.py)
- Classifies optical spaces and variables, including glass thickness, air gaps, and the back focal length variable. [`domains.py`](domains.py)
- Converts and scales variable values for distance comparisons, applies bounds to mutations, and identifies duplicate rows. [`coordinates.py`](coordinates.py)
- Samples and adjusts lens thicknesses and air gaps within the total track length and back focal length budgets. [`thickness.py`](thickness.py)
- Plans and samples refractive index and Abbe number values using catalog glasses that cover the required wavelengths. [`materials.py`](materials.py)

## Candidate generation

- Generates candidates using first-order optical targets and the available thickness budget. [`first_order.py`](first_order.py)
- Builds the hybrid population by preparing starting values and sampling the design variables with the configured generation mode. [`hybrid_generation.py`](hybrid_generation.py)
- Samples across defined ranges of back focal length and distributes candidates across different surface shapes. [`mechanical.py`](mechanical.py)
- Generates nearby candidates by perturbing the variable values of an existing lens design. [`local.py`](local.py)
- Resolves the seeding strategy and calculates population and basin counts from the effective number of design variables and the run settings. [`sizing.py`](sizing.py)

## Candidate selection and CSV files

- Chooses representative candidates using trace feasibility, ranking, and distance between designs. [`selection.py`](selection.py)
- Measures how much of each variable's range the candidate pool covers and checks whether it meets diversity thresholds. [`diversity.py`](diversity.py)
- Fills gaps in coverage by adding eligible candidates that vary otherwise underrepresented design variables. [`coverage.py`](coverage.py)
- Reads and writes candidate rows without changing their order and maps merit scores back to row indices. [`io.py`](io.py)
