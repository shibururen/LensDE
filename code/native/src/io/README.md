# Native optical inputs and result files

This directory reads the lens prescription, ordered design variables, merit settings, and glass catalogs used by the native commands. It also writes the lens system and merit score JSON files used for checkpoints, results, and stage handoffs. [Public I/O API](../../include/optiland/fileio.hpp)

## Lens prescriptions and design variables

- Loads compact LensDE or Optiland JSON into a lens system, including surfaces, materials, fields, wavelengths, apertures, and aiming settings. [`system_reader.cpp`](system_reader.cpp)
- Reads variable types, surface indices, initial values, and bounds in the order used by the population CSV columns. [`variables_reader.cpp`](variables_reader.cpp)
- Provides shared text and CSV readers, JSON string escaping, and serialization of non-finite numbers as `null`. [`json.cpp`](json.cpp)
- Converts surface, variable, and aiming names and normalizes option names such as the distortion model and wavefront reference. [`names.cpp`](names.cpp)
- Defines the internal JSON parser and declares the helpers shared by the readers and writers. [`detail.hpp`](detail.hpp)

## Glass catalogs and search

- Resolves glass names and references through `catalog_nk.csv` and loads refractive-index models from `data-nk/`. [`glass_catalog.cpp`](glass_catalog.cpp)
- Builds the material candidate groups, checks wavelength coverage, and indexes nearby glasses using refractive index and Abbe number. [`glass_search.cpp`](glass_search.cpp)

## Merit settings

The native merit loader calls the following readers to apply the supplied JSON values to the current merit configuration. [Merit loader](../fileio.cpp)

- Reads the merit model, focal-length and track-length targets, back focal length, spot target, and their weights. [`merit_settings_targets.cpp`](merit_settings_targets.cpp)
- Reads manufacturing limits for glass and air center and edge thicknesses, plus surface-angle and structural settings. [`merit_settings_manufacturing.cpp`](merit_settings_manufacturing.cpp)
- Reads spot and wavefront scales, dimensional tolerances, and minimum relative illumination settings. [`merit_settings_quality.cpp`](merit_settings_quality.cpp)
- Reads vignetting penalties, the loss budget, and settings for evaluating the pupil boundary. [`merit_settings_vignetting.cpp`](merit_settings_vignetting.cpp)
- Reads chromatic, distortion, chief ray angle, and surface shape tolerances. [`merit_settings_chromatic_quality.cpp`](merit_settings_chromatic_quality.cpp)
- Reads pupil sampling, field weighting, clear-aperture sampling, and the wavefront reference. [`merit_settings_sampling.cpp`](merit_settings_sampling.cpp)
- Reads trace coverage thresholds, ray failure penalties, and weights for the merit groups. [`merit_settings_trace.cpp`](merit_settings_trace.cpp)
- Reads first-order and image-quality weights as well as gates for scoring image quality. [`merit_settings_image_weights.cpp`](merit_settings_image_weights.cpp)
- Reads explicit bounds, merit operands, and glass proximity constraints. [`merit_settings_constraints.cpp`](merit_settings_constraints.cpp)

## Result files

- Serializes the system prescription in LensDE JSON, preserving the order of surfaces, fields, and wavelengths. [`system_writer.cpp`](system_writer.cpp)
- Writes the merit score, measured quantities, and ordered merit contributions to JSON. [`score_writer.cpp`](score_writer.cpp)
