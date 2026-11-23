"""Explicit compatibility entry points for responsibility-owned seed algorithms.

Callers and preparation hooks retain these signatures. Generator callbacks stay
late-bound here; leaf modules neither import this facade nor inspect namespaces.
"""
from __future__ import annotations

from .seed_population import coordinates as _seed_coordinates
from .seed_population import domains as _seed_domains
from .seed_population import diversity as _seed_diversity
from .seed_population import selection as _seed_selection
from .seed_population import coverage as _seed_coverage
from .seed_population import variables as _seed_variables
from .seed_population import thickness as _seed_thickness
from .seed_population import materials as _seed_materials
from .seed_population import hybrid_generation as _seed_hybrid_generation
from .seed_population import mechanical as _seed_mechanical
from .seed_population import first_order as _seed_first_order
from .seed_population import io as _seed_io
from .seed_population import sizing as _seed_sizing
from .seed_population import local as _seed_local

def _call_hybrid_generator(*args, **kwargs):
    return make_hybrid_initial_seed_population(*args, **kwargs)


def _call_physics_generator(*args, **kwargs):
    return generate_budgeted_first_order_seed_population(*args, **kwargs)


def is_profiled_asphere_seed_variable(variable: dict) -> bool:
    return _seed_coordinates.is_profiled_asphere_seed_variable(variable)


def profiled_asphere_seed_q_scale(variable: dict) -> float:
    return _seed_coordinates.profiled_asphere_seed_q_scale(variable)


def _finite_seed_number(value, default: float=0.0) -> float:
    return _seed_coordinates._finite_seed_number(value, default)


def _variable_aware_seed_scales(variables: list[dict]) -> tuple[float, ...]:
    return _seed_coordinates._variable_aware_seed_scales(variables)


def _variable_aware_seed_row_key_from_scales(row: list[float], scales: tuple[float, ...], digits: int) -> tuple[float, ...]:
    return _seed_coordinates._variable_aware_seed_row_key_from_scales(row, scales, digits)


def seed_variable_coordinate(value: float, variable: dict) -> float:
    return _seed_coordinates.seed_variable_coordinate(value, variable)


def seed_variable_coordinate_bounds(variable: dict) -> tuple[float, float]:
    return _seed_coordinates.seed_variable_coordinate_bounds(variable)


def variable_aware_seed_row_key(row: list[float], variables: list[dict], digits: int=12) -> tuple[float, ...]:
    return _seed_coordinates.variable_aware_seed_row_key(row, variables, digits)


def dedupe_variable_aware_seed_rows(rows: list[list[float]], variables: list[dict], digits: int=12) -> list[list[float]]:
    return _seed_coordinates.dedupe_variable_aware_seed_rows(rows, variables, digits)


def append_profiled_asphere_seed_selector_args(args: list[str], variables: list[dict]) -> None:
    return _seed_coordinates.append_profiled_asphere_seed_selector_args(args, variables)


def bound_normalized_delta(x: float, y: float, variable: dict) -> float:
    return _seed_coordinates.bound_normalized_delta(x, y, variable)


def bound_normalized_distance(a: list[float], b: list[float], variables: list[dict]) -> float:
    return _seed_coordinates.bound_normalized_distance(a, b, variables)


def seed_activity_tolerance(variable: dict) -> float:
    return _seed_coordinates.seed_activity_tolerance(variable)


def active_indices_from_row(row: list[float], variables: list[dict], *, reference_row: list[float] | None=None, tol: float | None=None) -> set[int]:
    return _seed_coordinates.active_indices_from_row(row, variables, reference_row=reference_row, tol=tol)


def row_curvature_mask(row: list[float], variables: list[dict], *, tol: float | None=None) -> str:
    return _seed_coordinates.row_curvature_mask(row, variables, tol=tol)


def seed_variable_group(system: dict, variable: dict) -> int | None:
    return _seed_coordinates.seed_variable_group(system, variable)


def clamp_variable_value(value: float, variable: dict) -> float:
    return _seed_coordinates.clamp_variable_value(value, variable)


def project_optimization_variable_value(value: float, variable: dict) -> float:
    return _seed_coordinates.project_optimization_variable_value(value, variable)


def perturb_seed_value_by_kind(value: float, variable: dict, rng: random.Random, profile: dict, *, system: dict | None=None) -> float:
    return _seed_coordinates.perturb_seed_value_by_kind(value, variable, rng, profile, system=system)


