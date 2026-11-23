# CUDA population scoring modules

This directory scores batches of lens candidates on the GPU. It takes a base lens, candidate variable values, and the material, field, wavelength, and merit settings. It builds a lens for each candidate, traces pupil rays, and returns optical measurements and merit scores in input row order. [Scoring compilation unit](../de_cuda_scorer.cu)

## Shared header

- Brings together the native interfaces for merit evaluation, ray tracing, aperture handling, pupil sampling, and kernel launch settings. It also supplies the shared device surface and solver types, CUDA runtime definitions, and common C++ headers. [`prelude.cuh`](prelude.cuh)

## Scoring stages

| Directory | Responsibility |
| --- | --- |
| `host/` | Prepares one scoring call, manages host and device buffers, launches kernels, and copies scores back to the caller. |
| `configuration/` | Converts lens, variable, merit, material, field, and wavelength inputs into device configuration and arrays. |
| `contracts/` | Defines device records for the lens, configuration, rays, aiming models, calibration tasks, and field statistics. |
| `geometry/` | Computes surface sag, refractive indices, pupil geometry, and first-order optical quantities. |
| `sampling/` | Chooses pupil sample positions, weights, and symmetry mappings. |
| `aiming/` | Builds calibrated aiming models from chief and edge rays and reuses valid starting points across fields and wavelengths. |
| `ray/` | Generates and traces rays through the optical surfaces, applying refraction and aperture checks. |
| `wavefront/` | Builds wavefront references and supplies the calculations used to fit and measure wavefront error. |
| `merit/` | Combines trace coverage, image measurements, and physical constraints into merit contributions. |
| `kernels/` | Executes candidate, aiming, and ray work on the GPU and reduces the results by field and candidate. |
| `launch/` | Schedules the calibrated-model kernels in dependency order. |
| `diagnostics/` | Collects GPU counters and formats diagnostic reports when requested. |

Ray tracing calls the shared device intersection functions for aspheric surfaces. [CUDA asphere intersection](../intersection/README.md)
