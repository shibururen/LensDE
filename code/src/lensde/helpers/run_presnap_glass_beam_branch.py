"""Production helpers extracted from run_presnap_glass_beam_branch; unrelated historical entry points omitted."""

from __future__ import annotations

from lensde.resources import ProjectPaths
PROJECT_ROOT = ProjectPaths().root

from dataclasses import dataclass

from typing import Any

from lensde.helpers.run_final_material_neighbor_feedback import finite_float

@dataclass(frozen=True)
class LensSite:
    lens_no: int
    front_surface: int
    rear_surface: int
    nd: float
    vd: float

def lens_sites(system: dict[str, Any]) -> list[LensSite]:
    surfaces = list(system.get("surfaces") or [])
    sites: list[LensSite] = []
    for index, surface in enumerate(surfaces[:-1]):
        material = surface.get("material_after") or {}
        mode = str(material.get("mode") or "").lower()
        if mode not in {"abbe", "glass"}:
            continue
        if finite_float(material.get("nd"), 1.0) <= 1.05:
            continue
        sites.append(
            LensSite(
                lens_no=len(sites) + 1,
                front_surface=index,
                rear_surface=index + 1,
                nd=finite_float(material.get("nd")),
                vd=finite_float(material.get("vd"), 0.0),
            )
        )
    return sites

def glass_distance(glass: dict[str, Any], nd: float, vd: float) -> float:
    return ((float(glass["nd"]) - nd) / 0.035) ** 2 + ((float(glass["vd"]) - vd) / 7.5) ** 2