def mutate_independent_sparse_seed_row(parent: list[float], variables: list[dict], p: float, profile: dict, rng: random.Random, *, system: dict) -> list[float]:
    return _seed_coordinates.mutate_independent_sparse_seed_row(parent, variables, p, profile, rng, system=system)


def mutate_group_masked_seed_row(parent: list[float], variables: list[dict], selected_groups: set[int], profile: dict, rng: random.Random, *, system: dict) -> list[float]:
    return _seed_coordinates.mutate_group_masked_seed_row(parent, variables, selected_groups, profile, rng, system=system)


def seed_diversity_thresholds_from_options(options: dict | None=None) -> dict[str, float]:
    return _seed_diversity.seed_diversity_thresholds_from_options(options)


def seed_pool_diversity_summary(rows: list[list[float]], variables: list[dict], *, min_range_frac: float=0.001, target_range_frac: float=0.01, min_active_fraction: float=0.05, min_curvature_active_fraction: float | None=None, active_target_count: int | None=None) -> dict:
    return _seed_diversity.seed_pool_diversity_summary(rows, variables, min_range_frac=min_range_frac, target_range_frac=target_range_frac, min_active_fraction=min_active_fraction, min_curvature_active_fraction=min_curvature_active_fraction, active_target_count=active_target_count)


def mutable_vignetting_variable_indices(variables: list[dict]) -> list[int]:
    return _seed_diversity.mutable_vignetting_variable_indices(variables)


def mandatory_vignetting_seed_diversity_requirements(summary: dict, variables: list[dict], *, target_range_frac: float, require_active_ready: bool, minimum_active_count: int=1) -> dict:
    return _seed_diversity.mandatory_vignetting_seed_diversity_requirements(summary, variables, target_range_frac=target_range_frac, require_active_ready=require_active_ready, minimum_active_count=minimum_active_count)


def vignetting_seed_population_preflight(rows: list[list[float]], variables: list[dict], *, min_range_frac: float=0.001) -> tuple[bool, dict, dict]:
    return _seed_diversity.vignetting_seed_population_preflight(rows, variables, min_range_frac=min_range_frac)


def seed_pool_refill_diversity_ready(rows: list[list[float]], variables: list[dict], *, min_range_frac: float=0.001, target_range_frac: float=0.01, min_active_fraction: float=0.05, min_curvature_active_fraction: float | None=None, active_target_count: int | None=None, summary: dict | None=None) -> tuple[bool, dict, dict]:
    return _seed_diversity.seed_pool_refill_diversity_ready(rows, variables, min_range_frac=min_range_frac, target_range_frac=target_range_frac, min_active_fraction=min_active_fraction, min_curvature_active_fraction=min_curvature_active_fraction, active_target_count=active_target_count, summary=summary)


def select_traceable_diverse_population(objects: list[dict], variables: list[dict], target: int, seed: int, *, min_range_frac: float=0.001, target_range_frac: float=0.01, min_active_fraction: float=0.05, min_curvature_active_fraction: float | None=None) -> list[list[float]]:
    return _seed_selection.select_traceable_diverse_population(objects, variables, target, seed, min_range_frac=min_range_frac, target_range_frac=target_range_frac, min_active_fraction=min_active_fraction, min_curvature_active_fraction=min_curvature_active_fraction)


def repair_seed_population_active_coverage(selected_rows: list[list[float]], objects: list[dict], variables: list[dict], target: int, *, min_active_fraction: float=0.05, min_curvature_active_fraction: float | None=None) -> tuple[list[list[float]], dict]:
    return _seed_coverage.repair_seed_population_active_coverage(selected_rows, objects, variables, target, min_active_fraction=min_active_fraction, min_curvature_active_fraction=min_curvature_active_fraction)


def front_prior_ranges(profile: str) -> tuple[tuple[float, float], tuple[float, float]]:
    return _seed_variables.front_prior_ranges(profile)


def variable_current_value_for_system(system: dict, variable: dict, default: float=0.0) -> float:
    return _seed_coordinates.variable_current_value_for_system(system, variable, default)


