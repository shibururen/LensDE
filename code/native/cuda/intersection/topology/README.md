# Slope limits for CUDA asphere intersections

This directory provides the geometric checks used to find the first forward intersection within an asphere's slope-limited radial domain. It takes the ray direction and the surface's valid radius and helps the solver decide where to search and whether a candidate can be certified. [Intersection solver](../README.md)

- Clips the forward search interval to the distances where the ray lies within the surface's valid radius. It also checks whether the ray's axial motion exceeds the maximum sag change from its transverse motion. In that case the residual is monotonic, and a valid root within the domain can be certified as the first hit. [`slope_domain.cuh`](slope_domain.cuh)

The CUDA intersection unit compiles this header. [Intersection compilation unit](../../de_cuda_asphere_intersection.cu)
