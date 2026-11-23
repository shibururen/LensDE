"""Explicit phase-owned state for the retained initial-search runner.

Fields are initialized at their original phase position; no RNG or population
is copied into a second state owner. Missing fields cannot be added dynamically.
"""
from __future__ import annotations
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

@dataclass(slots=True, repr=False)
class SearchInputs:
    """Caller inputs retained by reference until their original normalization."""
    project: dict
    run_dir: Path
    run_merit: dict
    target_bfl: float
    target_efl: float
    target_ttl: float

@dataclass(slots=True, repr=False)
class SearchSettings:
    """Configured policies shared by argument and phase builders."""
    adaptive_initial: bool = field(init=False)
    aiming_mode: Any = field(init=False)
    baseline_bfl_bound_changes: Any = field(init=False)
    baseline_seed_config: dict = field(init=False)
    basin_de_initial: bool = field(init=False)
    de_dls_summary: dict = field(init=False)
    external_seed_path: Path = field(init=False)
    flow: Any = field(init=False)
    generations: int = field(init=False)
    hybrid_de_gen: int = field(init=False)
    hybrid_outer: int = field(init=False)
    hybrid_parents: int = field(init=False)
    hybrid_polish_workers: int = field(init=False)
    hybrid_refresh: Any = field(init=False)
    initial_polish_disabled: bool = field(init=False)
    material_stages_enabled: bool = field(init=False)
    mechanical_direct_seed: bool = field(init=False)
    optimization_variables_path: Path = field(init=False)
    options: dict = field(init=False)
    population: int = field(init=False)
    probe_cycle_enabled: bool = field(init=False)
    run_meta: Any = field(init=False)
    search_dir: Path = field(init=False)
    search_optimizer_mode: Any = field(init=False)
    seed: int = field(init=False)
    staged_full_outer: int = field(init=False)
    staged_search_outer: int = field(init=False)
    wide_angle_prior: Any = field(init=False)

@dataclass(slots=True, repr=False)
class OpticalState:
    """Current optical model, variable domain, merits and input files."""
    base_system: dict = field(init=False)
    continuous_glass_constraints_count: int = field(init=False)
    downstream_funnel_merit: dict = field(init=False)
    downstream_funnel_merit_path: Path = field(init=False)
    final_merit_path: Path = field(init=False)
    final_stage_merit: dict = field(init=False)
    full_merit: dict = field(init=False)
    glass_search_config: dict = field(init=False)
    glass_search_path: Path = field(init=False)
    initial_stage_merit: dict = field(init=False)
    merit_path: Path = field(init=False)
    merit_strategy: Any = field(init=False)
    outer_loose_merit_path: Path = field(init=False)
    search_merit: dict = field(init=False)
    system_path: Path = field(init=False)
    variables: list[dict] | list[list[float]] = field(init=False)
    variables_path: Path = field(init=False)