def make_hybrid_initial_search_variables(system: dict, base_variables: list[dict], *, wide_angle_prior: bool, front_groups: int, front_profile: str, rest_cap: float, target_ttl: float, target_bfl: float, target_efl: float=0.0, adaptive_thickness: bool=True, thickness_constraints: dict | None=None, bfl_surface_index: int | None=None, bfl_lower_factor: float=0.9, bfl_upper_factor: float=1.5, use_current_thickness_prior: bool=False) -> tuple[list[dict], list[tuple[int, int]]]:
    return _seed_variables.make_hybrid_initial_search_variables(system, base_variables, wide_angle_prior=wide_angle_prior, front_groups=front_groups, front_profile=front_profile, rest_cap=rest_cap, target_ttl=target_ttl, target_bfl=target_bfl, target_efl=target_efl, adaptive_thickness=adaptive_thickness, thickness_constraints=thickness_constraints, bfl_surface_index=bfl_surface_index, bfl_lower_factor=bfl_lower_factor, bfl_upper_factor=bfl_upper_factor, use_current_thickness_prior=use_current_thickness_prior)


def system_fallback_radius(system: dict) -> float:
    return _seed_variables.system_fallback_radius(system)


def edge_radius_for_initial_search(system: dict, surface_index: int) -> float:
    return _seed_variables.edge_radius_for_initial_search(system, surface_index)


def asphere_bound_span_for_initial_search(system: dict, variables: list[dict], value: float, surface_index: int, term: int) -> float:
    return _seed_variables.asphere_bound_span_for_initial_search(system, variables, value, surface_index, term)


def append_initial_search_live_variables(system: dict, variables: list[dict], source_variables: list[dict], options: dict) -> list[dict]:
    return _seed_variables.append_initial_search_live_variables(system, variables, source_variables, options)


def canonical_funnel_asphere_refine_max_order(value: object) -> int:
    return _seed_variables.canonical_funnel_asphere_refine_max_order(value)


def canonical_post_asphere_max_order(value: object) -> int:
    return _seed_variables.canonical_post_asphere_max_order(value)


def asphere_order_for_term(term: int) -> int:
    return _seed_coordinates.asphere_order_for_term(term)


def asphere_term_for_order(order: int) -> int:
    return _seed_coordinates.asphere_term_for_order(order)


def surface_has_prescribed_asphere(surface: dict, source_terms: set[int] | None=None) -> bool:
    return _seed_variables.surface_has_prescribed_asphere(surface, source_terms)


def funnel_asphere_stage_terms(stage: str, options: dict) -> list[int]:
    return _seed_variables.funnel_asphere_stage_terms(stage, options)


def funnel_asphere_field_count_for_stage(stage: str, options: dict) -> int:
    return _seed_variables.funnel_asphere_field_count_for_stage(stage, options)


def funnel_field_count_for_stage(stage: str, options: dict) -> int:
    return _seed_variables.funnel_field_count_for_stage(stage, options)


def append_funnel_asphere_variables(system: dict, variables: list[dict], source_variables: list[dict], options: dict, stage: str) -> list[dict]:
    return _seed_variables.append_funnel_asphere_variables(system, variables, source_variables, options, stage)


def fixed_track_length(system: dict, variables: list[dict]) -> float:
    return _seed_thickness.fixed_track_length(system, variables)


def ttl_seed_thickness_budget_config(system: dict, variables: list[dict], target_ttl: float, target_bfl: float, target_efl: float=0.0, *, enabled: bool=True, trigger_ratio: float=1.15, compact_slack_fraction: float=0.55, lower_factor: float=0.9, upper_factor: float=1.03, trace_upper_factor: float=1.2, allow_short_seed: bool=True, seed_max_factor: float=1.0, bfl_surface_index: int | None=None, bfl_lower_factor: float=0.9, bfl_upper_factor: float=1.5) -> dict:
    return _seed_thickness.ttl_seed_thickness_budget_config(system, variables, target_ttl, target_bfl, target_efl, enabled=enabled, trigger_ratio=trigger_ratio, compact_slack_fraction=compact_slack_fraction, lower_factor=lower_factor, upper_factor=upper_factor, trace_upper_factor=trace_upper_factor, allow_short_seed=allow_short_seed, seed_max_factor=seed_max_factor, bfl_surface_index=bfl_surface_index, bfl_lower_factor=bfl_lower_factor, bfl_upper_factor=bfl_upper_factor)


