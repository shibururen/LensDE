# Preparing project settings for ray tracing and optimization

This package fills in missing settings and reconciles the optical system, design variables, merit settings, and run options before ray tracing or optimization. For example, it removes continuous refractive index and Abbe number variables from surfaces assigned catalog glass. The `finish_project()` entry point applies the settings in order and returns the updated project dictionary. [`configuration.py`](../configuration.py)

## Optical system and merit settings

- Prepares aperture and glass-search settings, fills in surface material properties, and filters design variables to match the material model. [`materials.py`](materials.py)
- Sets the merit function's image-quality, illumination, and vignetting criteria, along with ray sampling and manufacturing constraints. [`merit.py`](merit.py)
- Sets ray-aiming methods and iteration limits, and specifies the trace coverage required when screening initial designs. [`aiming.py`](aiming.py)

## Backend and search settings

- Fills in backend selection and timeout settings, bound overrides, and final local refinement options. [`runtime.py`](runtime.py)
- Sets the bounds and material priors for sampling the initial population and the rules for screening and replenishing it. [`seeding.py`](seeding.py)
- Resolves the initial search strategy and sets how candidates are grouped into basins and how the optimizer adapts during the search. [`search_policy.py`](search_policy.py)
- Sets the budgets and restart options for global exploration, candidate screening, and refinement. [`funnel.py`](funnel.py)

## Final adjustments

- Adds vignetting variables where required, synchronizes the aperture stop, applies variable bounds, and applies the random-seed settings. [`finalization.py`](finalization.py)
