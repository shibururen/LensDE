# Headers for lens models, ray tracing, and optimization

This directory defines how the LensDE C++ and CUDA sources represent lens designs and pass them to ray tracing, merit evaluation, and optimization. The `optiland/` include path comes from the native optical engine prototype. [Naming and upstream attribution](../README.md)

## Optical model, tracing, and evaluation

- Collects the types for optical systems, surfaces, fields, rays, glasses, design variables, merit settings, and scores. [`core.hpp`](core.hpp) · [Optical data types](core/README.md)
- Declares sequential ray tracing and the sessions that aim rays through the aperture stop. [`raytrace.hpp`](raytrace.hpp)
- Declares optical measurements such as spot size and wavefront error, plus merit function evaluation. [`analysis.hpp`](analysis.hpp)
- Declares the quick-focus solve, which searches for the best image-plane position within a given range. [`solves.hpp`](solves.hpp)

## Ray aiming and pupil sampling

- Defines the numerical slope limits shared by the CPU and CUDA even-asphere intersection solvers. [`asphere_domain_contract.hpp`](asphere_domain_contract.hpp)
- Predicts a starting point for CPU pupil-edge aiming at a vignetted field from previously solved fields. [`cpu_vignetted_continuation.hpp`](cpu_vignetted_continuation.hpp)
- Solves for the two transverse coordinates of a CPU aiming ray using its error at the aperture stop. [`cpu_xy_continuation_solve.hpp`](cpu_xy_continuation_solve.hpp)
- Chooses pupil sampling patterns for a fixed ray budget on CPU and CUDA. [`fixed_budget_sampling.hpp`](fixed_budget_sampling.hpp)
- Checks whether pupil sample positions are distinct before the tracer groups equivalent rays. [`pupil_distinct.hpp`](pupil_distinct.hpp)
- Provides shared CPU and CUDA helpers for keeping wavelength-specific pupil-edge estimates within the first-surface aperture. [`spectral_aperture.hpp`](spectral_aperture.hpp)

## Optimization and GPU evaluation

- Collects the options, progress and result types, and entry points for differential evolution (DE) and damped least squares (DLS). [`optimizer.hpp`](optimizer.hpp) · [Optimizer interfaces](optimizer/README.md)
- Declares the CUDA thread-block sizes used by general scoring and calibrated ray aiming. [`cuda_launch_config.hpp`](cuda_launch_config.hpp)
- Defines the coverage and merit bounds used to decide when a candidate evaluation can end early. [`evaluation_certificate.hpp`](evaluation_certificate.hpp)
- Groups candidates from separate optimization populations into shared CUDA scoring batches. [`population_broker.hpp`](population_broker.hpp)

## Commands, input files, and results

- Declares the argument parsing and option conversion functions used by `lensde::cli`. [`cli_arguments.hpp`](cli_arguments.hpp)
- Declares readers for systems, variables, merit settings, and glass catalogs, and writers for system and score JSON. [`fileio.hpp`](fileio.hpp)
- Declares writers for lens layout SVG, spot and ray-trace CSV, and relative illumination CSV. [`visualization.hpp`](visualization.hpp)