def project_thickness_values_to_ttl_budget(system: dict, variables: list[dict], values: dict[tuple[str, int, int], float], config: dict, rng: random.Random, mutable_keys: set[tuple[str, int, int]] | None=None, uniform_non_bfl_when_contracting: bool=False) -> bool:
    return _seed_thickness.project_thickness_values_to_ttl_budget(system, variables, values, config, rng, mutable_keys, uniform_non_bfl_when_contracting)


def prepare_catalog_material_seed_sampling_plan(system: dict, variables: list[dict], *, current_values: dict[tuple[str, int, int], float] | None=None, nd_cap: float=0.0, vd_cap: float=0.0, variable_active: Callable[[dict], bool] | None=None) -> list[dict]:
    return _seed_materials.prepare_catalog_material_seed_sampling_plan(system, variables, current_values=current_values, nd_cap=nd_cap, vd_cap=vd_cap, variable_active=variable_active)


def sample_catalog_material_seed_values(system: dict, variables: list[dict], rng: random.Random, sample_index: int, *, current_values: dict[tuple[str, int, int], float] | None=None, nd_cap: float=0.0, vd_cap: float=0.0, variable_active: Callable[[dict], bool] | None=None, prepared_plan: list[dict] | None=None) -> dict[tuple[str, int, int], float]:
    return _seed_materials.sample_catalog_material_seed_values(system, variables, rng, sample_index, current_values=current_values, nd_cap=nd_cap, vd_cap=vd_cap, variable_active=variable_active, prepared_plan=prepared_plan)


def material_seed_plan_region_label(entry: dict, nd: float, vd: float) -> int | None:
    return _seed_materials.material_seed_plan_region_label(entry, nd, vd)


def material_seed_region_diversity_applicable(prepared_plan: list[dict] | None, minimum_regions: int=2) -> bool:
    return _seed_materials.material_seed_region_diversity_applicable(prepared_plan, minimum_regions)


def material_seed_row_region_labels(row: list[float], variables: list[dict], prepared_plan: list[dict] | None) -> list[int]:
    return _seed_materials.material_seed_row_region_labels(row, variables, prepared_plan)


def material_seed_row_region_count(row: list[float], variables: list[dict], prepared_plan: list[dict] | None) -> int:
    return _seed_materials.material_seed_row_region_count(row, variables, prepared_plan)


def enforce_material_seed_population_region_diversity(system: dict, variables: list[dict], rows: list[list[float]], rng: random.Random, prepared_plan: list[dict] | None, *, minimum_regions: int=2) -> list[list[float]]:
    return _seed_materials.enforce_material_seed_population_region_diversity(system, variables, rows, rng, prepared_plan, minimum_regions=minimum_regions)


def project_material_anchor_to_catalog(system: dict, variables: list[dict], values: dict[tuple[str, int, int], float]) -> dict[tuple[str, int, int], float]:
    return _seed_materials.project_material_anchor_to_catalog(system, variables, values)


def canonical_initial_thickness_sampling_mode(value: object) -> str:
    return _seed_thickness.canonical_initial_thickness_sampling_mode(value)


def allocate_bounded_thickness_total(variables: list[dict], total: float, rng: random.Random, alpha: float) -> dict[tuple[str, int, int], float] | None:
    return _seed_thickness.allocate_bounded_thickness_total(variables, total, rng, alpha)


def sample_budget_simplex_thickness_values(system: dict, variables: list[dict], rng: random.Random, target_ttl: float, target_bfl: float, *, ttl_quantile: float | None=None, bfl_quantile: float | None=None, ttl_lower_fraction: float=0.9, ttl_upper_fraction: float=1.0, bfl_lower_factor: float=0.9, bfl_upper_factor: float=1.0, bfl_ttl_upper_fraction: float=0.0, glass_beta_alpha: float=2.0, glass_beta_beta: float=4.0, air_dirichlet_alpha: float=2.0, bfl_surface_index: int | None=None, max_attempts: int=256) -> tuple[dict[tuple[str, int, int], float], dict]:
    return _seed_thickness.sample_budget_simplex_thickness_values(system, variables, rng, target_ttl, target_bfl, ttl_quantile=ttl_quantile, bfl_quantile=bfl_quantile, ttl_lower_fraction=ttl_lower_fraction, ttl_upper_fraction=ttl_upper_fraction, bfl_lower_factor=bfl_lower_factor, bfl_upper_factor=bfl_upper_factor, bfl_ttl_upper_fraction=bfl_ttl_upper_fraction, glass_beta_alpha=glass_beta_alpha, glass_beta_beta=glass_beta_beta, air_dirichlet_alpha=air_dirichlet_alpha, bfl_surface_index=bfl_surface_index, max_attempts=max_attempts)


