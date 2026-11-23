# Merit evaluation for CUDA scoring

This directory uses candidate geometry, pupil rays, field measurements, and
merit settings to compute the score used by the optimizer. It aggregates trace
coverage and image measurements, evaluates optical and manufacturing limits,
and combines their penalties into a candidate score.

## Rays and field measurements

- [`supported_ray_generation.cuh`](supported_ray_generation.cuh) selects
  paraxial or calibrated ray generation for merit evaluation.
- [`residual_primitives.cuh`](residual_primitives.cuh) defines ray weights,
  trace progress, failure penalties, and residual scaling.
- [`spot_statistics.cuh`](spot_statistics.cuh) traces pupil rays and computes
  field spot, coverage, and illumination statistics.
- [`trace_spot_summary.cuh`](trace_spot_summary.cuh) aggregates field
  statistics into the candidate summary and image merit terms.

## Constraints and score assembly

- [`first_order_constraints.cuh`](first_order_constraints.cuh) evaluates chief
  ray and first-order image quantities, and supplies pupil samples for aperture
  tracing.
- [`geometry_constraints.cuh`](geometry_constraints.cuh) updates clear
  apertures from traced rays and evaluates glass proximity penalties.
- [`unified_residuals.cuh`](unified_residuals.cuh) evaluates geometric and
  manufacturing constraints and the trace-priority metric.
- [`unified_score.cuh`](unified_score.cuh) prepares candidate surfaces, combines
  the trace, constraint, first-order, and image penalties, and caps the score.

The [scoring compilation unit](../../de_cuda_scorer.cu) includes these helpers.
