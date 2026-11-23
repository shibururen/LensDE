"""Reject new jobs after loaded source code or saved inputs change."""
from pathlib import Path

PROJECT=Path(__file__).resolve().parents[3]


def snapshot():
    paths=[]
    package=PROJECT/'src/lensde'
    paths.extend(package.rglob('*.py'))
    paths.extend((package/'seeds/profiles').glob('*.json'))
    paths.extend((PROJECT/'benchmarks').rglob('*.json'))
    paths.append(PROJECT/'native/sources.json')
    for folder in ('src','include','cuda'):
        paths.extend(p for p in (PROJECT/'native'/folder).rglob('*') if p.is_file())
    return {str(p.relative_to(PROJECT)):(p.stat().st_mtime_ns,p.stat().st_size) for p in paths
            if p.is_file() and '__pycache__' not in p.parts}


class SourceGuard:
    def __init__(self):self.loaded=snapshot()
    def ensure_current(self):
        current=snapshot()
        changed=sorted(k for k in set(current)|set(self.loaded) if current.get(k)!=self.loaded.get(k))
        if changed:raise RuntimeError('LensDE code or saved inputs changed after this process started; restart the workbench before running: '+', '.join(changed[:5]))


_guard=SourceGuard()


def ensure_current():_guard.ensure_current()
