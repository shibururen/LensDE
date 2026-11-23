"""Generate hybrid seeds through explicit preparation, sampling and finishing phases."""
from __future__ import annotations
import random
from .. import configuration as _lensde_configuration
from .. import fields as _lensde_fields
from .. import geometry as _lensde_geometry
from .. import initialization as _lensde_initialization
from . import coordinates as _seed_coordinates
from . import domains as _seed_domains
from . import variables as _seed_variables
from . import thickness as _seed_thickness
from . import materials as _seed_materials

def _make_hybrid_initial_seed_population(system: dict, variables: list[dict], front_pairs: list[tuple[int, int]], population: int, seed: int, target_ttl: float, target_bfl: float, rest_cap: float, seed_curvature_cap: float, population_generation_mode: str='random', front_profile: str='weak', seed_air_thickness_cap: float=25.0, seed_glass_thickness_cap: float=2.0, seed_material_nd_cap: float=0.0, seed_material_vd_cap: float=0.0, local_group_perturbation: bool=False, local_long_air_gap_prior: bool=False, perturb_material_variables: bool=True, perturb_other_variables: bool=False, target_efl: float=0.0, auto_ttl_thickness_budget: bool=True, ttl_budget_trigger_ratio: float=1.15, ttl_budget_compact_slack_fraction: float=0.55, ttl_budget_lower_factor: float=0.9, ttl_budget_upper_factor: float=1.03, ttl_budget_trace_upper_factor: float=1.2, seed_ttl_allow_short: bool=True, seed_ttl_max_factor: float=1.0, bfl_surface_index: int | None=None, bfl_lower_factor: float=0.9, bfl_upper_factor: float=1.5, thickness_sampling_mode: str='legacy', budget_simplex_ttl_lower_fraction: float=0.9, budget_simplex_ttl_upper_fraction: float=1.0, budget_simplex_bfl_lower_factor: float=0.9, budget_simplex_bfl_upper_factor: float=1.0, budget_simplex_bfl_ttl_upper_fraction: float=0.0, budget_simplex_glass_beta_alpha: float=2.0, budget_simplex_glass_beta_beta: float=4.0, budget_simplex_air_dirichlet_alpha: float=2.0, stratified_bfl_sampling: bool=False, stratified_bfl_lower_factor: float=1.0, stratified_bfl_upper_factor: float=1.0, asphere_profile_metadata: dict[tuple[float, ...], dict] | None=None, apply_profiled_asphere_population: bool=True, reproject_local_thickness_rows: bool=True, *, _physics_generator) -> list[list[float]]:
    return _HybridSeedBuilder(system, variables, front_pairs, population, seed, target_ttl, target_bfl, rest_cap, seed_curvature_cap, population_generation_mode, front_profile, seed_air_thickness_cap, seed_glass_thickness_cap, seed_material_nd_cap, seed_material_vd_cap, local_group_perturbation, local_long_air_gap_prior, perturb_material_variables, perturb_other_variables, target_efl, auto_ttl_thickness_budget, ttl_budget_trigger_ratio, ttl_budget_compact_slack_fraction, ttl_budget_lower_factor, ttl_budget_upper_factor, ttl_budget_trace_upper_factor, seed_ttl_allow_short, seed_ttl_max_factor, bfl_surface_index, bfl_lower_factor, bfl_upper_factor, thickness_sampling_mode, budget_simplex_ttl_lower_fraction, budget_simplex_ttl_upper_fraction, budget_simplex_bfl_lower_factor, budget_simplex_bfl_upper_factor, budget_simplex_bfl_ttl_upper_fraction, budget_simplex_glass_beta_alpha, budget_simplex_glass_beta_beta, budget_simplex_air_dirichlet_alpha, stratified_bfl_sampling, stratified_bfl_lower_factor, stratified_bfl_upper_factor, asphere_profile_metadata, apply_profiled_asphere_population, reproject_local_thickness_rows, _physics_generator).build()

