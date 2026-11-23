"""Explicit compatibility with historical fixed-material seed identity.

Copied from the original two-layer workflow; paths, JSON numeric spelling and
aperture flags intentionally remain part of stable_candidate_identity_v1.
Formal numeric profiles use recorded integer tables instead.
"""
from __future__ import annotations

import hashlib
import json
from typing import Any
from . import protocol

FIXED_GLASS_SEED_POLICY_STABLE = "stable_candidate_identity_v1"
FIXED_GLASS_SEED_POLICY_LEGACY = "legacy_source_rank"
FIXED_GLASS_SEED_POLICY_NUMERIC = protocol.NUMERIC_POLICY

def fixed_glass_feedback_candidate_key(
    source_system: dict[str, Any],
    material_mode: str,
    algorithm: str,
) -> str:
    """Legacy v1 identity, including serialized metadata and material paths."""
    optical_system = {
        key: source_system.get(key)
        for key in (
            "aperture",
            "max_field_deg",
            "fields",
            "wavelengths",
            "ray_aiming",
            "surfaces",
        )
        if key in source_system
    }
    payload = {
        "algorithm": str(algorithm),
        "material_mode": str(material_mode),
        "optical_system": optical_system,
    }
    canonical = json.dumps(
        payload,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=False,
    ).encode("utf-8")
    return hashlib.sha256(canonical).hexdigest()

def fixed_glass_feedback_seed_contract(
    *,
    source_system: dict[str, Any],
    material_mode: str,
    algorithm: str,
    source_rank: int,
    optimizer_seed_base: int,
    population_seed_base: int,
    seed_policy: str = FIXED_GLASS_SEED_POLICY_NUMERIC,
    seed_profile: str = protocol.GENERIC_PROFILE,
    master_seed: int | None = None,
    strict: bool = False,
) -> dict[str, Any]:
    policy = str(seed_policy or FIXED_GLASS_SEED_POLICY_NUMERIC).strip().lower()
    if policy == FIXED_GLASS_SEED_POLICY_NUMERIC:
        profile = protocol.load_profile(seed_profile)
        return protocol.fixed_glass_seeds(
            profile_name=seed_profile,
            master=int(profile["reference_master_seed"] if master_seed is None else master_seed),
            source_rank=source_rank, material_mode=material_mode, algorithm=algorithm,
            strict=strict,
        )
    candidate_key = fixed_glass_feedback_candidate_key(
        source_system,
        material_mode,
        algorithm,
    )
    policy = str(seed_policy or FIXED_GLASS_SEED_POLICY_STABLE).strip().lower()
    if policy == FIXED_GLASS_SEED_POLICY_LEGACY:
        optimizer_seed = int(optimizer_seed_base) + int(source_rank) * 101 + len(algorithm)
        population_seed = int(population_seed_base) + int(source_rank) * 1009 + len(algorithm)
    elif policy == FIXED_GLASS_SEED_POLICY_STABLE:
        def stable_stream_seed(base_seed: int, stream: str) -> int:
            material = f"{candidate_key}|{stream}|{int(base_seed)}".encode("ascii")
            digest = hashlib.sha256(material).digest()
            return 1 + int.from_bytes(digest[:8], "big") % 2_147_483_646

        optimizer_seed = stable_stream_seed(optimizer_seed_base, "optimizer")
        population_seed = stable_stream_seed(population_seed_base, "population")
    else:
        raise ValueError(f"Unknown fixed-glass feedback seed policy: {seed_policy}")
    return {
        "seed_policy": policy,
        "seed_key": f"sha256:{candidate_key}",
        "optimizer_seed_base": int(optimizer_seed_base),
        "population_seed_base": int(population_seed_base),
        "optimizer_seed": optimizer_seed,
        "population_seed": population_seed,
    }
