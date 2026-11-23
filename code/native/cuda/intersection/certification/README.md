# First forward intersection with a CUDA asphere

This directory checks whether a ray has an admissible forward intersection with an even asphere. It tests candidate roots against the surface's valid radial domain and checks the ray segment before each candidate for earlier roots. When a candidate cannot be certified directly, the solver searches the permitted distance interval for the first hit. [Intersection solver](../README.md)

## Entry point and candidate checks

- Provides the certified intersection entry point and applies the selected root policy. [`dispatch.cuh`](dispatch.cuh)
- Applies the unified radial-domain rules. It checks the initial candidate, tries to establish that it is the first hit, and calls the interval search when needed. [`unified_domain.cuh`](unified_domain.cuh)
- Refines a candidate, checks for a sign change close to it, and tests whether the residual is monotonic from the start of the search through that bracket. [`candidate_root.cuh`](candidate_root.cuh)
- Checks whether the segment before a candidate can be ruled out and handles cases where the interval endpoints give evidence of an earlier root. [`candidate_prefix.cuh`](candidate_prefix.cuh)
- Uses geometric and residual-sign checks to recognize a root or a definite miss in selected cases, including recovery after an initial candidate fails. [`failure_classification.cuh`](failure_classification.cuh)

## Interval search and recovery

- Uses monotonicity and shorter brackets to check the segment before a candidate for an earlier root. [`prefix_piecewise.cuh`](prefix_piecewise.cuh)
- Subdivides the allowed interval in forward order. It uses residual and derivative bounds to discard intervals, locate the first root, or report that the search is inconclusive. [`first_root_interval.cuh`](first_root_interval.cuh)
- Narrows a sign-changing bracket using bisection and, where applicable, Newton steps inside the bracket. [`bracket_bisection.cuh`](bracket_bisection.cuh)
- Tries plane and conic starting estimates, checks for earlier intersections, and uses a bounded interval search when those estimates are insufficient. [`fallback.cuh`](fallback.cuh)

## Helpers for other root policies

- Handles initial-candidate acceptance and earlier-root checks in alternate root modes. [`initial_candidate.cuh`](initial_candidate.cuh)
- Runs the bounded candidate and seed checks for the alternate bounded-refinement policy. [`fast_bounded_candidate.cuh`](fast_bounded_candidate.cuh)

The CUDA intersection unit compiles these headers. [Intersection compilation unit](../../de_cuda_asphere_intersection.cu)
