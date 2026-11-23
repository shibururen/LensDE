# Wavefront reference geometry for CUDA scoring

This directory uses traced chief rays, pupil samples, and their wavelengths to
compute optical path differences relative to a reference sphere, in waves. It
also fits the piston and direction terms used by the wavefront calculation.

[`reference_geometry.cuh`](reference_geometry.cuh) performs these steps.

- Locate the exit pupil by paraxial tracing and build the reference sphere
  from the primary-wavelength chief ray's image intercept.
- Correct optical paths for their distance to the reference sphere and, for
  objects at infinity, their offset from the object-space reference plane.
- Convert the corrected chief-to-sample path differences to waves at each
  sample wavelength. Solve the weighted normal equations for one piston per
  usable wavelength and optional shared X and Y direction terms.

The [scoring compilation unit](../../de_cuda_scorer.cu) includes this header.
The calculation uses the [device ray and wavefront records](../contracts/README.md).
