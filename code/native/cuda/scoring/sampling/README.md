# Pupil sampling for CUDA scoring

This directory uses pupil sampling settings, field coordinates, and vignetting
factors to generate ordered pupil positions and weights for CUDA tracing and
merit evaluation. It also provides the indices and transforms used to
reconstruct the full set of samples from traced representatives.

- [`pupil_nodes.cuh`](pupil_nodes.cuh) generates Gaussian ring-and-arm
  samples and CODE V rectangular grids. It handles field-dependent sample
  counts, fixed-budget Gaussian layouts, and separate spot and wavefront
  weights for the rectangular grid.
- [`symmetry_mapping.cuh`](symmetry_mapping.cuh) selects representative
  samples and maps each full-pupil index to a traced index and transverse
  transform. It also defines the additional edge samples used by the
  single-ray spot-tail operand.

The [scoring compilation unit](../../de_cuda_scorer.cu) includes these helpers.
Trace and reduction kernels use the same sample order and index mapping.
