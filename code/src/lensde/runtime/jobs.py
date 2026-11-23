"""Durable job ownership shared by command-line and HTTP entry points."""
from __future__ import annotations
import copy
import hashlib
import json
import os
import threading
import time
import uuid
from pathlib import Path
from urllib.parse import urlencode
from lensde.engine import core, state
from lensde.runtime import processes
from lensde.runtime.native import NativeRunner
from lensde.projects import normalize
from lensde.runtime.source_guard import SourceGuard
PROJECT=state.PROJECT_ROOT


def process_identity(pid):
    """Read only the owner PID recorded by this application's own job file."""
    if os.name != 'nt':
        path=Path('/proc')/str(pid)/'stat'
        try:return path.read_text().split()[21]
        except OSError:return None
    import ctypes
    from ctypes import wintypes
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.OpenProcess.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD]
    kernel.OpenProcess.restype=wintypes.HANDLE
    kernel.GetProcessTimes.argtypes=[wintypes.HANDLE,*([ctypes.POINTER(wintypes.FILETIME)]*4)]
    kernel.GetExitCodeProcess.argtypes=[wintypes.HANDLE,ctypes.POINTER(wintypes.DWORD)]
    kernel.CloseHandle.argtypes=[wintypes.HANDLE]
    handle=kernel.OpenProcess(0x1000,False,int(pid))
    if not handle:return None
    try:
        exit_code=wintypes.DWORD()
        if not kernel.GetExitCodeProcess(handle,ctypes.byref(exit_code)) or exit_code.value!=259:return None
        values=[wintypes.FILETIME() for _ in range(4)]
        if not kernel.GetProcessTimes(handle,*(ctypes.byref(value) for value in values)):return 'unknown'
        return str((values[0].dwHighDateTime<<32)|values[0].dwLowDateTime)
    finally:kernel.CloseHandle(handle)

def strict_json(value):
    return json.dumps(core.sanitize_json_value(value), ensure_ascii=False, allow_nan=False)


def _build_stamp(executable):
    files=(executable, executable.with_suffix('.build.json'))
    return tuple((stat.st_size, stat.st_mtime_ns, stat.st_ctime_ns, stat.st_ino)
                 for stat in (path.stat() for path in files))


