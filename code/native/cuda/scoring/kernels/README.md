# CUDA kernels for population scoring

This directory uses candidate surfaces, field and wavelength settings, and
pupil samples to prepare geometry, build aiming models, trace rays, and reduce
the results to field statistics and candidate scores.

## Candidate geometry and apertures

- [`geometry_preparation.cuh`](geometry_preparation.cuh) materializes asphere
  slope domains, traces aperture samples, and sets candidate aperture radii.
- [`aperture_preparation.cuh`](aperture_preparation.cuh) traces candidate
  fields and records vignetting boundary support.

## Calibrated aiming

- [`center_model_build.cuh`](center_model_build.cuh) and
  [`field_model_build.cuh`](field_model_build.cuh) build the aiming models for
  center and off-axis fields.
- [`primary_edge_candidates.cuh`](primary_edge_candidates.cuh) and
  [`primary_model_finish.cuh`](primary_model_finish.cuh) evaluate primary
  wavelength edge candidates and finish models from accepted anchors.
- [`ordered_chief_prepare.cuh`](ordered_chief_prepare.cuh),
  [`ordered_chief_solve.cuh`](ordered_chief_solve.cuh), and
  [`ordered_model_finalize.cuh`](ordered_model_finalize.cuh) prepare
  chief-ray trials, reduce their results, and assemble the final models.
- [`cooperative_bridge_prepare.cuh`](cooperative_bridge_prepare.cuh) and
  [`cooperative_bridge_solve.cuh`](cooperative_bridge_solve.cuh) advance
  cooperative chief-ray trials through intermediate fields to the target field.

## Image measurements and scoring

- [`spot_trace.cuh`](spot_trace.cuh) traces chief, pupil, and guard rays
  and reconstructs pupil samples from their symmetry representatives.
- [`spot_reduction.cuh`](spot_reduction.cuh) reduces ray samples to field
  spot sizes, coverage, and trace statistics.
- [`wavefront_trace.cuh`](wavefront_trace.cuh) computes field wavefront errors
  and adds the wavefront terms to candidate merit.
- [`completed_field_certificate.cuh`](completed_field_certificate.cuh) records
  bounds from failed aiming models in completed fields.
- [`merit_reduction.cuh`](merit_reduction.cuh) bounds merit from selected
  field RMS values, applies spot guards, and finalizes candidate scores.

The [scoring compilation unit](../../de_cuda_scorer.cu) includes these kernels.
They exchange samples, statistics, and status through the
[device records](../contracts/README.md).
