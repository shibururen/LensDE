# Device records for CUDA scoring

This directory defines the records that host code fills before a CUDA scoring
batch and that the kernels use to trace rays and calculate field statistics.

## Scoring inputs

- [`constants.cuh`](constants.cuh) defines surface type codes, pupil sampling
  modes, array limits, and numerical tolerances.
- [`optical_records.cuh`](optical_records.cuh) defines field coordinates and
  vignetting factors, wavelength weights, optimization variables, glass
  candidates, and proximity constraints.
- [`device_config.cuh`](device_config.cuh) collects batch dimensions, pointers
  to device arrays, and settings for aiming, sampling, and merit evaluation.

## Rays and aiming

- [`ray_record.cuh`](ray_record.cuh) stores the ray position, direction, optical
  path, wavelength, weight, and trace status. It includes helpers for handling
  aperture misses at secondary wavelengths.
- [`calibrated_model.cuh`](calibrated_model.cuh) stores the chief-ray seed, its
  coordinates at the aperture stop, the fit error, and four pupil-edge anchors
  used by calibrated aiming.
- [`calibration_tasks.cuh`](calibration_tasks.cuh) defines the inputs and results
  for sampling the stop, solving chief rays, and testing trial aiming seeds.

## Field statistics

- [`spot_wavefront_records.cuh`](spot_wavefront_records.cuh) defines ray samples
  with weights and validity flags, and field summaries for RMS spot radius,
  RMS wavefront error, and pupil coverage.

The [scoring compilation unit](../../de_cuda_scorer.cu) includes these headers.
