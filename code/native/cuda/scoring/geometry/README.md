# Device geometry for CUDA scoring

This directory uses candidate surface records, wavelength and material data,
and scoring settings to calculate the geometry used in CUDA ray tracing and
merit evaluation. It supplies surface profiles, asphere root domains, aperture
sizes, and optical and mechanical metrics.

## Surface geometry

- [`material_and_sag.cuh`](material_and_sag.cuh) resolves refractive indices
  at the requested wavelength and computes surface vertex positions and sag.
- [`asphere_slope.cuh`](asphere_slope.cuh) evaluates radial slopes and bounds
  them over radial intervals. It also checks whether a ray hit lies within the
  stored slope domain.
- [`domain_materialization.cuh`](domain_materialization.cuh) computes and
  caches the slope-limited root domains on the candidate surfaces.

## Paraxial and mechanical metrics

- [`first_order_metrics.cuh`](first_order_metrics.cuh) computes effective focal
  length, paraxial focus, and axial focus shift across active wavelengths.
- [`pupil_paraxial.cuh`](pupil_paraxial.cuh) traces paraxial rays, derives the
  entrance-pupil location and stop radius, and updates unfixed stop and
  mechanical apertures.
- [`mechanical_constraints.cuh`](mechanical_constraints.cuh) evaluates
  surface tangent angles, aperture-limited edge thickness, total track length,
  and back focal length.

The [scoring compilation unit](../../de_cuda_scorer.cu) includes these helpers.
They use the [device records](../contracts/README.md) prepared by the
[host configuration](../configuration/README.md).
