# CUDA surface geometry for ray intersection

This directory evaluates a surface along a ray and computes the forward distance intervals used by the CUDA intersection solver. Root refinement uses the residual and its derivative, while the search bounds restrict the distances that can be tested. [Intersection solver](../README.md)

## Surface evaluation and search bounds

- Evaluates the sag of plane, conic, and even-aspheric surfaces. It computes the difference between surface sag and the ray's axial coordinate, along with its derivative with respect to distance along the ray. [`sag_residual.cuh`](sag_residual.cuh)
- Finds the forward distance interval where the ray's transverse coordinates fall inside the stop aperture. It also clips a requested search interval to the valid conic domain of an even asphere and moves the endpoints inside that domain to account for rounding. [`forward_interval.cuh`](forward_interval.cuh)

Both headers compile through the CUDA intersection unit. [Intersection compilation unit](../../de_cuda_asphere_intersection.cu)