def make_hybrid_initial_seed_population(system: dict, variables: list[dict], front_pairs: list[tuple[int, int]], population: int, seed: int, target_ttl: float, target_bfl: float, rest_cap: float, seed_curvature_cap: float, population_generation_mode: str='random', front_profile: str='weak', seed_air_thickness_cap: float=25.0, seed_glass_thickness_cap: float=2.0, seed_material_nd_cap: float=0.0, seed_material_vd_cap: float=0.0, local_group_perturbation: bool=False, local_long_air_gap_prior: bool=False, perturb_material_variables: bool=True, perturb_other_variables: bool=False, target_efl: float=0.0, auto_ttl_thickness_budget: bool=True, ttl_budget_trigger_ratio: float=1.15, ttl_budget_compact_slack_fraction: float=0.55, ttl_budget_lower_factor: float=0.9, ttl_budget_upper_factor: float=1.03, ttl_budget_trace_upper_factor: float=1.2, seed_ttl_allow_short: bool=True, seed_ttl_max_factor: float=1.0, bfl_surface_index: int | None=None, bfl_lower_factor: float=0.9, bfl_upper_factor: float=1.5, thickness_sampling_mode: str='legacy', budget_simplex_ttl_lower_fraction: float=0.9, budget_simplex_ttl_upper_fraction: float=1.0, budget_simplex_bfl_lower_factor: float=0.9, budget_simplex_bfl_upper_factor: float=1.0, budget_simplex_bfl_ttl_upper_fraction: float=0.0, budget_simplex_glass_beta_alpha: float=2.0, budget_simplex_glass_beta_beta: float=4.0, budget_simplex_air_dirichlet_alpha: float=2.0, stratified_bfl_sampling: bool=False, stratified_bfl_lower_factor: float=1.0, stratified_bfl_upper_factor: float=1.0, asphere_profile_metadata: dict[tuple[float, ...], dict] | None=None, apply_profiled_asphere_population: bool=True, reproject_local_thickness_rows: bool=True) -> list[list[float]]:
    return _seed_hybrid_generation._make_hybrid_initial_seed_population(system, variables, front_pairs, population, seed, target_ttl, target_bfl, rest_cap, seed_curvature_cap, population_generation_mode, front_profile, seed_air_thickness_cap, seed_glass_thickness_cap, seed_material_nd_cap, seed_material_vd_cap, local_group_perturbation, local_long_air_gap_prior, perturb_material_variables, perturb_other_variables, target_efl, auto_ttl_thickness_budget, ttl_budget_trigger_ratio, ttl_budget_compact_slack_fraction, ttl_budget_lower_factor, ttl_budget_upper_factor, ttl_budget_trace_upper_factor, seed_ttl_allow_short, seed_ttl_max_factor, bfl_surface_index, bfl_lower_factor, bfl_upper_factor, thickness_sampling_mode, budget_simplex_ttl_lower_fraction, budget_simplex_ttl_upper_fraction, budget_simplex_bfl_lower_factor, budget_simplex_bfl_upper_factor, budget_simplex_bfl_ttl_upper_fraction, budget_simplex_glass_beta_alpha, budget_simplex_glass_beta_beta, budget_simplex_air_dirichlet_alpha, stratified_bfl_sampling, stratified_bfl_lower_factor, stratified_bfl_upper_factor, asphere_profile_metadata, apply_profiled_asphere_population, reproject_local_thickness_rows, _physics_generator=_call_physics_generator)


def canonical_initial_seed_strategy(value: object, fallback: str='anchor_sparse_refill') -> str:
    return _seed_sizing.canonical_initial_seed_strategy(value, fallback)


