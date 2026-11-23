# Lens system, ray, and merit data

This directory defines the C++ data types LensDE uses to represent an optical system and its evaluation settings. The tracer and merit evaluator use these types to record ray paths, optical measurements, and a merit score for the optimizer. The public `optiland/core.hpp` header brings these declarations together. [`optiland/core.hpp`](../core.hpp)

## Optical system and trace inputs

- Defines `LensSystem`, which combines the surfaces, fields, wavelengths, aperture, and ray-aiming settings of one lens design. [`model.hpp`](model.hpp)
- Defines plane, spherical, conic, and aspheric surfaces, along with their material, thickness, and aperture data. [`surfaces.hpp`](surfaces.hpp)
- Defines field coordinates and their vignetting factors, as well as wavelength values and weights. [`fields.hpp`](fields.hpp)
- Defines aperture specifications, ray-aiming modes, and the settings for aiming rays through the stop. [`aiming.hpp`](aiming.hpp)

## Rays and trace results

- Provides three-dimensional vector operations, angle conversions, and numerical constants shared by the optical code. [`numeric.hpp`](numeric.hpp)
- Defines a ray's position, direction, wavelength, intensity, and trace failure status. [`rays.hpp`](rays.hpp)
- Defines weighted pupil samples and stores the traced rays and their histories at each surface. [`trace.hpp`](trace.hpp)

## Optimization inputs and scores

- Defines the parameters the optimizer can change, including their initial values and bounds. [`variables.hpp`](variables.hpp)
- Defines the catalog glass candidates, per-surface choices, and settings used when searching for glass assignments. [`glass.hpp`](glass.hpp)
- Defines individual merit terms, bound constraints, and policies for failed rays. [`merit_operands.hpp`](merit_operands.hpp)
- Collects the ray-sampling settings, optical targets, term weights, and manufacturing limits used in merit evaluation. [`merit_config.hpp`](merit_config.hpp)
- Stores the merit score, optical measurements, trace coverage, constraint errors, and contributions from individual terms. [`score.hpp`](score.hpp)

## Applying variables and sampling the pupil

- Declares the functions that apply a row of candidate values to an optical system and generate pupil samples for evaluation. [`operations.hpp`](operations.hpp) · [Implementation](../../../src/core.cpp)
