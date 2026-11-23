"""Conservative object cache for the unchanged C++17/O3 CPU recipe."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'src'))
from lensde.runtime.native_toolchain import cpu_runtime_linkage


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def dependencies(source: Path, include_root: Path) -> dict[str,str]:
    found={}
    def visit(path):
        path=path.resolve()
        if str(path) in found:return
        data=path.read_bytes();found[str(path)]=hashlib.sha256(data).hexdigest()
        for name in re.findall(rb'^[ \t]*#include[ \t]+"([^"\r\n]+)"',data,re.M):
            relative=name.decode('utf-8')
            candidates=(path.parent/relative,include_root/relative)
            target=next((p for p in candidates if p.is_file()),None)
            if target is None:raise FileNotFoundError(f'Unresolved project include: {path} -> {relative}')
            visit(target)
    visit(source)
    return dict(sorted(found.items()))


def build(compiler: str, source_root: Path, sources: list[Path], target: Path, cache: Path):
    compiler=str(Path(compiler).resolve());include=(source_root/'include').resolve()
    version=subprocess.check_output([compiler,'--version'],text=True)
    compiler_sha256=digest(Path(compiler))
    flags=['-std=c++17','-O3']
    cache.mkdir(parents=True,exist_ok=True);objects=[];records=[];compiled=0
    for source in sources:
        source=source.resolve()
        spec={'compiler':compiler,'compiler_sha256':compiler_sha256,'compiler_version':version,
              'flags':flags,'include_root':str(include),'source':str(source),'dependencies':dependencies(source,include)}
        key=hashlib.sha256(json.dumps(spec,sort_keys=True).encode()).hexdigest()[:24]
        obj=cache/(source.stem+'_'+key+'.o');receipt=obj.with_suffix('.json')
        reused=False
        if obj.is_file() and receipt.is_file():
            old=json.loads(receipt.read_text(encoding='utf-8'))
            reused=old.get('inputs')==spec and old.get('object_sha256')==digest(obj)
        command=[compiler,*flags,'-I',str(include),'-c',str(source),'-o',str(obj)]
        if not reused:
            print('Compiling CPU object '+source.name,flush=True)
            subprocess.run(command,check=True)
            if dependencies(source,include)!=spec['dependencies']:raise RuntimeError('CPU inputs changed during compilation')
            receipt.write_text(json.dumps({'schema_version':1,'inputs':spec,'command':command,'object_sha256':digest(obj)},indent=2)+'\n',encoding='utf-8')
            compiled+=1
        else:print('Reusing verified CPU object '+source.name,flush=True)
        objects.append(obj);records.append({'source':str(source),'object':str(obj),'sha256':digest(obj),'reused':reused,'input_receipt':str(receipt),'input_receipt_sha256':digest(receipt)})
    linkage=cpu_runtime_linkage(compiler)
    command=[compiler,*flags,*linkage['link_flags'],*(str(p) for p in objects),'-o',str(target)]
    print('Linking '+str(target),flush=True);subprocess.run(command,check=True)
    return {'compiler_version':version,'link_command':command,'objects':records,'objects_compiled':compiled,'objects_reused':len(objects)-compiled,'compile_flags':flags,**linkage}
