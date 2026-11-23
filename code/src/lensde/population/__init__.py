"""Deterministic local population construction, independent of workbench scripts."""

from .local import build_population, clamp_row, inject_material_neighbors, mutate_glass_choices
from .io import write_population_csv
from .sampling import perturb_same_shape

__all__ = ["build_population", "clamp_row", "inject_material_neighbors",
           "mutate_glass_choices", "perturb_same_shape", "write_population_csv"]
