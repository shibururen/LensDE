"""Versioned numeric seed schedules, independent of systems and file locations.

Reference profiles contain historical integer seeds, never content-hash lookups.
At their reference master seed they reproduce the recorded seeds exactly.
"""
from __future__ import annotations

import copy
import json
from pathlib import Path

NUMERIC_POLICY = "numeric_stage_v2"
LEGACY_POLICY = "legacy_stage_v1"
GENERIC_PROFILE = "generic_v2"
SEED_LIMIT = 2_147_483_646
PROFILE_ROOT = Path(__file__).resolve().parent / "profiles"
ASPHERE_PROFILE_NAMES = (
    "6A_formal_20260929", "9A1_formal_20260929", "9A2_formal_20260929",
    "9A3_formal_20260929", "9A4_formal_20260928", "GGPGPPPP_formal_20261004",
)
SPHERICAL_PROFILE_NAMES = (
    "S1_formal_20261028", "S2_formal_20260917", "S3_formal_20260918", "S4_formal_20260918",
)
FORMAL_PROFILE_NAMES = SPHERICAL_PROFILE_NAMES + ASPHERE_PROFILE_NAMES
BENCHMARK_PROFILES = dict(zip(
    ("S1", "S2", "S3", "S4", "6A", "A1", "A2", "A3", "A4", "DF1"), FORMAL_PROFILE_NAMES,
))


class HistoricalSeedNotFoundError(ValueError):
    """A historical reference was requested without recorded integer evidence."""

OPTION_SEEDS = {
    "initial_search_funnel_probe_selection_seed": 20260515,
    "initial_search_checkpoint_optimizer_seed": 20261729,
    "initial_search_checkpoint_population_seed": 2026072400,
    "initial_search_de_dls_material_chromatic_repair_seed": 20260804,
    "initial_search_de_dls_material_bridge_seed": 20260719,
    "initial_search_de_dls_glass_scout_seed": 20260726,
    "initial_search_de_dls_glass_scout_rng_seed": 104729,
    "initial_search_de_dls_fixed_glass_feedback_seed": 20260704,
    "initial_search_de_dls_fixed_glass_feedback_rng_seed": 104729,
    "initial_search_post_asphere_seed": 20260709,
    "initial_search_post_asphere_rng_seed": 104729,
}


def shift_seed(reference: int, master: int, reference_master: int) -> int:
    """An arithmetic translation; exact identity at the reference master seed."""
    reference, master, reference_master = int(reference), int(master), int(reference_master)
    if not 0 <= reference <= SEED_LIMIT:
        raise ValueError(f"seed outside [0, {SEED_LIMIT}]: {reference}")
    if master == reference_master:
        return reference
    return 1 + (reference - 1 + master - reference_master) % SEED_LIMIT


def load_profile(name: str = GENERIC_PROFILE) -> dict:
    if name == GENERIC_PROFILE:
        return {"schema_version": 2, "name": name, "reference_master_seed": 20260515,
                "reference_initial_seed": 20260515, "option_seeds": dict(OPTION_SEEDS),
                "fixed_glass_feedback": {}, "post_asphere_stage_seeds": {}}
    if not name or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-" for c in name):
        raise ValueError(f"invalid seed profile name: {name!r}")
    path = PROFILE_ROOT / f"{name}.json"
    profile = json.loads(path.read_text(encoding="utf-8"))
    if profile.get("schema_version") != 2 or profile.get("name") != name:
        raise ValueError(f"invalid numeric seed profile: {path}")
    return profile


