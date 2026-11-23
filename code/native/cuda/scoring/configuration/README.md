# Host configuration for CUDA scoring

This directory converts a lens system, optimization variables, and merit settings
into a device configuration and ordered arrays of surfaces, materials, fields,
and wavelengths for CUDA population scoring.

## Scoring settings

- [`supported_merit.cuh`](supported_merit.cuh) checks whether the CUDA scorer
  supports the requested ray aiming, pupil sampling, merit operands, and bounds.
- [`config_builder.cuh`](config_builder.cuh) sets the device counts, aiming and
  sampling options, trace gates, constraints, and merit weights.
- [`merit_operands.cuh`](merit_operands.cuh) maps active operands to device
  flags, targets, bounds, and weights.
- [`asphere_policy.cuh`](asphere_policy.cuh) selects the asphere root and
  slope-domain settings. [`runtime_options.cuh`](runtime_options.cuh) reads
  environment overrides and controls optional CUDA event timing.

## Upload records

- [`variable_kinds.cuh`](variable_kinds.cuh) maps host surface and variable
  types to device codes.
- [`material_upload.cuh`](material_upload.cuh) packs surfaces and variables,
  evaluates refractive indices in wavelength order, and packs glass candidates
  with per-variable offsets.
- [`field_upload.cuh`](field_upload.cuh) packs fields with their vignetting
  factors, wavelengths with their weights, and glass proximity constraints
  with offsets into the point array.

The [scoring compilation unit](../../de_cuda_scorer.cu) includes these headers.
Host code prepares the [device records](../contracts/README.md) before uploading
them to the GPU.
