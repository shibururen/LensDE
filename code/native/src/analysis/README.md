# Optical measurements and merit scoring

This directory measures a lens prescription using its fields, wavelengths, pupil samples, and merit settings. It computes first-order properties, spot and wavefront errors, trace coverage, and physical constraints. The merit evaluator combines those measurements into a score and produces residuals for damped least squares (DLS). The directory also provides standalone relative illumination, clear-aperture updates, and automatic vignetting. [Public analysis API](../../include/optiland/analysis.hpp) · [Merit entry point](../analysis.cpp)

## First-order properties and physical measurements

- Computes effective focal length, back focal length, paraxial focus and color, and minimum edge thickness. [`paraxial_metrics.cpp`](paraxial_metrics.cpp)
- Measures on-axis working F-number and image-space numerical aperture from traced marginal rays. [`working_f_number.cpp`](working_f_number.cpp)
- Uses chief rays to compute reference image heights, distortion, chief ray angle, lateral color, and path metrics. [`chief_metrics.cpp`](chief_metrics.cpp)
- Measures aberrations and focus shifts from traced rays at selected field and pupil positions. [`finite_aberrations.cpp`](finite_aberrations.cpp)
- Measures thickness, sag, diameter, and surface tangent angles for geometry bounds and manufacturing constraints. [`geometry_constraints.cpp`](geometry_constraints.cpp)
- Measures glass proximity in refractive index and Abbe number and updates mechanical apertures for grouped surfaces. [`glass_constraints.cpp`](glass_constraints.cpp)
- Combines trace health, first-order errors, and physical constraints into topology-potential metrics for population scoring. [`topology_potential.cpp`](topology_potential.cpp)

## Pupil sampling, spot size, and trace health

- Builds the pupil sample sets and coordinate transforms used by spot, wavefront, and ray-path checks. [`pupil_sampling.cpp`](pupil_sampling.cpp)
- Classifies trace failures and measures the condition of the ray path through the stop and image space. [`ray_health.cpp`](ray_health.cpp)
- Traces the pupil sets for monochromatic and polychromatic spot measurements, reusing tracing and aiming contexts when supplied. [`spot_evaluation.cpp`](spot_evaluation.cpp)
- Adds selected pupil-edge and spot-tail guard rays to cover cases that the main sample set misses. [`spot_guards.cpp`](spot_guards.cpp)
- Reduces traced rays into weighted centroids, RMS and maximum spot radii, and coverage statistics. [`spot_statistics.cpp`](spot_statistics.cpp)

## Wavefront error

- Exposes the field and system wavefront statistics used in scoring and reporting. [`wavefront_evaluation.cpp`](wavefront_evaluation.cpp)
- Builds the object-space and primary-wavelength reference geometry and converts the resulting optical path differences into wavefront summaries. [`wavefront_reference.cpp`](wavefront_reference.cpp)
- Collects pupil samples for centroid-referenced wavefront fits and selects the reference-specific evaluation for each field. [`wavefront_sampling.cpp`](wavefront_sampling.cpp)
- Fits centroid-referenced wavefront models using direction cosines and estimates projected pupil area. [`wavefront_fit.cpp`](wavefront_fit.cpp)
- Accumulates per-ray optical path differences against the reference sphere for one field and wavelength. [`wavefront_accumulation.cpp`](wavefront_accumulation.cpp)

## Merit evaluation and optimizer residuals

- Holds the score, field measurements, and contribution totals for one evaluation. [`merit_evaluation.hpp`](merit_evaluation.hpp)
- Runs measurement, trace checks, contribution evaluation, and finalization in order. [`merit_pipeline.cpp`](merit_pipeline.cpp)
- Collects the first-order, spot, wavefront, chief ray, and trace-health measurements needed by the configured score. [`merit_measurement.cpp`](merit_measurement.cpp)
- Applies early failure rules for invalid image geometry, structural failures, or unacceptable trace results. [`merit_failures.cpp`](merit_failures.cpp)
- Applies hard trace requirements and adds penalties for coverage, chief ray progress, and ray failures. [`merit_trace.cpp`](merit_trace.cpp)
- Resolves merit operand names, identifies their required measurements, and evaluates target and bound penalties. [`merit_operands.cpp`](merit_operands.cpp)
- Evaluates the configured operand list at the system, field, or wavelength level and records each contribution. [`merit_operands_evaluation.cpp`](merit_operands_evaluation.cpp)
- Builds contributions from targets and weights when the input merit configuration supplies no operands. [`merit_legacy.cpp`](merit_legacy.cpp)
- Adds field illumination floors and vignetting penalties to the merit score. [`merit_illumination.cpp`](merit_illumination.cpp)
- Adds geometry penalties for small edge separation and changes in pupil ray order. [`merit_geometry.cpp`](merit_geometry.cpp)
- Adds center and edge thickness penalties and the configured penalty for tightly spaced triplets. [`merit_manufacturing.cpp`](merit_manufacturing.cpp)
- Adds surface-shape, explicit bound, and glass proximity constraints. [`merit_surface.cpp`](merit_surface.cpp)
- Classifies merit contributions and supplies their reference scales, tolerances, and weights. [`merit_weights.cpp`](merit_weights.cpp)
- Combines contribution groups for the adaptive-quality and unified trace gate merit models. [`merit_aggregation.cpp`](merit_aggregation.cpp)
- Applies the selected merit model, handles invalid scores, and attaches DLS residuals. [`merit_finalization.cpp`](merit_finalization.cpp)
- Builds the ordered ray-spot and guard-ray residual arrays consumed by DLS. [`dls_residuals.cpp`](dls_residuals.cpp)

## Relative illumination and aperture updates

- Integrates projected pupil samples along a field scan and normalizes the standalone relative illumination result. [`illumination.cpp`](illumination.cpp)
- Builds clear-aperture envelopes from traced rays and updates stop radii where the aperture settings permit it. [`aperture_update.cpp`](aperture_update.cpp)

## Automatic vignetting

The solver finds the part of the pupil that passes the lens apertures and calculates the center shifts (FVC) and extent reductions (FVD) for each field.

- Coordinates the field and wavelength solves and returns the factors after final validation. [`vignetting.cpp`](vignetting.cpp)
- Selects wavelengths and builds the reference-pupil and fallback lens variants used by the solve. [`vignetting_config.cpp`](vignetting_config.cpp)
- Prepares the per-field and per-wavelength tracing contexts and the physical apertures to check. [`vignetting_solver.cpp`](vignetting_solver.cpp)
- Generates and corrects rays for requested stop-pupil coordinates. [`vignetting_aiming.cpp`](vignetting_aiming.cpp)
- Tests whether the same pupil point passes the apertures at the selected wavelengths and follows a connected feasible branch. [`vignetting_branch.cpp`](vignetting_branch.cpp)
- Traces batches of pupil targets and preserves their indices when distributing the work to parallel workers. [`vignetting_batch.cpp`](vignetting_batch.cpp)
- Finds the centroid of a connected feasible pupil region to start the boundary searches. [`vignetting_centroid.cpp`](vignetting_centroid.cpp)
- Finds and refines the feasible interval along one horizontal or vertical pupil axis. [`vignetting_axis.cpp`](vignetting_axis.cpp)
- Combines the axis intervals around a shared pupil center and validates the resulting factors against the final aperture and aiming settings. [`vignetting_contract.cpp`](vignetting_contract.cpp)

## Shared analysis interfaces

- Declares the internal measurement, sampling, ray-checking, and vignetting helpers used across these files. [`detail.hpp`](detail.hpp)