def validate_reference_profile(name: str) -> dict:
    """Validate the complete known reference table, never an empty glob.

    Spherical profiles have no polynomial stage and no active probe-selection
    branch. Their absent probe seed is intentionally not filled from generic_v2.
    """
    if name not in FORMAL_PROFILE_NAMES:
        raise HistoricalSeedNotFoundError(f"no audited historical profile: {name}")
    profile = load_profile(name)
    expected_fixed = {f"locked_glass/adaptive_jade/{rank}" for rank in range(1, 5)}
    expected_asphere = {
        f"scout_{stream}:1009:{rank}"
        for stream in ("optimizer", "population") for rank in range(1, 6)
    } | {
        f"{stage}_{stream}:1009:{rank}"
        for stage, rank in (("formation", 1), ("periodic", 2))
        for stream in ("optimizer", "population")
    }
    expected_options = set(OPTION_SEEDS)
    if name in SPHERICAL_PROFILE_NAMES:
        expected_asphere = set()
        expected_options.remove("initial_search_funnel_probe_selection_seed")
    expected_tables = {
        "option_seeds": expected_options,
        "fixed_glass_feedback": expected_fixed,
        "post_asphere_stage_seeds": expected_asphere,
    }
    for table, expected in expected_tables.items():
        actual = profile.get(table)
        if not isinstance(actual, dict) or set(actual) != expected:
            raise HistoricalSeedNotFoundError(
                f"incomplete historical table {name}/{table}; "
                f"expected {len(expected)} registered coordinates")
    integers = [profile["reference_master_seed"], profile["reference_initial_seed"]]
    integers.extend(profile["option_seeds"].values())
    integers.extend(profile["post_asphere_stage_seeds"].values())
    for coordinate, row in profile["fixed_glass_feedback"].items():
        if not isinstance(row, dict) or set(row) != {"optimizer_seed", "population_seed"}:
            raise HistoricalSeedNotFoundError(f"incomplete seed pair: {name}/{coordinate}")
        integers.extend(row.values())
    if any(type(value) is not int or not 0 <= value <= SEED_LIMIT for value in integers):
        raise HistoricalSeedNotFoundError(f"invalid historical integer seed: {name}")
    return {
        "profile": name,
        "option_seeds": len(profile["option_seeds"]),
        "fixed_glass_coordinates": len(profile["fixed_glass_feedback"]),
        "post_asphere_coordinates": len(profile["post_asphere_stage_seeds"]),
    }


def validate_reference_profiles() -> list[dict]:
    """Require all ten retained profiles and all their registered coordinates."""
    return [validate_reference_profile(name) for name in FORMAL_PROFILE_NAMES]


def policy(options: dict) -> str:
    value = str(options.get("initial_search_seed_policy", LEGACY_POLICY))
    if value not in {LEGACY_POLICY, NUMERIC_POLICY}:
        raise ValueError(f"unknown initial-search seed policy: {value}")
    return value


def apply_options(options: dict, *, strict: bool = False) -> dict:
    """Resolve stage bases after defaults, without changing legacy option sets."""
    if policy(options) == LEGACY_POLICY:
        if strict:
            raise HistoricalSeedNotFoundError("strict historical resolution requires numeric_stage_v2")
        return options
    profile_name = str(options.get("initial_search_seed_profile", GENERIC_PROFILE))
    if strict:
        validate_reference_profile(profile_name)
    profile = load_profile(profile_name)
    master = int(options.get("seed", profile["reference_master_seed"]))
    reference_master = int(profile["reference_master_seed"])
    for key, recorded in profile["option_seeds"].items():
        reference = int(recorded)
        options[key] = shift_seed(reference, master, reference_master)
    options["initial_search_effective_initial_seed"] = shift_seed(
        profile["reference_initial_seed"], master, reference_master)
    options["initial_search_de_dls_fixed_glass_feedback_seed_policy"] = NUMERIC_POLICY
    # This old manual override is an output/control field in v2, not another RNG input.
    options["initial_search_post_asphere_single_mother_formation_seed"] = 0
    options["initial_search_seed_profile"] = profile_name
    options["initial_search_resolved_seed_contract"] = {
        "policy": NUMERIC_POLICY, "profile": profile_name, "master_seed": master,
        "reference_master_seed": reference_master,
        "initial_seed": options["initial_search_effective_initial_seed"],
        "stage_bases": {key: options[key] for key in profile["option_seeds"]},
        "derivation": "reference_seed + master_seed - reference_master_seed (modulo positive seed range)",
    }
    return options


def initial_seed(options: dict) -> int:
    if policy(options) == NUMERIC_POLICY:
        apply_options(options)
        return int(options["initial_search_effective_initial_seed"])
    return int(options.get("seed", 20260515))


def apply_cli_args(args, *, strict: bool = False) -> None:
    """Resolve the two-layer CLI's stage bases from the same GUI seed schedule."""
    if getattr(args, "fixed_glass_feedback_seed_policy", LEGACY_POLICY) != NUMERIC_POLICY:
        if strict:
            raise HistoricalSeedNotFoundError("strict historical resolution requires numeric_stage_v2")
        return
    profile_name = str(getattr(args, "seed_profile", GENERIC_PROFILE))
    profile = load_profile(profile_name)
    master = getattr(args, "master_seed", None)
    options = {"seed": profile["reference_master_seed"] if master is None else int(master),
               "initial_search_seed_policy": NUMERIC_POLICY,
               "initial_search_seed_profile": profile_name}
    apply_options(options, strict=strict)
    args.master_seed = options["seed"]
    for key in profile["option_seeds"]:
        flag = key.removeprefix("initial_search_").removeprefix("de_dls_")
        if hasattr(args, flag):
            setattr(args, flag, options[key])


