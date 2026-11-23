"""Explicit native translation-unit registry and project dependency fingerprints.

Source discovery validates the registry; it never silently adds a source to a
build. Compiler options and the ordered host/device link inputs are separate.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re

GROUPS = ('core', 'optimizer', 'raytrace', 'analysis', 'io', 'cli', 'cuda_host', 'broker', 'cuda')
TARGETS = ('cpu', 'cuda', 'broker')


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _relative_source(value: str) -> str:
    if not isinstance(value, str) or '\\' in value:
        raise ValueError('Native sources must use project-relative forward-slash paths')
    parts = PurePosixPath(value).parts
    if (not parts or parts[0] not in {'src', 'cuda'} or
            any(part in {'', '.', '..'} or ':' in part for part in parts) or
            PurePosixPath(value).as_posix() != value or
            PurePosixPath(value).suffix not in {'.cpp', '.cu'}):
        raise ValueError('Invalid native source path: ' + value)
    return value


def load_registry(native_root: Path, *, validate_files: bool = True) -> dict:
    """Read the authoritative registry, rejecting duplicates and missing units."""
    root = Path(native_root).resolve()
    path = root / 'sources.json'
    registry = json.loads(path.read_text(encoding='utf-8-sig'))
    if registry.get('schema_version') != 1 or set(registry.get('groups', {})) != set(GROUPS):
        raise ValueError('Unexpected native source registry groups: ' + str(path))
    if set(registry.get('targets', {})) != set(TARGETS):
        raise ValueError('Unexpected native source registry targets: ' + str(path))
    seen = {}
    for group, sources in registry['groups'].items():
        if not isinstance(sources, list) or not sources:
            raise ValueError('Native source group must be a nonempty list: ' + group)
        for source in sources:
            source = _relative_source(source)
            identity = source.casefold()
            if identity in seen:
                raise ValueError(f'Duplicate native translation unit: {source} ({seen[identity]}, {group})')
            if group == 'cuda' and not source.endswith('.cu') or group != 'cuda' and not source.endswith('.cpp'):
                raise ValueError('Host/device source group mismatch: ' + source)
            seen[identity] = group
            if validate_files:
                current = (root / source).resolve()
                if not current.is_relative_to(root) or not current.is_file():
                    raise FileNotFoundError('Missing or escaped registered native source: ' + source)
    used_groups = set()
    for target, record in registry['targets'].items():
        if set(record) != {'host_groups', 'device_groups'}:
            raise ValueError('Expected explicit host_groups/device_groups for target: ' + target)
        for kind in ('host_groups', 'device_groups'):
            names = record[kind]
            if not isinstance(names, list) or len(names) != len(set(names)) or any(name not in GROUPS for name in names):
                raise ValueError(f'Invalid/duplicate group references: {target}/{kind}')
            if any((name == 'cuda') != (kind == 'device_groups') for name in names):
                raise ValueError(f'Host/device group references mismatch: {target}/{kind}')
            used_groups.update(names)
    if used_groups != set(GROUPS):
        raise ValueError('Unreferenced native groups: ' + str(sorted(set(GROUPS) - used_groups)))
    if validate_files:
        actual = {p.relative_to(root).as_posix().casefold()
                  for folder, suffix in (('src', '*.cpp'), ('cuda', '*.cu'))
                  for p in (root / folder).rglob(suffix) if p.is_file()}
        if actual != set(seen):
            raise ValueError('Native registry/source mismatch; missing registrations=' +
                             str(sorted(actual - set(seen))) + '; absent files=' + str(sorted(set(seen) - actual)))
    return registry


def _legacy_plan(root: Path, target: str) -> dict:
    """Explicit old audit recipe; never an implicit production fallback."""
    common = ['src/' + name + '.cpp' for name in
              ('core', 'raytrace', 'analysis', 'fileio', 'solves', 'optimizer', 'visualization')]
    host = common + ([] if target == 'cpu' else ['src/cuda_launch_config.cpp'])
    if target == 'broker':
        host += ['src/population_broker.cpp', 'src/multi_island_experiment_main.cpp']
    else:
        host += ['src/main.cpp']
        if (root / 'src/cli_arguments.cpp').is_file():
            host.append('src/cli_arguments.cpp')
    device = [] if target == 'cpu' else ['cuda/de_cuda_asphere_intersection.cu', 'cuda/de_cuda_scorer.cu']
    for relative in host + device:
        if not (root / relative).is_file():
            raise FileNotFoundError('Missing frozen legacy audit source: ' + relative)
    return {'schema_version': 1, 'target': target, 'host_sources': host,
            'device_sources': device, 'source_registry_sha256': None, 'legacy_audit_recipe': True}


def source_plan(native_root: Path, target: str, *, allow_legacy: bool = False,
                validate_files: bool = True) -> dict:
    root = Path(native_root).resolve()
    if target not in TARGETS:
        raise ValueError('Unsupported native target: ' + target)
    if not (root / 'sources.json').is_file() and allow_legacy:
        return _legacy_plan(root, target)
    registry = load_registry(root, validate_files=validate_files)
    plan = {'schema_version': 1, 'target': target,
            'source_registry_sha256': sha256(root / 'sources.json'), 'legacy_audit_recipe': False}
    for kind, key in (('host', 'host_groups'), ('device', 'device_groups')):
        plan[kind + '_sources'] = [source for name in registry['targets'][target][key]
                                  for source in registry['groups'][name]]
    return plan


def project_dependencies(source: Path, native_root: Path) -> dict[str, str]:
    """Fingerprint the source and recursively quoted project headers/fragments.

    Both conditional branches are conservatively followed. Standard/SDK angle
    includes are owned by the compiler identity, not project source discovery.
    """
    root = Path(native_root).resolve()
    source = Path(source).resolve()
    if not source.is_relative_to(root):
        raise ValueError('Native dependency source escapes its root: ' + str(source))
    found = {}
    def visit(path):
        path = path.resolve()
        if not path.is_relative_to(root):
            raise ValueError('Native project include escapes its root: ' + str(path))
        if str(path) in found:
            return
        data = path.read_bytes()
        found[str(path)] = hashlib.sha256(data).hexdigest()
        for raw in re.findall(rb'^[ \t]*#[ \t]*include[ \t]+"([^"\r\n]+)"', data, re.M):
            relative = raw.decode('utf-8')
            candidates = (path.parent / relative, root / 'include' / relative, root / relative)
            target = next((candidate for candidate in candidates if candidate.is_file()), None)
            if target is None:
                raise FileNotFoundError(f'Unresolved project include: {path} -> {relative}')
            visit(target)
    visit(source)
    return dict(sorted(found.items()))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native-root', type=Path, default=Path(__file__).resolve().parents[1] / 'native')
    parser.add_argument('--target', choices=TARGETS, default='cpu')
    parser.add_argument('--kind', choices=('host', 'device', 'all'), default='all')
    parser.add_argument('--format', choices=('json', 'lines'), default='json')
    parser.add_argument('--allow-legacy', action='store_true')
    parser.add_argument('--dependencies', help='Report a relative native source dependency closure instead of a build plan')
    args = parser.parse_args(argv)
    if args.dependencies:
        print(json.dumps(project_dependencies(args.native_root / args.dependencies, args.native_root), indent=2))
        return
    plan = source_plan(args.native_root, args.target, allow_legacy=args.allow_legacy)
    if args.format == 'lines':
        names = (plan['host_sources'] if args.kind == 'host' else plan['device_sources']
                 if args.kind == 'device' else plan['host_sources'] + plan['device_sources'])
        print('\n'.join(names))
    else:
        print(json.dumps(plan, indent=2))


if __name__ == '__main__':
    main()
