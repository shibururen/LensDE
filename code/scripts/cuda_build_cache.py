"""Build registered CUDA host/device units with verified dependency caches.

PowerShell initializes the preserved MSVC environment. This module retains the
original nvcc recipes while tracking only each source's actual project includes.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

from native_sources import project_dependencies, source_plan


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def key(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def save(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)


def compiler_identity(path):
    resolved = Path(shutil.which(str(path)) or path).resolve()
    if not resolved.is_file():
        raise FileNotFoundError('Native compiler not found: ' + str(path))
    return {'path': str(resolved), 'sha256': sha(resolved)}


def build(native_root, target_name, output_name, nvcc, arch, ptxas_opt=3,
          expensive=True, verbose=False, max_register_count=0, host_only=False,
          force=False, definitions=None):
    root = Path(native_root).resolve()
    if Path(output_name).name != output_name or not output_name.endswith('.exe'):
        raise ValueError('Use a plain executable output filename')
    plan = source_plan(root, target_name)
    definitions = definitions or ['-DOPTILAND_WITH_CUDA', '-DOPTILAND_DE_CUDA_ONLY', '-DOPTILAND_ASPHERE_UC10_RELEASE']
    nvcc_id = compiler_identity(nvcc)
    host_id = compiler_identity('cl.exe')
    context = {'nvcc': nvcc_id, 'host_compiler': host_id,
               'include_environment': os.environ.get('INCLUDE', ''),
               'lib_environment': os.environ.get('LIB', ''),
               'implicit_flag_environment': {name: os.environ.get(name, '') for name in
                   ('CL', '_CL_', 'NVCC_PREPEND_FLAGS', 'NVCC_APPEND_FLAGS', 'NVCC_CCBIN')}}
    nvcc = nvcc_id['path']
    arch_flags = ['-gencode', f'arch=compute_{arch},code=sm_{arch}']
    include_flags = ['-I', str(root / 'include')]
    ptxas_flags = ['-Xptxas', f'--opt-level={ptxas_opt}']
    if not expensive:
        ptxas_flags += ['-Xptxas', '--allow-expensive-optimizations=false']
    if verbose:
        ptxas_flags += ['-Xptxas', '--verbose']
    register_flags = ['--maxrregcount=' + str(max_register_count)] if max_register_count else []
    host_flags = ['--use-local-env', '-std=c++17', '-O3', *definitions, *include_flags]
    device_flags = ['--use-local-env', '-std=c++17', '-O3', '-rdc=true', *arch_flags,
                    *ptxas_flags, *register_flags, *definitions, *include_flags]
    build_root = root / 'build'
    records = []
    input_specs = []
    for kind, sources, flags in (('host', plan['host_sources'], host_flags),
                                  ('device', [] if host_only else plan['device_sources'], device_flags)):
        folder = build_root / ('cuda_host_objects' if kind == 'host' else 'cuda_device_objects')
        folder.mkdir(parents=True, exist_ok=True)
        for relative in sources:
            source = root / relative
            spec = {'schema_version': 1, 'compiler_context': context, 'flags': flags,
                    'source': str(source), 'dependencies': project_dependencies(source, root)}
            fingerprint = key(spec)
            object_path = folder / (source.stem + '_' + fingerprint[:24] + '.obj')
            receipt_path = object_path.with_suffix('.json')
            previous = json.loads(receipt_path.read_text(encoding='utf-8')) if receipt_path.is_file() else {}
            reused = (not force and object_path.is_file() and previous.get('inputs') == spec and
                      previous.get('object_sha256') == sha(object_path))
            command = [nvcc, *flags, '-c', str(source), '-o', str(object_path)]
            if not reused:
                print(f'Compiling {kind} object {relative}', flush=True)
                subprocess.run(command, check=True)
                if project_dependencies(source, root) != spec['dependencies']:
                    raise RuntimeError('Native inputs changed during compilation: ' + relative)
                save(receipt_path, {'schema_version': 1, 'inputs': spec, 'command': command,
                                    'object_sha256': sha(object_path)})
            else:
                print(f'Reusing {kind} object {relative}', flush=True)
            records.append({'source': relative, 'kind': kind, 'path': str(object_path),
                            'sha256': sha(object_path), 'reused': reused,
                            'dependency_sha256': spec['dependencies'], 'cache_key': fingerprint,
                            'input_receipt': str(receipt_path), 'input_receipt_sha256': sha(receipt_path)})
            input_specs.append((source, spec))
    for source, spec in input_specs:
        if project_dependencies(source, root) != spec['dependencies']:
            raise RuntimeError('Native inputs changed before linking: ' + str(source))
    if source_plan(root, target_name) != plan:
        raise RuntimeError('Native source registry changed during build')
    if host_only:
        print('Host-only CUDA objects verified; no device compilation or executable link', flush=True)
        return {'host_only': True, 'objects': records, **plan}
    target = build_root / output_name
    objects = [row['path'] for row in records]
    link_command = [nvcc, '--use-local-env', *arch_flags, *objects, '-o', str(target)]
    signature = key({'compiler_context': context, 'arch_flags': arch_flags,
                     'ordered_objects': [(row['path'], row['sha256']) for row in records]})
    evidence_path = Path(str(target) + '.objects.json')
    previous = json.loads(evidence_path.read_text(encoding='utf-8-sig')) if evidence_path.is_file() else {}
    link_reused = (not force and target.is_file() and previous.get('link_signature') == signature and
                   previous.get('executable_sha256') == sha(target))
    if not link_reused:
        print('Linking ' + str(target), flush=True)
        subprocess.run(link_command, check=True)
    else:
        print('Reusing verified executable ' + str(target), flush=True)
    source_hashes = {p.relative_to(root).as_posix(): sha(p) for part in ('src', 'include', 'cuda')
                     for p in sorted((root / part).rglob('*')) if p.is_file()}
    evidence = {**plan, 'schema_version': 2, 'source_sha256': source_hashes,
                'compile_definitions': definitions, 'compiler_context': context,
                'host_compile_flags': host_flags, 'device_compile_flags': device_flags,
                'arch': str(arch), 'ptxas_opt': ptxas_opt, 'expensive': expensive,
                'max_register_count': max_register_count, 'objects': records,
                'objects_compiled': sum(not row['reused'] for row in records),
                'objects_reused': sum(row['reused'] for row in records),
                'link_command': link_command, 'link_signature': signature, 'link_reused': link_reused,
                'link_signature_method': 'SHA256 canonical JSON of compiler_context, arch_flags and ordered path/SHA256 pairs',
                'executable_sha256': sha(target)}
    save(evidence_path, evidence)
    Path(str(target) + '.link.sha256').write_text(signature, encoding='ascii')
    print('Built ' + str(target), flush=True)
    return evidence


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native-root', type=Path, required=True)
    parser.add_argument('--target', choices=('cuda', 'broker'), required=True)
    parser.add_argument('--output-name', required=True)
    parser.add_argument('--nvcc', required=True)
    parser.add_argument('--arch', required=True)
    parser.add_argument('--ptxas-opt', type=int, choices=range(4), default=3)
    parser.add_argument('--expensive', choices=('true', 'false'), default='true')
    parser.add_argument('--verbose', action='store_true')
    parser.add_argument('--max-register-count', type=int, default=0)
    parser.add_argument('--host-only', action='store_true')
    parser.add_argument('--force', action='store_true')
    parser.add_argument('--define', action='append')
    args = parser.parse_args(argv)
    build(args.native_root, args.target, args.output_name, args.nvcc, args.arch, args.ptxas_opt,
          args.expensive == 'true', args.verbose, args.max_register_count, args.host_only,
          args.force, args.define)


if __name__ == '__main__':
    main()