class _HybridSeedBuilder:
    """Own one hybrid-seed invocation, its RNG and persistent sampling context."""

    def __init__(self, system, variables, front_pairs, population, seed, target_ttl, target_bfl, rest_cap, seed_curvature_cap, population_generation_mode, front_profile, seed_air_thickness_cap, seed_glass_thickness_cap, seed_material_nd_cap, seed_material_vd_cap, local_group_perturbation, local_long_air_gap_prior, perturb_material_variables, perturb_other_variables, target_efl, auto_ttl_thickness_budget, ttl_budget_trigger_ratio, ttl_budget_compact_slack_fraction, ttl_budget_lower_factor, ttl_budget_upper_factor, ttl_budget_trace_upper_factor, seed_ttl_allow_short, seed_ttl_max_factor, bfl_surface_index, bfl_lower_factor, bfl_upper_factor, thickness_sampling_mode, budget_simplex_ttl_lower_fraction, budget_simplex_ttl_upper_fraction, budget_simplex_bfl_lower_factor, budget_simplex_bfl_upper_factor, budget_simplex_bfl_ttl_upper_fraction, budget_simplex_glass_beta_alpha, budget_simplex_glass_beta_beta, budget_simplex_air_dirichlet_alpha, stratified_bfl_sampling, stratified_bfl_lower_factor, stratified_bfl_upper_factor, asphere_profile_metadata, apply_profiled_asphere_population, reproject_local_thickness_rows, _physics_generator):
        self.system = system
        self.variables = variables
        self.front_pairs = front_pairs
        self.population = population
        self.seed = seed
        self.target_ttl = target_ttl
        self.target_bfl = target_bfl
        self.rest_cap = rest_cap
        self.seed_curvature_cap = seed_curvature_cap
        self.population_generation_mode = population_generation_mode
        self.front_profile = front_profile
        self.seed_air_thickness_cap = seed_air_thickness_cap
        self.seed_glass_thickness_cap = seed_glass_thickness_cap
        self.seed_material_nd_cap = seed_material_nd_cap
        self.seed_material_vd_cap = seed_material_vd_cap
        self.local_group_perturbation = local_group_perturbation
        self.local_long_air_gap_prior = local_long_air_gap_prior
        self.perturb_material_variables = perturb_material_variables
        self.perturb_other_variables = perturb_other_variables
        self.target_efl = target_efl
        self.auto_ttl_thickness_budget = auto_ttl_thickness_budget
        self.ttl_budget_trigger_ratio = ttl_budget_trigger_ratio
        self.ttl_budget_compact_slack_fraction = ttl_budget_compact_slack_fraction
        self.ttl_budget_lower_factor = ttl_budget_lower_factor
        self.ttl_budget_upper_factor = ttl_budget_upper_factor
        self.ttl_budget_trace_upper_factor = ttl_budget_trace_upper_factor
        self.seed_ttl_allow_short = seed_ttl_allow_short
        self.seed_ttl_max_factor = seed_ttl_max_factor
        self.bfl_surface_index = bfl_surface_index
        self.bfl_lower_factor = bfl_lower_factor
        self.bfl_upper_factor = bfl_upper_factor
        self.thickness_sampling_mode = thickness_sampling_mode
        self.budget_simplex_ttl_lower_fraction = budget_simplex_ttl_lower_fraction
        self.budget_simplex_ttl_upper_fraction = budget_simplex_ttl_upper_fraction
        self.budget_simplex_bfl_lower_factor = budget_simplex_bfl_lower_factor
        self.budget_simplex_bfl_upper_factor = budget_simplex_bfl_upper_factor
        self.budget_simplex_bfl_ttl_upper_fraction = budget_simplex_bfl_ttl_upper_fraction
        self.budget_simplex_glass_beta_alpha = budget_simplex_glass_beta_alpha
        self.budget_simplex_glass_beta_beta = budget_simplex_glass_beta_beta
        self.budget_simplex_air_dirichlet_alpha = budget_simplex_air_dirichlet_alpha
        self.stratified_bfl_sampling = stratified_bfl_sampling
        self.stratified_bfl_lower_factor = stratified_bfl_lower_factor
        self.stratified_bfl_upper_factor = stratified_bfl_upper_factor
        self.asphere_profile_metadata = asphere_profile_metadata
        self.apply_profiled_asphere_population = apply_profiled_asphere_population
        self.reproject_local_thickness_rows = reproject_local_thickness_rows
        self._physics_generator = _physics_generator

    def _configure(self):
        # Create the invocation RNG once before deriving sampling state.
        self.rng = random.Random(self.seed)
        self.surfaces = self.system.get('surfaces', []) or []
        self.all_pairs = _lensde_fields.glass_pairs(self.system)
        self.rear_start = max((pair[1] for pair in self.front_pairs), default=0)
        self.pairs = [pair for pair in self.all_pairs if pair[0] > self.rear_start]
        self.by_key = {_lensde_geometry.variable_key(variable): variable for variable in self.variables}
        self.thickness_vars = [variable for variable in self.variables if variable.get('kind') == 'thickness']
        self.restricted_seed = _lensde_configuration.canonical_population_generation_mode(self.population_generation_mode, 'random') == 'restricted_perturbation'
        self.seed_air_thickness_cap = max(0.0, _lensde_configuration.finite_float(self.seed_air_thickness_cap, 25.0))
        self.seed_glass_thickness_cap = max(0.0, _lensde_configuration.finite_float(self.seed_glass_thickness_cap, 2.0))
        self.seed_material_nd_cap = max(0.0, _lensde_configuration.finite_float(self.seed_material_nd_cap, 0.0))
        self.seed_material_vd_cap = max(0.0, _lensde_configuration.finite_float(self.seed_material_vd_cap, 0.0))
        self.local_group_perturbation = bool(self.local_group_perturbation)
        self.local_long_air_gap_prior = bool(self.local_long_air_gap_prior)
        self.perturb_material_variables = bool(self.perturb_material_variables)
        self.perturb_other_variables = bool(self.perturb_other_variables)
        self.front_pair_set = {(int(front), int(rear)) for front, rear in self.front_pairs}
        self.front_front_range, self.front_rear_range = _seed_variables.front_prior_ranges(self.front_profile)
        self.local_pairs = self.pairs or [pair for pair in self.all_pairs if (int(pair[0]), int(pair[1])) not in self.front_pair_set] or self.all_pairs
        self.use_local_group = self.restricted_seed and self.local_group_perturbation and bool(self.local_pairs)
        self.local_pair_offset = self.rng.randrange(len(self.local_pairs)) if self.use_local_group else 0
        self.last_thickness_index = max(0, len(self.surfaces) - 2)
        self.bfl_indices = _lensde_geometry.bfl_interval_indices(self.system, self.bfl_surface_index)
        self.thickness_sampling_mode = _seed_thickness.canonical_initial_thickness_sampling_mode(self.thickness_sampling_mode)
        self.budget_simplex_mode = self.thickness_sampling_mode == 'budget_simplex'
        _, self.length_longness = _lensde_geometry.ttl_efl_longness(self.target_ttl, self.target_efl)
        self.ttl_projection_config = _seed_thickness.ttl_seed_thickness_budget_config(self.system, self.variables, self.target_ttl, self.target_bfl, self.target_efl, enabled=self.auto_ttl_thickness_budget, trigger_ratio=self.ttl_budget_trigger_ratio, compact_slack_fraction=self.ttl_budget_compact_slack_fraction, lower_factor=self.ttl_budget_lower_factor, upper_factor=self.ttl_budget_upper_factor, trace_upper_factor=self.ttl_budget_trace_upper_factor, allow_short_seed=self.seed_ttl_allow_short, seed_max_factor=self.seed_ttl_max_factor, bfl_surface_index=self.bfl_surface_index, bfl_lower_factor=self.bfl_lower_factor, bfl_upper_factor=self.bfl_upper_factor)

    def _initialize_geometry(self):
        self.front_surface_indices: set[int] = set()
        self.front_thickness_indices: set[int] = set()
        for pair in self.front_pairs:
            self.front_surface_indices.update(self.pair_surface_indices(pair))
            self.front_thickness_indices.update(self.pair_thickness_indices(pair, include_before=False))

    def pair_surface_indices(self, pair: tuple[int, int]) -> set[int]:
        front, rear = pair
        lo = max(0, min(int(front), int(rear)))
        hi = min(max(0, len(self.surfaces) - 1), max(int(front), int(rear)))
        return set(range(lo, hi + 1)) if hi >= lo else set()

    def pair_thickness_indices(self, pair: tuple[int, int], *, include_before: bool) -> set[int]:
        front, rear = pair
        lo = int(front) - 1 if include_before else int(front)
        hi = int(rear)
        lo = max(0, lo)
        hi = min(self.last_thickness_index, hi)
        return set(range(lo, hi + 1)) if hi >= lo else set()

    def local_variable_active(self, variable: dict, selected_pair: tuple[int, int] | None) -> bool:
        if not self.use_local_group or selected_pair is None:
            return True
        kind = str(variable.get('kind', ''))
        index = int(variable.get('index', 0) or 0)
        selected_surfaces = self.pair_surface_indices(selected_pair)
        selected_thickness = self.pair_thickness_indices(selected_pair, include_before=True)
        if kind == 'thickness':
            return index in self.front_thickness_indices or index in selected_thickness
        if kind in {'curvature', 'material_nd', 'material_vd', 'glass_choice', 'conic', 'asphere_coeff'}:
            return index in self.front_surface_indices or index in selected_surfaces
        return False

    def front_prior_curvature_bounds(self, variable: dict) -> tuple[float, float] | None:
        index = int(variable.get('index', 0) or 0)
        prior_range: tuple[float, float] | None = None
        for front, rear in self.front_pairs:
            if index == int(front):
                prior_range = self.front_front_range
                break
            if index == int(rear):
                prior_range = self.front_rear_range
                break
        if prior_range is None:
            return None
        lo, hi = self.curvature_seed_bounds(variable)
        prior_lo, prior_hi = prior_range
        clipped_lo = max(lo, min(prior_lo, prior_hi))
        clipped_hi = min(hi, max(prior_lo, prior_hi))
        if clipped_lo <= clipped_hi:
            return (clipped_lo, clipped_hi)
        edge = _seed_coordinates.clamp_value(0.5 * (prior_lo + prior_hi), lo, hi)
        return (edge, edge)

    def curvature_seed_bounds(self, variable: dict) -> tuple[float, float]:
        lo = _lensde_configuration.finite_float(variable.get('lower'), -self.rest_cap)
        hi = _lensde_configuration.finite_float(variable.get('upper'), self.rest_cap)
        if lo > hi:
            lo, hi = (hi, lo)
        cap = abs(_lensde_configuration.finite_float(self.seed_curvature_cap, 0.0))
        if cap <= 0.0:
            return (lo, hi)
        clipped_lo = max(lo, -cap)
        clipped_hi = min(hi, cap)
        if clipped_lo <= clipped_hi:
            return (clipped_lo, clipped_hi)
        edge = lo if lo > cap else hi
        return (edge, edge)

    def maybe_apply_long_air_gap_pattern(self, values: dict[tuple[str, int, int], float], *, force: bool=False) -> bool:
        if self.target_ttl <= 0.0 or self.length_longness <= 0.05:
            return False
        probability = min(0.55, 0.1 + 0.38 * self.length_longness)
        if not force and self.rng.random() > probability:
            return False
        flexible = []
        for variable in self.thickness_vars:
            index = int(variable.get('index', 0) or 0)
            if index in self.bfl_indices:
                continue
            if _seed_domains.surface_after_is_glass(self.system, index):
                continue
            lo = _lensde_configuration.finite_float(variable.get('lower'), _seed_coordinates.variable_current_value_for_system(self.system, variable))
            hi = _lensde_configuration.finite_float(variable.get('upper'), lo)
            if hi > lo + 1e-09:
                flexible.append(variable)
        if not flexible:
            return False
        base_total = _seed_thickness.fixed_track_length(self.system, self.variables)
        flexible_keys = {_lensde_geometry.variable_key(variable) for variable in flexible}
        base_values: dict[tuple[str, int, int], float] = {}
        for variable in self.thickness_vars:
            key = _lensde_geometry.variable_key(variable)
            lo = _lensde_configuration.finite_float(variable.get('lower'), _seed_coordinates.variable_current_value_for_system(self.system, variable))
            hi = _lensde_configuration.finite_float(variable.get('upper'), lo)
            current = values.get(key, _seed_coordinates.variable_current_value_for_system(self.system, variable))
            if key in flexible_keys:
                current = lo
            current = _seed_coordinates.clamp_value(current, lo, hi)
            base_values[key] = current
            base_total += current
        target_low = max(base_total, self.target_ttl * (0.55 + 0.1 * self.length_longness))
        target_high = max(target_low, self.target_ttl * (0.86 + 0.1 * self.length_longness))
        target_total = min(self.target_ttl * 1.02, self.rng.uniform(target_low, target_high))
        extra = max(0.0, target_total - base_total)
        if extra <= 1e-09:
            return False
        max_long_gaps = min(len(flexible), 1 + int(round(2.0 * self.length_longness)))
        long_gap_count = max(1, min(max_long_gaps, 1 + int(self.rng.random() < 0.45 * self.length_longness)))
        selected = self.rng.sample(flexible, k=long_gap_count)
        selected_keys = {_lensde_geometry.variable_key(variable) for variable in selected}
        major_share = min(0.92, 0.58 + 0.28 * self.length_longness)
        selected_extra = extra * major_share
        residual_extra = extra - selected_extra

        def distribute(candidates: list[dict], amount: float, *, sharp: bool) -> float:
            if amount <= 1e-09 or not candidates:
                return amount
            weights = [(0.05 + self.rng.random()) ** (0.45 if sharp else 1.0) for _ in candidates]
            remaining = amount
            active = list(candidates)
            active_weights = list(weights)
            for _pass in range(5):
                if remaining <= 1e-09 or not active:
                    break
                wsum = sum(active_weights)
                next_active: list[dict] = []
                next_weights: list[float] = []
                for variable, weight in zip(active, active_weights, strict=False):
                    key = _lensde_geometry.variable_key(variable)
                    cur = base_values.get(key, _seed_coordinates.variable_current_value_for_system(self.system, variable))
                    hi = _lensde_configuration.finite_float(variable.get('upper'), cur)
                    room = max(0.0, hi - cur)
                    add = min(room, remaining * weight / wsum) if wsum > 0.0 else 0.0
                    base_values[key] = cur + add
                    remaining -= add
                    if room > add + 1e-09:
                        next_active.append(variable)
                        next_weights.append(weight)
                active = next_active
                active_weights = next_weights
            return remaining
        residual_extra += distribute(selected, selected_extra, sharp=True)
        distribute([variable for variable in flexible if _lensde_geometry.variable_key(variable) not in selected_keys], residual_extra, sharp=False)
        values.update(base_values)
        return True

    def maybe_apply_conservative_local_long_air_gap_pattern(self, values: dict[tuple[str, int, int], float]) -> bool:
        "Inject a long air gap without borrowing length from glass or TTL.\n\n        The legacy global prior starts every non-selected air interval at its\n        hard lower bound.  That is useful for broad long-system topology\n        discovery, but it creates exactly the glass-dominated seeds this local\n        ablation is intended to avoid.  Here the projected mother's total air\n        budget is a hard floor, and the selected long gap is funded only by a\n        mild redistribution among the remaining air intervals.\n        "
        if self.target_ttl <= 0.0 or self.length_longness <= 0.05:
            return False
        probability = min(0.55, 0.1 + 0.38 * self.length_longness)
        if self.rng.random() > probability:
            return False
        flexible: list[dict] = []
        for variable in self.thickness_vars:
            index = int(variable.get('index', 0) or 0)
            if index in self.bfl_indices or _seed_domains.surface_after_is_glass(self.system, index):
                continue
            lo = _lensde_configuration.finite_float(variable.get('lower'), _seed_coordinates.variable_current_value_for_system(self.system, variable))
            hi = _lensde_configuration.finite_float(variable.get('upper'), lo)
            if hi > lo + 1e-09:
                flexible.append(variable)
        if len(flexible) < 2:
            return False
        mother_values = {_lensde_geometry.variable_key(variable): _seed_coordinates.clamp_value(self.initial_values.get(_lensde_geometry.variable_key(variable), _seed_coordinates.variable_current_value_for_system(self.system, variable)), _lensde_configuration.finite_float(variable.get('lower'), 0.0), _lensde_configuration.finite_float(variable.get('upper'), 0.0)) for variable in flexible}
        mother_air_total = sum(mother_values.values())
        current_air_total = sum((_seed_coordinates.clamp_value(values.get(_lensde_geometry.variable_key(variable), mother_values[_lensde_geometry.variable_key(variable)]), _lensde_configuration.finite_float(variable.get('lower'), 0.0), _lensde_configuration.finite_float(variable.get('upper'), 0.0)) for variable in flexible))
        air_budget = max(mother_air_total, current_air_total)
        average_air = air_budget / len(flexible)
        selected = self.rng.choice(flexible)
        selected_key = _lensde_geometry.variable_key(selected)
        selected_lo = _lensde_configuration.finite_float(selected.get('lower'), 0.0)
        selected_hi = _lensde_configuration.finite_float(selected.get('upper'), selected_lo)
        other_variables = [variable for variable in flexible if variable is not selected]
        soft_lowers = {_lensde_geometry.variable_key(variable): max(_lensde_configuration.finite_float(variable.get('lower'), 0.0), 0.45 * mother_values[_lensde_geometry.variable_key(variable)]) for variable in other_variables}
        other_soft_lower_total = sum(soft_lowers.values())
        desired_long = max(mother_values[selected_key] * 1.6, average_air * self.rng.uniform(1.8, 2.6 + 0.4 * self.length_longness))
        long_value = _seed_coordinates.clamp_value(min(desired_long, air_budget - other_soft_lower_total), selected_lo, selected_hi)
        if long_value <= mother_values[selected_key] * 1.2:
            return False
        values[selected_key] = long_value
        remaining = air_budget - long_value
        level_lo = min(soft_lowers.values())
        level_hi = max((_lensde_configuration.finite_float(variable.get('upper'), level_lo) for variable in other_variables))
        for _ in range(80):
            level = 0.5 * (level_lo + level_hi)
            allocated = sum((_seed_coordinates.clamp_value(level, soft_lowers[_lensde_geometry.variable_key(variable)], _lensde_configuration.finite_float(variable.get('upper'), level)) for variable in other_variables))
            if allocated < remaining:
                level_lo = level
            else:
                level_hi = level
        level = 0.5 * (level_lo + level_hi)
        for variable in other_variables:
            key = _lensde_geometry.variable_key(variable)
            values[key] = _seed_coordinates.clamp_value(level, soft_lowers[key], _lensde_configuration.finite_float(variable.get('upper'), level))
        return True

    def row_from_values(self, values: dict[tuple[str, int, int], float]) -> list[float]:
        row = []
        for variable in self.variables:
            lo = _lensde_configuration.finite_float(variable.get('lower'), 0.0)
            hi = _lensde_configuration.finite_float(variable.get('upper'), lo)
            row.append(_seed_coordinates.clamp_value(values.get(_lensde_geometry.variable_key(variable), _lensde_configuration.finite_float(variable.get('initial'), 0.0)), lo, hi))
        return row

    def stratified_quantiles(self, count: int) -> list[float]:
        quantiles = [(index + self.rng.random()) / max(1, count) for index in range(max(0, count))]
        self.rng.shuffle(quantiles)
        return quantiles

    def finish_population(self, candidate_rows: list[list[float]]) -> list[list[float]]:
        if not self.apply_profiled_asphere_population:
            return candidate_rows
        profile_scales = [_lensde_configuration.finite_float(variable.get('initial_search_asphere_profile_scale'), 1.0) for variable in self.variables if bool(variable.get('initial_search_asphere_profiled', False))]
        return _lensde_initialization.make_profiled_initial_asphere_population(candidate_rows, self.variables, self.seed, profile_scales[0] if profile_scales else 1.0, profile_metadata=self.asphere_profile_metadata)

    def _initialize_anchor(self):
        # Prepare the anchor and material plan before generating stratified quantiles.
        self.initial_values: dict[tuple[str, int, int], float] = {}
        for variable in self.variables:
            if variable.get('kind') == 'curvature':
                prior_bounds = self.front_prior_curvature_bounds(variable) if self.restricted_seed else None
                lo, hi = prior_bounds if prior_bounds is not None else self.curvature_seed_bounds(variable)
                initial_value = 0.5 * (lo + hi) if prior_bounds is not None else _seed_coordinates.variable_current_value_for_system(self.system, variable, _lensde_configuration.finite_float(variable.get('initial'), 0.0))
            else:
                lo = _lensde_configuration.finite_float(variable.get('lower'), _lensde_configuration.finite_float(variable.get('initial'), 0.0))
                hi = _lensde_configuration.finite_float(variable.get('upper'), _lensde_configuration.finite_float(variable.get('initial'), 0.0))
                initial_value = _seed_coordinates.variable_current_value_for_system(self.system, variable, _lensde_configuration.finite_float(variable.get('initial'), 0.0))
                if _seed_domains.is_bfl_anchor_variable(self.system, variable, self.bfl_surface_index):
                    bfl_anchor = _seed_domains.bfl_anchor_seed_target(self.system, self.target_bfl, self.bfl_surface_index)
                    if bfl_anchor is not None:
                        initial_value = bfl_anchor
            self.initial_values[_lensde_geometry.variable_key(variable)] = _seed_coordinates.clamp_value(initial_value, lo, hi)
        if self.perturb_material_variables:
            self.initial_values = _seed_materials.project_material_anchor_to_catalog(self.system, self.variables, self.initial_values)
        _seed_thickness.project_thickness_values_to_ttl_budget(self.system, self.variables, self.initial_values, self.ttl_projection_config, self.rng, uniform_non_bfl_when_contracting=self.restricted_seed and self.use_local_group)
        self.material_seed_sampling_plan = _seed_materials.prepare_catalog_material_seed_sampling_plan(self.system, self.variables, current_values=self.initial_values, nd_cap=self.seed_material_nd_cap, vd_cap=self.seed_material_vd_cap) if self.perturb_material_variables else None
        if _seed_materials.material_seed_region_diversity_applicable(self.material_seed_sampling_plan, 2):
            self.initial_values.update(_seed_materials.sample_catalog_material_seed_values(self.system, self.variables, self.rng, 0, prepared_plan=self.material_seed_sampling_plan))
        self.initial = self.row_from_values(self.initial_values)
        self.rows = [] if self.budget_simplex_mode else [self.initial]

    def _initialize_quantiles(self):
        self.generated_row_count = max(0, self.population) if self.budget_simplex_mode else max(0, self.population - 1)
        self.ttl_quantiles = self.stratified_quantiles(self.generated_row_count) if self.budget_simplex_mode else []
        self.bfl_quantiles = self.stratified_quantiles(self.generated_row_count) if self.budget_simplex_mode else []
        self.stratified_bfl_sampling = bool(self.stratified_bfl_sampling) and bool([variable for variable in self.thickness_vars if int(variable.get('index', 0) or 0) in self.bfl_indices])
        self.independent_bfl_quantiles = self.stratified_quantiles(self.generated_row_count) if self.stratified_bfl_sampling and (not self.budget_simplex_mode) else []
        self.bfl_variables = [variable for variable in self.thickness_vars if int(variable.get('index', 0) or 0) in self.bfl_indices]
        self.fixed_bfl_track = sum((max(0.0, _lensde_configuration.finite_float(surface.get('thickness'), 0.0)) for index, surface in enumerate(self.surfaces[:-1]) if index in self.bfl_indices and (not any((int(variable.get('index', 0) or 0) == index for variable in self.bfl_variables)))))

    def _sample_curvatures(self, values, selected_pair):
        # Curvature draws precede every thickness/material draw.
        for variable in self.variables:
            if variable.get('kind') != 'curvature':
                continue
            if self.use_local_group and (not self.local_variable_active(variable, selected_pair)):
                continue
            prior_bounds = self.front_prior_curvature_bounds(variable) if self.restricted_seed else None
            lo, hi = prior_bounds if prior_bounds is not None else self.curvature_seed_bounds(variable)
            values[_lensde_geometry.variable_key(variable)] = lo + (hi - lo) * self.rng.random()

    def _sample_thickness(self, values, selected_pair, mutable_thickness_keys):
        # Keep the original variable traversal and restricted/local exclusions.
        for variable in self.thickness_vars:
            if self.budget_simplex_mode:
                continue
            key = _lensde_geometry.variable_key(variable)
            lo = _lensde_configuration.finite_float(variable.get('lower'), _seed_coordinates.variable_current_value_for_system(self.system, variable))
            hi = _lensde_configuration.finite_float(variable.get('upper'), lo)
            index = int(variable.get('index', 0) or 0)
            if self.stratified_bfl_sampling and index in self.bfl_indices:
                continue
            if self.restricted_seed:
                if self.use_local_group and (not self.local_variable_active(variable, selected_pair)):
                    continue
                current = _seed_coordinates.clamp_value(values.get(key, self.initial_values.get(key, lo)), lo, hi)
                cap = self.seed_glass_thickness_cap if 0 <= index < len(self.surfaces) and _seed_domains.surface_after_is_glass(self.system, index) else self.seed_air_thickness_cap
                seed_lo = max(lo, current - cap)
                seed_hi = min(hi, current + cap)
                values[key] = seed_lo + (seed_hi - seed_lo) * self.rng.random() if seed_hi > seed_lo else current
            elif 0 <= index < len(self.surfaces) and _seed_domains.surface_after_is_glass(self.system, index):
                values[key] = lo + (hi - lo) * self.rng.random()
            else:
                values[key] = lo + (hi - lo) * self.rng.random() ** 1.4
            mutable_thickness_keys.add(key)

    def _sample_bfl(self, values, mutable_thickness_keys, row_index):
        # Consume the precomputed quantile before allocating BFL members.
        if self.stratified_bfl_sampling and self.bfl_variables:
            member_lower = sum((_seed_coordinates.variable_bounds(variable)[0] for variable in self.bfl_variables))
            member_upper = sum((_seed_coordinates.variable_bounds(variable)[1] for variable in self.bfl_variables))
            requested_lower = self.target_bfl * max(0.0, _lensde_configuration.finite_float(self.stratified_bfl_lower_factor, 1.0)) if self.target_bfl > 0.0 else self.fixed_bfl_track + member_lower
            requested_upper = self.target_bfl * max(max(0.0, _lensde_configuration.finite_float(self.stratified_bfl_lower_factor, 1.0)), _lensde_configuration.finite_float(self.stratified_bfl_upper_factor, 1.0)) if self.target_bfl > 0.0 else self.fixed_bfl_track + member_upper
            sampled_lower = max(self.fixed_bfl_track + member_lower, requested_lower)
            sampled_upper = min(self.fixed_bfl_track + member_upper, requested_upper)
            if sampled_upper < sampled_lower - 1e-09:
                raise ValueError(f'stratified BFL sampling window is infeasible: requested=[{requested_lower:.12g},{requested_upper:.12g}], feasible=[{self.fixed_bfl_track + member_lower:.12g},{self.fixed_bfl_track + member_upper:.12g}]')
            quantile = self.independent_bfl_quantiles[row_index]
            sampled_total = sampled_lower + (sampled_upper - sampled_lower) * quantile
            sampled_values = _seed_thickness.allocate_bounded_thickness_total(self.bfl_variables, sampled_total - self.fixed_bfl_track, self.rng, 2.0)
            if sampled_values is None:
                raise ValueError(f'stratified BFL member allocation failed: sampled_total={sampled_total:.12g}')
            values.update(sampled_values)
            mutable_thickness_keys.update(sampled_values)

    def _sample_materials(self, values, row_index):
        # Catalog selection uses the same prepared plan and RNG instance.
        if self.perturb_material_variables:
            values.update(_seed_materials.sample_catalog_material_seed_values(self.system, self.variables, self.rng, row_index, current_values=self.initial_values, nd_cap=self.seed_material_nd_cap, vd_cap=self.seed_material_vd_cap, variable_active=None, prepared_plan=self.material_seed_sampling_plan))

    def _redistribute_air(self, values):
        # Apply the long-gap policy before optional extra-air allocation.
        long_gap_applied = False if self.budget_simplex_mode else self.maybe_apply_conservative_local_long_air_gap_pattern(values) if self.restricted_seed and self.use_local_group and self.local_long_air_gap_prior else self.maybe_apply_long_air_gap_pattern(values) if not (self.restricted_seed and self.use_local_group) else False
        current_total = _seed_thickness.fixed_track_length(self.system, self.variables) + sum((values.get(_lensde_geometry.variable_key(v), _seed_coordinates.variable_current_value_for_system(self.system, v)) for v in self.thickness_vars))
        if not self.budget_simplex_mode and (not long_gap_applied) and (not self.restricted_seed) and (self.target_ttl > 0.0) and (current_total < 0.95 * self.target_ttl):
            flexible = [v for v in self.thickness_vars if not _seed_domains.surface_after_is_glass(self.system, int(v.get('index', 0) or 0))]
            extra = min(max(0.0, self.target_ttl * self.rng.uniform(0.82, 1.02) - current_total), self.target_ttl)
            weights = [0.05 + self.rng.random() for _ in flexible]
            for _pass in range(5):
                if extra <= 1e-09 or not flexible:
                    break
                wsum = sum(weights)
                next_flexible = []
                next_weights = []
                for variable, weight in zip(flexible, weights, strict=False):
                    key = _lensde_geometry.variable_key(variable)
                    cur = values.get(key, _seed_coordinates.variable_current_value_for_system(self.system, variable))
                    hi = _lensde_configuration.finite_float(variable.get('upper'), cur)
                    room = max(0.0, hi - cur)
                    add = min(room, extra * weight / wsum) if wsum > 0.0 else 0.0
                    values[key] = cur + add
                    extra -= add
                    if room > add + 1e-09:
                        next_flexible.append(variable)
                        next_weights.append(weight)
                flexible = next_flexible
                weights = next_weights

    def _sample_other_coordinates(self, values, selected_pair):
        # Retain vignetting, discrete glass and restricted-coordinate branch order.
        for variable in self.variables:
            key = _lensde_geometry.variable_key(variable)
            kind = str(variable.get('kind', ''))
            resample_vignetting = self.restricted_seed and _lensde_configuration.is_vignetting_variable(variable)
            if key in values:
                resample_restricted_other = self.restricted_seed and self.perturb_other_variables and (kind not in {'curvature', 'thickness', 'material_nd', 'material_vd'}) and (not self.use_local_group or self.local_variable_active(variable, selected_pair))
                if not (resample_restricted_other or resample_vignetting):
                    continue
            hard_lo = _lensde_configuration.finite_float(variable.get('lower'), _lensde_configuration.finite_float(variable.get('initial'), 0.0))
            hard_hi = _lensde_configuration.finite_float(variable.get('upper'), hard_lo)
            if hard_lo > hard_hi:
                hard_lo, hard_hi = (hard_hi, hard_lo)
            lo = max(hard_lo, _lensde_configuration.finite_float(variable.get('seed_lower'), hard_lo))
            hi = min(hard_hi, _lensde_configuration.finite_float(variable.get('seed_upper'), hard_hi))
            if lo > hi:
                lo, hi = (hard_lo, hard_hi)
            if self.restricted_seed and self.use_local_group and (not self.local_variable_active(variable, selected_pair)) and (not _lensde_configuration.is_vignetting_variable(variable)):
                values[key] = _seed_coordinates.clamp_value(_seed_coordinates.variable_current_value_for_system(self.system, variable, _lensde_configuration.finite_float(variable.get('initial'), 0.0)), lo, hi)
            elif kind in {'material_nd', 'material_vd'} and (not self.perturb_material_variables):
                values[key] = _seed_coordinates.clamp_value(_seed_coordinates.variable_current_value_for_system(self.system, variable, _lensde_configuration.finite_float(variable.get('initial'), 0.0)), lo, hi)
            elif self.restricted_seed and self.perturb_other_variables:
                if kind == 'glass_choice':
                    lo_i = int(round(lo))
                    hi_i = max(lo_i, int(round(hi)))
                    values[key] = float(self.rng.randint(lo_i, hi_i))
                else:
                    values[key] = lo + (hi - lo) * self.rng.random() if hi > lo else lo
            elif _lensde_configuration.is_vignetting_variable(variable):
                values[key] = lo + (hi - lo) * self.rng.random() if hi > lo else lo
            elif self.restricted_seed:
                values[key] = _seed_coordinates.clamp_value(_seed_coordinates.variable_current_value_for_system(self.system, variable, _lensde_configuration.finite_float(variable.get('initial'), 0.0)), lo, hi)
            elif kind == 'glass_choice':
                lo_i = int(round(lo))
                hi_i = max(lo_i, int(round(hi)))
                values[key] = float(self.rng.randint(lo_i, hi_i))
            else:
                values[key] = lo + (hi - lo) * self.rng.random() if hi > lo else lo

    def _finish_thickness(self, values, mutable_thickness_keys, row_index):
        # The simplex and projection paths retain their original exclusivity.
        if self.budget_simplex_mode:
            sampled_thickness, _sample_summary = _seed_thickness.sample_budget_simplex_thickness_values(self.system, self.variables, self.rng, self.target_ttl, self.target_bfl, ttl_quantile=self.ttl_quantiles[row_index], bfl_quantile=self.bfl_quantiles[row_index], ttl_lower_fraction=self.budget_simplex_ttl_lower_fraction, ttl_upper_fraction=self.budget_simplex_ttl_upper_fraction, bfl_lower_factor=self.budget_simplex_bfl_lower_factor, bfl_upper_factor=self.budget_simplex_bfl_upper_factor, bfl_ttl_upper_fraction=self.budget_simplex_bfl_ttl_upper_fraction, glass_beta_alpha=self.budget_simplex_glass_beta_alpha, glass_beta_beta=self.budget_simplex_glass_beta_beta, air_dirichlet_alpha=self.budget_simplex_air_dirichlet_alpha, bfl_surface_index=self.bfl_surface_index)
            values.update(sampled_thickness)
        elif not (self.restricted_seed and self.use_local_group and (not self.reproject_local_thickness_rows)):
            _seed_thickness.project_thickness_values_to_ttl_budget(self.system, self.variables, values, self.ttl_projection_config, self.rng, mutable_keys=mutable_thickness_keys)

    def _sample_row(self, row_index):
        selected_pair = self.local_pairs[(self.local_pair_offset + row_index) % len(self.local_pairs)] if self.use_local_group else None
        values: dict[tuple[str, int, int], float] = dict(self.initial_values) if self.restricted_seed else {}
        mutable_thickness_keys: set[tuple[str, int, int]] = set()
        self._sample_curvatures(values, selected_pair)
        self._sample_thickness(values, selected_pair, mutable_thickness_keys)
        self._sample_bfl(values, mutable_thickness_keys, row_index)
        self._sample_materials(values, row_index)
        self._redistribute_air(values)
        self._sample_other_coordinates(values, selected_pair)
        self._finish_thickness(values, mutable_thickness_keys, row_index)
        self.rows.append(self.row_from_values(values))

    def build(self):
        self._configure()
        self._initialize_geometry()
        self._initialize_anchor()
        self._initialize_quantiles()
        if _lensde_configuration.canonical_population_generation_mode(self.population_generation_mode, 'random') == 'physics_constrained':
            self.rows.extend(self._physics_generator(self.system, self.variables, max(0, self.population - 1), self.target_efl, self.target_ttl, self.target_bfl, self.seed + 1701, self.seed_curvature_cap, perturb_material_variables=self.perturb_material_variables, perturb_other_variables=self.perturb_other_variables, auto_ttl_thickness_budget=self.auto_ttl_thickness_budget, ttl_budget_trigger_ratio=self.ttl_budget_trigger_ratio, ttl_budget_compact_slack_fraction=self.ttl_budget_compact_slack_fraction, ttl_budget_lower_factor=self.ttl_budget_lower_factor, ttl_budget_upper_factor=self.ttl_budget_upper_factor, ttl_budget_trace_upper_factor=self.ttl_budget_trace_upper_factor, bfl_surface_index=self.bfl_surface_index, bfl_lower_factor=self.bfl_lower_factor, bfl_upper_factor=self.bfl_upper_factor))
            return self.finish_population(self.rows[:max(0, self.population)])
        for row_index in range(self.generated_row_count):
            self._sample_row(row_index)
        self.finished_rows = self.finish_population(self.rows)
        if self.perturb_material_variables:
            self.finished_rows = _seed_materials.enforce_material_seed_population_region_diversity(self.system, self.variables, self.finished_rows, self.rng, self.material_seed_sampling_plan, minimum_regions=2)
        return self.finished_rows