@dataclass(slots=True, repr=False)
class SeedState:
    """Seed preparation targets, projections and gate diagnostics."""
    adaptive_seed_caps: bool = field(init=False)
    adaptive_thickness_bounds: bool = field(init=False)
    asphere_profile_metadata: dict = field(init=False)
    base_seed_pool_target: int = field(init=False)
    bfl_surface_index: Any = field(init=False)
    evaluator_thickness_constraints: Any = field(init=False)
    front_groups: int = field(init=False)
    front_pairs: list[tuple[int, int]] = field(init=False)
    full_restart_per_outer_seed_pool_target: int = field(init=False)
    full_restart_seed_pool_multiplier: Any = field(init=False)
    full_restart_shared_seed_pool_enabled: bool = field(init=False)
    full_restart_shared_seed_pool_target: int = field(init=False)
    global_effective_variable_count: int = field(init=False)
    global_evaluator_dimension: int = field(init=False)
    initial_curvature_bound: Any = field(init=False)
    material_region_diversity_required: bool = field(init=False)
    material_region_sampling_plan: dict = field(init=False)
    refresh_pool_multiplier: Any = field(init=False)
    refresh_pool_target: int = field(init=False)
    rest_cap: Any = field(init=False)
    seed_air_thickness_cap: Any = field(init=False)
    seed_gate_failed_message: Any = field(init=False)
    seed_gate_merit: dict = field(init=False)
    seed_gate_merit_path: Path = field(init=False)
    seed_gate_stages: list = field(init=False)
    seed_gate_stderr_parts: list = field(init=False)
    seed_gate_stdout_parts: list = field(init=False)
    seed_glass_thickness_cap: Any = field(init=False)
    seed_path: Path = field(init=False)
    seed_pool_multiplier: Any = field(init=False)
    seed_pool_target: int = field(init=False)
    seed_rows: list[dict] | list[list[float]] = field(init=False)
    seed_strategy: Any = field(init=False)
    seed_thickness_budget: dict = field(init=False)
    seed_ttl_projection: dict = field(init=False)
    seed_ttl_projection_kwargs: dict = field(init=False)
    seed_variables: list[dict] | list[list[float]] = field(init=False)
    seed_variables_path: Path = field(init=False)

@dataclass(slots=True, repr=False)
class ExplorationState:
    """Restart, CRA, archive caches and exploration outcome."""
    code: int = field(init=False)
    cra_source_merit_fingerprint: Any = field(init=False)
    cra_start_tolerance: Any = field(init=False)
    cra_tighten_enabled: bool = field(init=False)
    cra_tighten_generations: Any = field(init=False)
    cra_tightened_merit: dict = field(init=False)
    cra_tightened_merit_fingerprint: Any = field(init=False)
    cra_tightened_merit_path: Path = field(init=False)
    cra_tightened_outer_root: Path = field(init=False)
    cra_tightened_tolerance: Any = field(init=False)
    full_restart_explore_outer_seed_budget: dict = field(init=False)
    funnel_global_micro_island_enabled: bool = field(init=False)
    funnel_global_micro_island_size_summary: dict = field(init=False)
    funnel_global_polish_enabled: bool = field(init=False)
    funnel_material_mode: Any = field(init=False)
    funnel_stage_material_modes: dict = field(init=False)
    funnel_summary: dict = field(init=False)
    global_explore_de_gen: int = field(init=False)
    global_explore_dir: Path = field(init=False)
    global_explore_outer: int = field(init=False)
    global_explore_stage_context: dict = field(init=False)
    outer_two_stage_enabled: bool = field(init=False)
    resume_existing_funnel_stages: list = field(init=False)
    source_topology_feature_cache: dict = field(init=False)
    stage_context_cache: dict = field(init=False)
    stage_topology_feature_cache: dict = field(init=False)
    stage_variables_file_cache: dict = field(init=False)
    timed_out: bool = field(init=False)

@dataclass(slots=True, repr=False)
class RuntimeState:
    """Executable, shared deadline, stage receipts and ordered logs."""
    exe: Path = field(init=False)
    stages: list = field(init=False)
    started_at: float = field(init=False)
    stderr_parts: list[str] | None = None
    stdout_parts: list[str] | None = None
    timeout_seconds: float = field(init=False)
    use_cuda: bool = field(init=False)

@dataclass(slots=True, repr=False)
class SearchState:
    """Named state groups exchanged by the six explicit workflow phases."""
    inputs: SearchInputs
    settings: SearchSettings = field(default_factory=SearchSettings)
    optics: OpticalState = field(default_factory=OpticalState)
    seeding: SeedState = field(default_factory=SeedState)
    exploration: ExplorationState = field(default_factory=ExplorationState)
    runtime: RuntimeState = field(default_factory=RuntimeState)

@dataclass(frozen=True, slots=True)
class PhaseResult:
    """Distinguish an early workflow return from ordinary phase completion."""
    finished: bool
    value: Any = None