def mechanical_stratified_budget_summary(system: dict, variables: list[dict], target_ttl: float, target_bfl: float, *, custom_ttl_range_enabled: bool=False, ttl_lower_fraction: float=0.7, ttl_upper_fraction: float=1.5, bfl_lower_factor: float=0.9, bfl_ttl_upper_fraction: float=0.5, bfl_surface_index: int | None=None) -> dict:
    return _seed_mechanical.mechanical_stratified_budget_summary(system, variables, target_ttl, target_bfl, custom_ttl_range_enabled=custom_ttl_range_enabled, ttl_lower_fraction=ttl_lower_fraction, ttl_upper_fraction=ttl_upper_fraction, bfl_lower_factor=bfl_lower_factor, bfl_ttl_upper_fraction=bfl_ttl_upper_fraction, bfl_surface_index=bfl_surface_index)


def expand_mechanical_stratified_bfl_seed_domain(system: dict, seed_variables: list[dict], optimization_preview_variables: list[dict], target_ttl: float, *, bfl_ttl_upper_fraction: float=0.5, bfl_surface_index: int | None=None) -> tuple[list[dict], list[dict]]:
    return _seed_mechanical.expand_mechanical_stratified_bfl_seed_domain(system, seed_variables, optimization_preview_variables, target_ttl, bfl_ttl_upper_fraction=bfl_ttl_upper_fraction, bfl_surface_index=bfl_surface_index)


def inject_balanced_initial_shape_coverage(rows: list[list[float]], variables: list[dict], seed: int, *, conic_fraction: float=0.08, conic_abs_max: float=100.0, asphere_fraction: float=0.02, asphere_bound_fraction: float=1.0, max_active_per_row: int=4) -> dict:
    return _seed_mechanical.inject_balanced_initial_shape_coverage(rows, variables, seed, conic_fraction=conic_fraction, conic_abs_max=conic_abs_max, asphere_fraction=asphere_fraction, asphere_bound_fraction=asphere_bound_fraction, max_active_per_row=max_active_per_row)


def make_mechanical_stratified_direct_population(system: dict, variables: list[dict], front_pairs: list[tuple[int, int]], population: int, seed: int, target_ttl: float, target_bfl: float, target_efl: float, *, rest_cap: float, seed_curvature_cap: float, front_profile: str='weak', perturb_material_variables: bool=True, seed_material_nd_cap: float=0.45, seed_material_vd_cap: float=50.0, bfl_surface_index: int | None=None, custom_ttl_range_enabled: bool=False, ttl_lower_fraction: float=0.7, ttl_upper_fraction: float=1.5, bfl_lower_factor: float=0.9, bfl_ttl_upper_fraction: float=0.5, glass_beta_alpha: float=2.0, glass_beta_beta: float=4.0, air_dirichlet_alpha: float=2.0, conic_coverage_fraction: float=0.08, conic_abs_max: float=100.0, asphere_coverage_fraction: float=0.02, asphere_bound_fraction: float=1.0, max_active_shape_per_row: int=4) -> tuple[list[list[float]], dict]:
    return _seed_mechanical._make_mechanical_stratified_direct_population(system, variables, front_pairs, population, seed, target_ttl, target_bfl, target_efl, rest_cap=rest_cap, seed_curvature_cap=seed_curvature_cap, front_profile=front_profile, perturb_material_variables=perturb_material_variables, seed_material_nd_cap=seed_material_nd_cap, seed_material_vd_cap=seed_material_vd_cap, bfl_surface_index=bfl_surface_index, custom_ttl_range_enabled=custom_ttl_range_enabled, ttl_lower_fraction=ttl_lower_fraction, ttl_upper_fraction=ttl_upper_fraction, bfl_lower_factor=bfl_lower_factor, bfl_ttl_upper_fraction=bfl_ttl_upper_fraction, glass_beta_alpha=glass_beta_alpha, glass_beta_beta=glass_beta_beta, air_dirichlet_alpha=air_dirichlet_alpha, conic_coverage_fraction=conic_coverage_fraction, conic_abs_max=conic_abs_max, asphere_coverage_fraction=asphere_coverage_fraction, asphere_bound_fraction=asphere_bound_fraction, max_active_shape_per_row=max_active_shape_per_row, _hybrid_generator=_call_hybrid_generator)


def surface_after_is_glass(system: dict, index: int) -> bool:
    return _seed_domains.surface_after_is_glass(system, index)


def is_thickness_variable(variable: dict) -> bool:
    return _seed_domains.is_thickness_variable(variable)


def is_glass_thickness_variable(system: dict, variable: dict) -> bool:
    return _seed_domains.is_glass_thickness_variable(system, variable)


