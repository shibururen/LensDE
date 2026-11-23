"""Run LensDE projects and inspect the published benchmark inputs."""
from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

from .config import BenchmarkCatalog, ConfigurationError


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", type=Path, help="configuration/build root; run/serve use this installed checkout")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("benchmarks", help="list the ten frozen saved configurations")
    show = commands.add_parser("inspect", help="inspect a saved configuration without starting optimization")
    show.add_argument("task")
    show.add_argument("--language", choices=("zh-CN", "en"), default="zh-CN")
    seed = commands.add_parser("recorded-seeds", help="read an exact archived fixed-material candidate coordinate")
    seed.add_argument("task")
    seed.add_argument("--source-rank", type=int, required=True)
    seed.add_argument("--cycle", type=int, required=True)
    serve=commands.add_parser('serve',help='open the local bilingual workbench service')
    serve.add_argument('--port',type=int,default=8767)
    build=commands.add_parser('build',help='build project-local native executables')
    build.add_argument('--backend',choices=('cpu','cuda','broker','all'),default='all')
    run=commands.add_parser('run',help='run the same project/workflow used by the workbench')
    input_group=run.add_mutually_exclusive_group()
    input_group.add_argument('--benchmark')
    input_group.add_argument('--project',type=Path)
    input_group.add_argument('--resume')
    run.add_argument('--kind',choices=('trace','optimize','vignetting','workflow'),default='workflow')
    run.add_argument('--quiet',action='store_true')
    args = parser.parse_args(argv)
    checkout = Path(__file__).resolve().parents[2]
    if args.command in {'serve', 'run'} and args.project_root and args.project_root.resolve() != checkout:
        parser.error('run/serve use the installed source checkout; launch lensde from the intended checkout instead of overriding --project-root')
    if args.command=='serve':
        from .api.server import main as serve_main
        serve_main(['--port',str(args.port)]);return 0
    if args.command=='build':
        import importlib.util
        root=Path(args.project_root).resolve() if args.project_root else Path(__file__).resolve().parents[2]
        spec=importlib.util.spec_from_file_location('lensde_build',root/'scripts/build.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        module.main(['--backend',args.backend]);return 0
    if args.command=='run':
        from .runtime.jobs import JobManager,strict_json
        from .projects import benchmark,normalize
        manager=JobManager()
        if args.resume:job_id=manager.resume(args.resume)['job_id']
        else:
            if args.project:project=normalize(json.loads(args.project.read_text(encoding='utf-8-sig')))
            elif args.benchmark:project=benchmark(args.benchmark)['project']
            else:parser.error('run requires --benchmark, --project or --resume')
            payload={'project':project,'kind':args.kind,'benchmark_id':args.benchmark}
            job_id=manager.start(payload)['job_id']
        previous='';logged=0
        try:
            while True:
                status=manager.status(job_id)
                if not args.quiet:
                    if status['status']!=previous:print(job_id+': '+status['status'],flush=True);previous=status['status']
                    text=status.get('log','')
                    if len(text)>logged:print(text[logged:],end='',flush=True);logged=len(text)
                if status['status'] not in {'queued','running','stopping'}:break
                time.sleep(0.2)
        except KeyboardInterrupt:
            manager.stop(job_id)
            thread=manager._threads.get(job_id)
            if thread:thread.join(timeout=5)
            return 130
        print(strict_json(status))
        return 0 if status['status']=='completed' else 1
    catalog = BenchmarkCatalog(args.project_root)
    try:
        if args.command == "benchmarks":
            value = [row.as_dict() for row in catalog.list_benchmarks()]
        elif args.command == "inspect":
            value = catalog.inspect(args.task, locale=args.language)
        else:
            value = catalog.load(args.task).fixed_material_seeds(
                source_rank=args.source_rank, cycle=args.cycle).as_dict()
    except (ConfigurationError, OSError, ValueError) as error:
        parser.error(str(error))
    print(json.dumps(value, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
