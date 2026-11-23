# CUDA asphere root candidates and refinement

This directory computes candidate distances where a ray meets a plane, conic, or even-aspheric surface. It starts from the surface geometry or a supplied distance and refines aspheric candidates by evaluating the residual along the ray. The certified solver uses these candidates in its search for the first forward intersection. [Intersection solver](../README.md)

## Candidate generation and refinement

- Computes initial distances from the plane or base conic and refines aspheric candidates. It also handles a starting distance supplied by the caller. [`conic_candidate.cuh`](conic_candidate.cuh)
- Evaluates the residual at a trial distance and checks which branch of the surface it hits. It uses bracketing and Newton steps to refine an aspheric candidate. [`newton_seed.cuh`](newton_seed.cuh)
- Limits the size of Newton steps in the fast refinement path. Its slope bound also supports checking whether the residual is monotonic over a search interval. [`bounded_seed.cuh`](bounded_seed.cuh)
- Refines a candidate using Newton steps constrained to a supplied distance interval during the first-root search. [`preconditioned_seed.cuh`](preconditioned_seed.cuh)

The CUDA intersection unit compiles these headers. [Intersection compilation unit](../../de_cuda_asphere_intersection.cu)