def is_air_thickness_variable(system: dict, variable: dict) -> bool:
    return _seed_domains.is_air_thickness_variable(system, variable)


def is_physical_stop_surface(surface: dict) -> bool:
    return _seed_domains.is_physical_stop_surface(surface)


def is_bfl_variable(system: dict, variable: dict, bfl_surface_index: int | None=None) -> bool:
    return _seed_domains.is_bfl_variable(system, variable, bfl_surface_index)


def is_bfl_anchor_variable(system: dict, variable: dict, bfl_surface_index: int | None=None) -> bool:
    return _seed_domains.is_bfl_anchor_variable(system, variable, bfl_surface_index)


def bfl_anchor_seed_target(system: dict, target_bfl: float, bfl_surface_index: int | None=None) -> float | None:
    return _seed_domains.bfl_anchor_seed_target(system, target_bfl, bfl_surface_index)


def clamp_value(value: float, lower: float, upper: float) -> float:
    return _seed_coordinates.clamp_value(value, lower, upper)


def next_normal(rng: random.Random) -> float:
    return _seed_coordinates.next_normal(rng)


def generate_budgeted_first_order_seed_population(system: dict, variables: list[dict], rows: int, target_efl: float, target_ttl: float, target_bfl: float, seed: int, curvature_cap: float, *, perturb_material_variables: bool=True, perturb_other_variables: bool=False, auto_ttl_thickness_budget: bool=True, ttl_budget_trigger_ratio: float=1.15, ttl_budget_compact_slack_fraction: float=0.55, ttl_budget_lower_factor: float=0.9, ttl_budget_upper_factor: float=1.03, ttl_budget_trace_upper_factor: float=1.2, bfl_surface_index: int | None=None, bfl_lower_factor: float=0.9, bfl_upper_factor: float=1.5) -> list[list[float]]:
    return _seed_first_order.generate_budgeted_first_order_seed_population(system, variables, rows, target_efl, target_ttl, target_bfl, seed, curvature_cap, perturb_material_variables=perturb_material_variables, perturb_other_variables=perturb_other_variables, auto_ttl_thickness_budget=auto_ttl_thickness_budget, ttl_budget_trigger_ratio=ttl_budget_trigger_ratio, ttl_budget_compact_slack_fraction=ttl_budget_compact_slack_fraction, ttl_budget_lower_factor=ttl_budget_lower_factor, ttl_budget_upper_factor=ttl_budget_upper_factor, ttl_budget_trace_upper_factor=ttl_budget_trace_upper_factor, bfl_surface_index=bfl_surface_index, bfl_lower_factor=bfl_lower_factor, bfl_upper_factor=bfl_upper_factor)


def write_population_csv(path: Path, rows: list[list[float]]) -> None:
    return _seed_io.write_population_csv(path, rows)


def read_population_csv(path: Path) -> list[list[float]]:
    return _seed_io.read_population_csv(path)


def variable_bounds(variable: dict) -> tuple[float, float]:
    return _seed_coordinates.variable_bounds(variable)


def effective_variable_count(variables: list[dict]) -> int:
    return _seed_sizing.effective_variable_count(variables)


def population_sizing_variable_count(variables: list[dict]) -> int:
    return _seed_sizing.population_sizing_variable_count(variables)


def population_from_effective_popsize(variables: list[dict], popsize: object, fallback_popsize: float, minimum: int=1) -> int:
    return _seed_sizing.population_from_effective_popsize(variables, popsize, fallback_popsize, minimum)


def round_to_multiple(value: float, multiple: int) -> int:
    return _seed_sizing.round_to_multiple(value, multiple)


def adaptive_initial_search_basin_counts(options: dict, global_population: int) -> dict[str, int]:
    return _seed_sizing.adaptive_initial_search_basin_counts(options, global_population)


def local_funnel_perturb_value(system: dict, variable: dict, value: float, rng: random.Random, scale: float) -> float:
    return _seed_local.local_funnel_perturb_value(system, variable, value, rng, scale)


def make_local_funnel_population(system: dict, variables: list[dict], values: list[float], population: int, seed: int, scale: float) -> list[list[float]]:
    return _seed_local.make_local_funnel_population(system, variables, values, population, seed, scale)


def read_population_scores_csv(path: Path) -> list[tuple[int, float]]:
    return _seed_io.read_population_scores_csv(path)


