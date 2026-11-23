# Interval bounds for CUDA asphere intersections

This directory bounds the geometry and intersection residual over a range of distances along a ray. The certified solver uses these bounds to discard intervals where the residual cannot reach zero and to narrow the search for the first forward intersection. [Intersection solver](../README.md)

## Interval arithmetic and residual bounds

- Provides scalar interval arithmetic with outward rounding. It bounds the squared distance from the optical axis and its derivative along the ray. [`radial_geometry.cuh`](radial_geometry.cuh)
- Bounds surface sag, the difference between sag and the ray's axial coordinate, and the derivative of that difference. It also provides a combined residual and derivative calculation that reuses intermediate bounds. [`asphere_residual.cuh`](asphere_residual.cuh)

Both headers compile through the CUDA intersection unit. [Intersection compilation unit](../../de_cuda_asphere_intersection.cu)
