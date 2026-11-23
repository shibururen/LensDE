# CUDA ray intersections with aspheres

This directory computes where a GPU ray hits a conic or even-aspheric surface. The solver takes the surface parameters and the ray's origin and direction, then reports whether it found an admissible forward hit and its distance. The certified path checks that the hit is the first forward root within the valid surface domain. The GPU scorer calls the device functions built by the intersection compilation unit. [Intersection compilation unit](../de_cuda_asphere_intersection.cu)

## Shared header

- Includes the device surface and intersection declarations, the status returned by solver stages, and standard math headers used throughout this directory. [`prelude.cuh`](prelude.cuh)

## Solver stages

- `geometry/` evaluates surface sag and the intersection residual and bounds the forward search.
- `intervals/` bounds ray coordinates and residuals over distance intervals.
- `topology/` determines the valid radial domain under the surface slope limit.
- `refinement/` computes trial roots from starting estimates and refines them.
- `certification/` checks candidate roots and searches for the first admissible hit when an earlier root cannot be ruled out.
