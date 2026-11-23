"""Read project-local native source plans and diagnostic source dependencies.

The runtime owns this small standard-library reader; it never imports build
scripts or resolves implementation functions from research snapshots.
"""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import re


def source_registry_sha256(native_root: Path) -> str:
    return hashlib.sha256((Path(native_root) / 'sources.json').read_bytes()).hexdigest()


def _registered_path(native_root: Path, relative: str) -> Path:
    if (not isinstance(relative, str) or '\\' in relative or
            any(part in {'', '.', '..'} or ':' in part for part in relative.split('/'))):
        raise ValueError('Invalid registered native source path: '+str(relative))
    path=(native_root/relative).resolve()
    if not path.is_relative_to(native_root):
        raise ValueError('Native source registry escapes its project: '+relative)
    return path


def load_native_registry(native_root: Path, *, validate_files: bool = True) -> dict:
    native_root=Path(native_root).resolve()
    registry=json.loads((native_root/'sources.json').read_text(encoding='utf-8-sig'))
    if not isinstance(registry,dict):raise ValueError('Invalid native source registry schema')
    groups=registry.get('groups');targets=registry.get('targets')
    if registry.get('schema_version')!=1 or not isinstance(groups,dict) or not groups or not isinstance(targets,dict) or not targets:
        raise ValueError('Invalid native source registry schema')
    seen=set()
    for group,records in groups.items():
        if not isinstance(group,str) or not isinstance(records,list):
            raise ValueError('Invalid native source registry group')
        for relative in records:
            source=_registered_path(native_root,relative)
            if source.suffix not in {'.cpp','.cu'} or source in seen:
                raise ValueError('Duplicate or unsupported registered native source: '+str(relative))
            seen.add(source)
            if validate_files and not source.is_file():
                raise FileNotFoundError('Registered native source is missing: '+str(source))
    for target,recipe in targets.items():
        if not isinstance(target,str) or not isinstance(recipe,dict):
            raise ValueError('Invalid native source registry target')
        for key in ('host_groups','device_groups'):
            references=recipe.get(key)
            if not isinstance(references,list) or any(not isinstance(group,str) or group not in groups for group in references):
                raise ValueError('Native target references an unknown source group: '+target)
    return registry


def registered_group_sources(native_root: Path, *group_names: str) -> list[Path]:
    native_root=Path(native_root).resolve();registry=load_native_registry(native_root)
    result=[]
    for name in group_names:
        if name not in registry['groups']:
            raise ValueError('Unknown native source group: '+name)
        for relative in registry['groups'][name]:
            source=_registered_path(native_root,relative)
            if source not in result:result.append(source)
    return result


def native_source_hashes(native_root: Path) -> dict[str,str]:
    native_root=Path(native_root).resolve();result={}
    for folder in ('src','include','cuda'):
        for path in sorted((native_root/folder).rglob('*')):
            if path.is_file():
                if not path.resolve().is_relative_to(native_root):
                    raise ValueError('Native source link escapes its project: '+str(path))
                result[path.relative_to(native_root).as_posix()]=hashlib.sha256(path.read_bytes()).hexdigest()
    return result


def project_source_dependencies(sources, native_root: Path, *, project_root: Path | None = None) -> dict[str,str]:
    """Fingerprint the actual recursive quoted-include closure, in source order."""
    native_root=Path(native_root).resolve();allowed=Path(project_root or native_root).resolve();found={}
    def visit(path):
        path=Path(path).resolve()
        if not path.is_relative_to(allowed):raise ValueError('Native dependency escapes its project: '+str(path))
        key=str(path)
        if key in found:return
        data=path.read_bytes();found[key]=hashlib.sha256(data).hexdigest()
        for name in re.findall(rb'^[ \t]*#include[ \t]+"([^"\r\n]+)"',data,re.M):
            relative=name.decode('utf-8')
            candidates=(path.parent/relative,native_root/'include'/relative)
            included=next((p for p in candidates if p.is_file()),None)
            if included is None:raise FileNotFoundError(f'Unresolved project include: {path} -> {relative}')
            visit(included)
    for source in sources:visit(source)
    return found


def source_group_text(native_root: Path, *group_names: str):
    """Collect complete implementation files plus their types for extraction.

    This is diagnostic source inspection, not textual inclusion in a build.
    Every .cpp in the registry remains an independently compiled source.
    """
    sources=registered_group_sources(native_root,*group_names)
    dependencies=project_source_dependencies(sources,native_root)
    ordered=[str(p) for p in sources]
    ordered.extend(path for path in dependencies if path not in ordered)
    text='\n\n'.join(Path(path).read_text(encoding='utf-8') for path in ordered)
    return text,dependencies
