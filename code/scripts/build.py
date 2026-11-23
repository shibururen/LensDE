"""Build the local LensDE CPU, CUDA, and material broker executables."""
from __future__ import annotations
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from cpu_build_cache import build as cached_cpu_build
from native_sources import source_plan

PROJECT = Path(__file__).resolve().parents[1]
NATIVE = PROJECT / "native"


def source_hashes():
    return {str(p.relative_to(NATIVE)).replace("\\", "/"):hashlib.sha256(p.read_bytes()).hexdigest()
            for part in ("src","include","cuda") for p in sorted((NATIVE/part).rglob('*')) if p.is_file()}


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend",choices=("cpu","cuda","broker","all"),default="all")
    parser.add_argument("--cuda-arch",default="86")
    parser.add_argument("--output-name",help="Plain executable filename for a single selected backend")
    args=parser.parse_args(argv)
    if args.output_name and (args.backend=='all' or Path(args.output_name).name!=args.output_name or not args.output_name.endswith('.exe')):
        parser.error('--output-name requires one backend and a plain .exe filename')
    build=NATIVE/'build';build.mkdir(exist_ok=True)
    for backend in (("cpu","cuda","broker") if args.backend=="all" else (args.backend,)):
        sources=source_hashes()
        plan=source_plan(NATIVE,backend)
        name=args.output_name or {"cpu":"lensde_cpu.exe","cuda":"lensde_de_cuda.exe","broker":"lensde_multi_island.exe"}[backend]
        target=build/name
        if backend=="cpu":
            compiler=shutil.which('g++')
            if not compiler:raise RuntimeError('g++ is required for the preserved CPU toolchain')
            evidence=cached_cpu_build(compiler,NATIVE,[NATIVE/name for name in plan['host_sources']],target,build/'cpu_objects')
            command=evidence['link_command'];version=evidence['compiler_version']
        else:
            shell=shutil.which('pwsh') or shutil.which('powershell')
            if not shell:raise RuntimeError('PowerShell is required for the preserved CUDA build')
            command=[shell,'-NoProfile','-File',str(NATIVE/'scripts/build_cuda_de.ps1'),
                     '-CudaArch',args.cuda_arch,'-ProductionOnly','-OutputName',name,'-PythonExecutable',sys.executable]
            if backend=='broker':command+=['-ExperimentalMultiIsland']
            version=subprocess.check_output(['nvcc','--version'],text=True)
        print('Building '+backend+': '+str(target),flush=True)
        if backend!='cpu':subprocess.run(command,cwd=PROJECT,check=True)
        if source_hashes()!=sources:raise RuntimeError('Native source changed during build')
        if source_plan(NATIVE,backend)!=plan:raise RuntimeError('Native source registry changed during build')
        receipt={'schema_version':1,'label':'candidate','backend':backend,'diagnostics':False,
                 'original_source_root':str(NATIVE),'source_sha256':sources,'command':command,
                 'compiler_version':version,'executable':str(target),
                 'executable_sha256':hashlib.sha256(target.read_bytes()).hexdigest()}
        receipt['source_registry_sha256']=plan['source_registry_sha256']
        receipt['host_sources']=plan['host_sources'];receipt['device_sources']=plan['device_sources']
        if backend=='cpu':receipt['object_build']=evidence
        target.with_suffix('.build.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
        print('Built '+str(target),flush=True)


if __name__=='__main__':main()
