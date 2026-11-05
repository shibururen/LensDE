# Multi-stage automated lens design framework via history-guided differential evolution
This repository provides the optical design files accompanying the paper "Multi-stage automated lens design framework via history-guided differential evolution". The source code will be released upon manuscript acceptance.
# Overview
This work presents a multi-stage hybrid optimization framework for automated lens design, with differential evolution (DE) as its core optimization algorithm. The framework incorporates a search-history-guided strategy that combines a trajectory-aware checkpoint archive with cross-run search memory to preserve promising intermediate solutions and discourage redundant exploration. For tasks permitting vignetting, the framework jointly optimizes lens parameters and field-dependent vignetting coefficients using relative-illumination estimation and aperture matching. It enables the automated design of high-quality coaxial refractive systems with spherical or aspheric surfaces, including challenging ultra-wide-angle and large-aperture lenses.

# Experimental Results
1.Results for Spheric Tasks
The proposed framework achieves lower five-field mean geometric RMS spot radii than both OptiNeuro and the manual references across all four tasks. Relative to OptiNeuro, the reductions range from 21.3\% to 39.1\%, demonstrating the framework's capability to generate high-quality spherical lens designs across different apertures and FOV.

2.Results for Aspheric Task
The 6A task serves as a common benchmark for comparison with different methods. Under comparable nominal specifications, our design achieves a five-field mean geometric RMS spot radius of 2.034~$\mu$m, compared with 2.506~$\mu$m for the best displayed OptiNeuro design (an 18.8\% reduction).

The results of nine-element tasks A1–A4 achieve 25.4--32.4\% lower five-field mean geometric RMS spot radii than OptiNeuro and outperform the corresponding manual references.

3.Results for Glass-Plastic Hybrid Fisheye Task
This task presents the framework's ability for complexable design tasks. Our result achieves better imaging performances under the same specifications adopted by Optineuro, with an RMS spot radius of 1.571μm and better MTF performances.
