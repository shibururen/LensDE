# Ray generation and tracing for CUDA scoring

This directory launches rays from candidate surfaces, field coordinates,
wavelengths, and pupil samples. It traces them to the stop or image and
records their positions, optical paths, aperture hits, and failure states.

## Ray generation

- [`paraxial_generation.cuh`](paraxial_generation.cuh) sets launch positions and
  directions from the field and pupil coordinates. It also chooses the
  aperture radius used for clipping.
- [`calibrated_generation.cuh`](calibrated_generation.cuh) interpolates launch
  seeds from calibrated chief and edge anchors for the requested pupil point.

## Surface intersections and refraction

- [`intersection_gate.cuh`](intersection_gate.cuh) computes surface slopes and
  plane or spherical intersections, and dispatches other surface shapes to
  the shared solver.
- [`asphere_intersection.cuh`](asphere_intersection.cuh) applies the configured
  asphere root policy using the [CUDA intersection solver](../../intersection/README.md).
- [`refraction.cuh`](refraction.cuh) computes the surface normal and the
  refracted direction, and detects total internal reflection.

## Trace results

- [`stop_trace.cuh`](stop_trace.cuh) launches rays from surface seeds and
  traces them to a specified surface, including the stop used for aiming.
- [`sequential_trace.cuh`](sequential_trace.cuh) traverses the candidate surfaces,
  applies aperture checks, and accumulates optical path lengths and aperture
  envelopes when requested.
- [`structural_trace.cuh`](structural_trace.cuh) records axis crossings and
  helps recover an alternative asphere root during aperture tracing.

The [scoring compilation unit](../../de_cuda_scorer.cu) includes these helpers.
