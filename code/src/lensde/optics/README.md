# Optical settings and stage handoffs

This package sets the rules for aperture updates, vignetting, and relative illumination when LensDE hands a lens system from one optimization stage to the next.

- **Update clear apertures.** `dynamic_aperture_contract.py` sets the four-ring, eight-arm sampling used for clear-aperture updates. Image-quality evaluation uses its own sampling settings.
- **Check vignetting handoffs.** `vignetting_handoff_policy.py` checks trace coverage and whether the chief ray in the configured pupil traced successfully.
- **Trim apertures within the illumination margin.** `stage_illumination_trim.py` proposes bounded changes to automatic apertures when edge-field illumination exceeds the required minimum by the configured margin. It checks the proposed apertures against native measurements.
- **Calibrate after asphere optimization.** `post_asphere_handoff.py` selects fields for relative-illumination checks and calibrates vignetting while preserving the lens prescription and fixed apertures.

The C++ and CUDA backends trace rays and evaluate optical performance. [Native analysis](../../../native/src/analysis/README.md)