def post_asphere_seed(options: dict, stage: str, slot: tuple[int, ...], fallback: int,
                      *, strict: bool = False) -> int:
    if policy(options) == LEGACY_POLICY:
        if strict:
            raise HistoricalSeedNotFoundError("strict historical lookup requires numeric_stage_v2")
        return int(fallback)
    profile_name = str(options.get("initial_search_seed_profile", GENERIC_PROFILE))
    if strict:
        validate_reference_profile(profile_name)
    profile = load_profile(profile_name)
    key = stage + ":" + ":".join(str(int(value)) for value in slot)
    reference = profile["post_asphere_stage_seeds"].get(key)
    if reference is None:
        if strict:
            raise HistoricalSeedNotFoundError(f"no historical seed: {profile_name}/{key}")
        # The fallback is already based on apply_options' resolved stage bases.
        return int(fallback)
    return shift_seed(reference, options.get("seed", profile["reference_master_seed"]),
                      profile["reference_master_seed"])


def fixed_glass_seeds(*, profile_name: str, master: int, source_rank: int,
                      material_mode: str, algorithm: str, strict: bool = False) -> dict:
    if strict:
        validate_reference_profile(profile_name)
    profile = load_profile(profile_name)
    slot = int(source_rank)
    if slot < 1:
        raise ValueError("numeric seed candidate slots are one-based")
    coordinate = f"{material_mode}/{algorithm}/{slot}"
    reference = profile["fixed_glass_feedback"].get(coordinate)
    if reference is None:
        if strict:
            raise HistoricalSeedNotFoundError(f"no historical seed pair: {profile_name}/{coordinate}")
        modes = {"locked_glass": 0, "continuous": 1, "true_glass": 2}
        algorithms = {"adaptive_jade": 0, "de": 1, "ga": 2, "pso": 3, "sa": 4}
        if material_mode not in modes or algorithm not in algorithms:
            raise ValueError(f"unregistered numeric seed coordinates: {coordinate}")
        offset = modes[material_mode] * 100_003 + algorithms[algorithm] * 19_937
        reference = {
            "optimizer_seed": int(profile["option_seeds"].get(
                "initial_search_de_dls_fixed_glass_feedback_seed", 20260704)) + slot * 101 + offset,
            "population_seed": int(profile["option_seeds"].get(
                "initial_search_de_dls_fixed_glass_feedback_rng_seed", 104729)) + slot * 1009 + offset,
        }
    return {
        "seed_policy": NUMERIC_POLICY,
        "seed_key": f"{NUMERIC_POLICY}:{profile_name}:{coordinate}",
        "seed_profile": profile_name, "master_seed": int(master), "candidate_slot": slot,
        "reference_master_seed": int(profile["reference_master_seed"]),
        "optimizer_seed_base": shift_seed(profile["option_seeds"].get(
            "initial_search_de_dls_fixed_glass_feedback_seed", 20260704), master, profile["reference_master_seed"]),
        "population_seed_base": shift_seed(profile["option_seeds"].get(
            "initial_search_de_dls_fixed_glass_feedback_rng_seed", 104729), master, profile["reference_master_seed"]),
        "optimizer_seed": shift_seed(reference["optimizer_seed"], master, profile["reference_master_seed"]),
        "population_seed": shift_seed(reference["population_seed"], master, profile["reference_master_seed"]),
    }


def configure_seed_only(project: dict, profile_name: str, master: int | None = None,
                        *, strict: bool = False) -> dict:
    """Copy a project and resolve seeds without changing optical configuration.

    The strict option validates the retained reference tables. It does not assert
    that the supplied project's physical inputs match a historical experiment.
    """
    result = copy.deepcopy(project)
    options = result.setdefault("options", {})
    profile = load_profile(profile_name)
    options.update(initial_search_seed_policy=NUMERIC_POLICY,
                   initial_search_seed_profile=profile_name,
                   seed=profile["reference_master_seed"] if master is None else int(master))
    apply_options(options, strict=strict)
    return result


def configure_project_with_fixed_geometry_v2(
    project: dict, profile_name: str, master: int | None = None,
) -> dict:
    """Explicit compatibility with the old configure_project's TWO migrations.

    Besides numeric seeds this opts into protective-glass identification v2.
    It does not normalize the project or perform optical aperture rewriting.
    Use configure_seed_only for a seed-only migration.
    """
    result = configure_seed_only(project, profile_name, master)
    result["options"]["protective_glass_identification_policy"] = "fixed_geometry_v2"
    return result