class JobManager:
    def __init__(self, root=None):
        self.root=Path(root or PROJECT/'work/jobs').resolve()
        self._jobs={}
        self._threads={}
        self._contexts={}
        self._lock=threading.RLock()
        self._source_guard=SourceGuard()
        self._trace_builds=None

    def prepare_trace_builds(self):
        """Validate the local Workbench trace binaries once before serving requests."""
        runner=NativeRunner(PROJECT,build_root='native/build',output_root='work')
        builds={}
        for name in ('lensde_cpu.exe','lensde_de_cuda.exe'):
            executable=runner.build_root/name
            try:
                stamp=_build_stamp(executable)
                runner.executable(executable)
                if _build_stamp(executable)!=stamp:
                    raise RuntimeError(f'Native build changed during verification: {executable}')
                builds[name]=stamp
            except (OSError, ValueError, RuntimeError) as error:
                builds[name]=str(error)
        self._trace_builds=builds

    def _uses_prepared_trace_build(self, kind, request):
        return self._trace_builds is not None and kind=='trace' and not request.get('recipe_id')

    def _check_trace_build(self, name):
        expected=self._trace_builds[name]
        if isinstance(expected,str):
            raise RuntimeError(f'{expected}. Build the selected backend if needed and restart the workbench.')
        executable=PROJECT/'native/build'/name
        try:
            current=_build_stamp(executable)
        except OSError as error:
            raise RuntimeError(f'Native trace build is unavailable: {executable}; restart the workbench after building.') from error
        if current!=expected:
            raise RuntimeError(f'Native trace build changed after startup: {executable}; restart the workbench.')

    def _path(self, job_id):
        if not job_id or any(ch not in '0123456789abcdef' for ch in job_id):
            raise ValueError('Invalid job ID')
        return self.root/job_id

    def _save(self, job):
        path=self._path(job['job_id'])/'job.json'
        path.write_text(strict_json(job)+'\n',encoding='utf-8')

    def _allocate_run(self,job_id):
        short_root=PROJECT/'w'
        short_root.mkdir(exist_ok=True)
        if not short_root.resolve().is_relative_to(PROJECT.resolve()):raise ValueError('Native run root escapes the project')
        for attempt in range(100):
            token=hashlib.sha256((job_id+':'+str(attempt)).encode()).hexdigest()[:8]
            target=short_root/token
            if not target.resolve().is_relative_to(short_root.resolve()):raise ValueError('Native run alias escapes the project')
            try:
                target.mkdir(exist_ok=False)
                (target/'.lensde_owner.json').write_text(json.dumps({'job_id':job_id}),encoding='utf-8')
                return str(target.relative_to(PROJECT))
            except FileExistsError:
                owner=target/'.lensde_owner.json'
                if owner.is_file() and json.loads(owner.read_text())['job_id']==job_id:
                    return str(target.relative_to(PROJECT))
        raise RuntimeError('Could not allocate an unambiguous short native run directory')

    def _run_path(self,job):
        path=(PROJECT/job['run_directory']).resolve() if job.get('run_directory') else (self._path(job['job_id'])/'run').resolve()
        if not path.is_relative_to(PROJECT.resolve()):raise ValueError('Native run output escapes the project')
        return path

    def _load(self, job_id):
        with self._lock:
            if job_id not in self._jobs:
                path=self._path(job_id)/'job.json'
                if not path.is_file():raise KeyError('Unknown job')
                self._jobs[job_id]=json.loads(path.read_text(encoding='utf-8'))
                if self._jobs[job_id]['status'] in {'queued','running','stopping'}:
                    job=self._jobs[job_id]
                    if not job.get('owner_pid') or process_identity(job.get('owner_pid',0))!=job.get('owner_identity'):
                        job['status']='interrupted'
            return self._jobs[job_id]

    def start(self,payload):
        kind=payload.get('kind','workflow')
        if kind not in {'workflow','trace','optimize','vignetting'}:raise ValueError('Unknown job kind')
        if not self._uses_prepared_trace_build(kind,payload):
            self._source_guard.ensure_current()
        project=normalize(payload['project'])
        job_id=uuid.uuid4().hex
        path=self._path(job_id);path.mkdir(parents=True,exist_ok=False)
        job={'job_id':job_id,'status':'queued','kind':kind,'project':project,
             'request':copy.deepcopy(payload),'metadata':{},'created_at':time.time(),
             'log':'','progress':{'stage':'queued'},'result':None,
             'owner_pid':os.getpid(),'owner_identity':process_identity(os.getpid())}
        job['run_directory']=self._allocate_run(job_id)
        with self._lock:
            self._jobs[job_id]=job;self._save(job)
        self._launch(job_id,False)
        return {'job_id':job_id}

    def _launch(self,job_id,resume):
        thread=threading.Thread(target=self._execute,args=(job_id,resume),daemon=True,name='LensDE-'+job_id[:8])
        self._threads[job_id]=thread;thread.start()

    def _execute(self,job_id,resume):
        job=self._load(job_id);path=self._path(job_id);run_dir=self._run_path(job)
        def log(text):
            with self._lock:
                job['log']=(job.get('log','')+str(text)+'\n')[-2_000_000:]
                self._save(job)
        try:
            with processes.scope(log,run_directory=run_dir) as context:
                with self._lock:
                    self._contexts[job_id]=context
                    if job['status']=='stopping':raise processes.CancelledError('Job stopped before launch')
                    job['status']='running';job['started_at']=time.time();self._save(job)
                project=copy.deepcopy(job['project'])
                mode=str(project.get('options',{}).get('optimizer_mode','')).lower().replace('-','_')
                initial_mode=mode in {'initial_structure_search','first_order_seeded','initial_search'}
                workflow=job['kind']=='workflow' or job['kind']=='optimize' and initial_mode
                if workflow:
                    names=('lensde_cpu.exe','lensde_de_cuda.exe','lensde_multi_island.exe')
                else:
                    use_cuda=job['kind']!='vignetting' and bool(project.get('options',{}).get('use_cuda',False))
                    names=('lensde_de_cuda.exe' if use_cuda else 'lensde_cpu.exe',)
                if self._uses_prepared_trace_build(job['kind'],job.get('request',{})):
                    self._check_trace_build(names[0])
                else:
                    runner=NativeRunner(PROJECT,build_root='native/build',output_root='work')
                    for name in names:runner.executable(PROJECT/'native/build'/name)
                if resume and workflow:project['options']['initial_search_resume_existing_funnel_stages']=True
                if job['kind']=='trace':
                    result=core.run_cpp(project,False)
                elif job['kind']=='vignetting':
                    result=core.run_set_vignetting(project)
                elif job['kind']=='optimize' and not workflow:
                    result=core.run_cpp(project,True)
                else:
                    result=core.run_initial_structure_search(project)
                if context.cancelled.is_set():raise processes.CancelledError('Job stopped')
                with self._lock:
                    job['result']=self._result(job_id,run_dir,result)
                    code=result.get('returncode',0) if isinstance(result,dict) else 0
                    job['status']='completed' if code==0 else 'failed'
                    job['message']='' if code==0 else str(result.get('stderr','Native stage failed'))[-4000:]
        except processes.CancelledError as error:
            with self._lock:
                job['status']='stopped';job['message']=str(error)
                job['result']=self._result(job_id,run_dir,{},partial=True)
        except BaseException as error:
            import traceback
            log(traceback.format_exc())
            with self._lock:job['status']='failed';job['message']=str(error)
        finally:
            with self._lock:
                self._contexts.pop(job_id,None)
                job['finished_at']=time.time();self._save(job)

    def _result(self,job_id,run_dir,result,*,partial=False):
        result=copy.deepcopy(result or {})
        result.pop('effective_repro_settings',None)
        result.pop('repro_manifest',None)
        artifacts=[]
        from urllib.parse import urlparse,parse_qs
        for key,value in list(result.items()):
            if key.endswith('_url') and isinstance(value,str) and value.startswith('/api/output?'):
                name=parse_qs(urlparse(value).query).get('file',[''])[0]
                if name and (run_dir/name).is_file():result[key]='/api/output?'+urlencode({'job_id':job_id,'file':name})
                else:result.pop(key)
        if run_dir.is_dir():
            for path in sorted(run_dir.iterdir()):
                if path.is_file():
                    url='/api/output?'+urlencode({'job_id':job_id,'file':path.name})
                    artifacts.append({'name':path.name,'url':url})
                    for field,filename in [('layout_url','layout.svg'),('spot_url','spot.csv'),('score_url','score.json')]:
                        if path.name==filename:result[field]=url
            for field,filename in [('score','score.json'),('system','system.json')]:
                try:
                    if (run_dir/filename).is_file():result[field]=json.loads((run_dir/filename).read_text(encoding='utf-8-sig'))
                except (OSError,ValueError):
                    if not partial:raise
        result['artifacts']=artifacts
        result['job_id']=job_id
        job=self._load(job_id)
        result_project=copy.deepcopy(job['project'])
        if job['kind']=='vignetting':
            # Measurement uses a hard-aperture copy. Only its field factors are
            # part of the editable-project operation, as in the original UI.
            system=copy.deepcopy(result_project['system'])
            fields=result.get('fields')
            if isinstance(fields,list) and len(fields)==len(system.get('fields',[])):
                for source,solved in zip(system['fields'],fields):
                    for key in ('fvcx','fvcy','fvdx','fvdy'):
                        source[key]=core.finite_float(solved.get(key),0.0)
                    source['vignette_x']=source['fvdx']
                    source['vignette_y']=source['fvdy']
            result['system']=system
        if isinstance(result.get('system'),dict):
            system=copy.deepcopy(result['system'])
            current=result_project['system']
            saved_libraries=current.get('glass_libraries')
            libraries=[str(value).strip() for value in saved_libraries if str(value).strip()] if isinstance(saved_libraries,list) else []
            primary=str(current.get('glass_library','')).strip()
            if libraries or primary:system['glass_libraries']=libraries or [primary]
            if primary or libraries:system['glass_library']=primary or libraries[0]
            if isinstance(current.get('glass_search'),dict):system['glass_search']=copy.deepcopy(current['glass_search'])
            result_project['system']=system
            core.sync_variable_initials(result_project)
        effective=result.get('effective_merit')
        if isinstance(effective,dict) and not effective.get('fine_polish',False):
            result_project['merit'].update(copy.deepcopy(effective))
        result['project']=result_project
        return result

    def status(self,job_id):
        job=self._load(job_id)
        with self._lock:
            result={key:copy.deepcopy(value) for key,value in job.items() if key not in {'project','request'}}
        run_dir=self._run_path(job)
        if job['status'] in {'running','stopping'} and run_dir.is_dir():
            candidates=list(run_dir.rglob('adaptive_hybrid_trace.jsonl'))
            if candidates:
                newest=max(candidates,key=lambda p:p.stat().st_mtime)
                try:
                    lines=newest.read_text(encoding='utf-8').splitlines()
                    latest=json.loads(lines[-1]) if lines else {}
                    result['progress']={'stage':str(newest.parent.relative_to(run_dir)),
                                        'generation':latest.get('generation',latest.get('total_de_generations'))}
                except (ValueError,OSError):pass
            result['result']=self._result(job_id,run_dir,{},partial=True)
        return result

    def stop(self,job_id):
        with self._lock:
            job=self._load(job_id)
            if job['status'] not in {'queued','running','stopping'}:return self.status(job_id)
            if job.get('owner_pid')!=os.getpid():raise ValueError('Job is managed by another LensDE server; use its workbench to stop it')
            job['status']='stopping';self._save(job)
            context=self._contexts.get(job_id)
        if context is not None:context.stop()
        return self.status(job_id)

    def resume(self,job_id,project=None):
        job=self._load(job_id)
        if not self._uses_prepared_trace_build(job['kind'],job.get('request',{})):
            self._source_guard.ensure_current()
        with self._lock:
            if job['status'] not in {'stopped','failed','interrupted'}:
                raise ValueError('Only a stopped, failed or interrupted job can resume')
            if project is not None and normalize(project)!=job['project']:
                raise ValueError('Configuration changed; start a new job instead of resuming')
            job['owner_pid']=os.getpid();job['owner_identity']=process_identity(os.getpid())
            job['status']='queued';self._save(job)
        self._launch(job_id,True)
        mode=str(job['project'].get('options',{}).get('optimizer_mode','')).lower().replace('-','_')
        workflow=job['kind']=='workflow' or job['kind']=='optimize' and mode in {'initial_structure_search','first_order_seeded','initial_search'}
        contract='completed stage reuse; unfinished stages restart from their deterministic stage input' if workflow else 'restart the operation from its saved deterministic input; no mid-iteration state restoration'
        return {'job_id':job_id,'resume_contract':contract}

    def output(self,job_id,name):
        root=self._run_path(self._load(job_id))
        path=(root/name).resolve()
        if not path.is_relative_to(root) or not path.is_file():raise FileNotFoundError('Missing job output')
        return path
