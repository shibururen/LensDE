# CUDA launch schedules for calibrated aiming

This directory uses candidate surfaces, fields, wavelengths, and scoring
settings to launch the CUDA kernels that fill the calibrated aiming model
buffer for each candidate, field, and wavelength.

- [`model_pipeline.cuh`](model_pipeline.cuh) selects the fused or ordered
  schedule. The fused schedule builds center-field models before off-axis
  models, starts each field with the primary wavelength, and retries invalid
  primary models after secondary wavelengths are available. It can also
  launch safeguarded recovery and record completed-field checks.
- [`ordered_model_pipeline.cuh`](ordered_model_pipeline.cuh) allocates chief
  trial and result buffers, launches the ordered solve and reduction tiles,
  and hands the selected seeds to the model finalization kernels. It also
  schedules cooperative continuation and the fallback stages for unresolved
  models.

The [scoring compilation unit](../../de_cuda_scorer.cu) includes these schedules.
The [CUDA kernels](../kernels/README.md) consume and produce the
[device records](../contracts/README.md) passed between stages.
