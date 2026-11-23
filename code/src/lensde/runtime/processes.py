"""Subprocess compatibility with job-local cancellation and package discovery.

Only children created inside an explicit scope are tracked. No process discovery,
port probing, or cleanup of unrelated applications occurs here.
"""
from __future__ import annotations
import contextlib
import contextvars
import os
import hashlib
import json
from pathlib import Path
import subprocess as _subprocess
import threading
import time

_scope = contextvars.ContextVar("lensde_process_scope", default=None)


class _WindowsJob:
    """An unnamed job object containing only children created for this task."""
    def __init__(self):
        import ctypes
        from ctypes import wintypes
        class Basic(ctypes.Structure):
            _fields_ = [("ProcessTime", ctypes.c_longlong), ("JobTime", ctypes.c_longlong),
                        ("Flags", wintypes.DWORD), ("Minimum", ctypes.c_size_t), ("Maximum", ctypes.c_size_t),
                        ("Active", wintypes.DWORD), ("Affinity", ctypes.c_size_t),
                        ("Priority", wintypes.DWORD), ("Scheduling", wintypes.DWORD)]
        class IO(ctypes.Structure):
            _fields_ = [(name, ctypes.c_ulonglong) for name in ("ReadOps", "WriteOps", "OtherOps", "ReadBytes", "WriteBytes", "OtherBytes")]
        class Extended(ctypes.Structure):
            _fields_ = [("Basic", Basic), ("IO", IO), ("ProcessMemory", ctypes.c_size_t),
                        ("JobMemory", ctypes.c_size_t), ("PeakProcess", ctypes.c_size_t), ("PeakJob", ctypes.c_size_t)]
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
        kernel.CreateJobObjectW.restype = wintypes.HANDLE
        kernel.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
        kernel.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
        kernel.TerminateJobObject.argtypes = [wintypes.HANDLE, wintypes.UINT]
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        self.kernel, self.ctypes = kernel, ctypes
        self.handle = kernel.CreateJobObjectW(None, None)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        limits = Extended()
        limits.Basic.Flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        if not kernel.SetInformationJobObject(self.handle, 9, ctypes.byref(limits), ctypes.sizeof(limits)):
            self.close()
            raise ctypes.WinError(ctypes.get_last_error())

    def add(self, process):
        if not self.kernel.AssignProcessToJobObject(self.handle, int(process._handle)):
            raise self.ctypes.WinError(self.ctypes.get_last_error())

    def terminate(self):
        if self.handle:
            self.kernel.TerminateJobObject(self.handle, 1)

    def close(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None


class CancelledError(RuntimeError):
    pass


class ProcessScope:
    def __init__(self, log=None):
        self.cancelled = threading.Event()
        self._children = set()
        self._lock = threading.RLock()
        self.log = log or (lambda text: None)
        self._job = _WindowsJob() if os.name == "nt" else None

    def add(self, child):
        with self._lock:
            if self.cancelled.is_set():
                child.terminate()
                raise CancelledError("Job stopped")
            self._children.add(child)
            if self._job is not None:
                try:
                    self._job.add(child)
                except BaseException:
                    child.kill()
                    child.wait()
                    raise

    def stop(self):
        self.cancelled.set()
        if self._job is not None:
            self._job.terminate()
        with self._lock:
            children = list(self._children)
        for child in children:
            if child.poll() is None:
                try:
                    child.terminate()
                except OSError:
                    pass
        for child in children:
            try:
                child.wait(timeout=2)
            except _subprocess.TimeoutExpired:
                child.kill()
                child.wait(timeout=2)

    def close(self):
        if self._job is not None:
            self._job.close()


@contextlib.contextmanager
def scope(log=None, run_directory=None):
    context = ProcessScope(log)
    context.run_directory = Path(run_directory) if run_directory is not None else None
    token = _scope.set(context)
    try:
        yield context
    finally:
        context.close()
        _scope.reset(token)


def run_directory(default):
    context = _scope.get()
    return context.run_directory if context is not None and context.run_directory is not None else default


def mark_stage(command, returncode):
    if returncode != 0 or not isinstance(command,(tuple,list)) or '--out-dir' not in command:return
    if not command or not Path(str(command[0])).name.lower().startswith('lensde_'):return
    directory=Path(command[command.index('--out-dir')+1])
    if not directory.is_dir():return
    inputs={}
    for flag in ('--system','--variables','--merit','--initial-population','--glass-search-config'):
        if flag in command:
            path=Path(command[command.index(flag)+1])
            if path.is_file():inputs[str(path)]=hashlib.sha256(path.read_bytes()).hexdigest()
    (directory/'.lensde_stage_complete.json').write_text(json.dumps({'returncode':0,'command':[str(x) for x in command],'inputs':inputs}),encoding='utf-8')


def stage_complete(directory):
    context=_scope.get()
    # Strict completion receipts are an operational contract for newly owned jobs.
    # Explicit historical-stage tools retain their own archived completion checks.
    if context is None or getattr(context,'run_directory',None) is None:return True
    marker=Path(directory)/'.lensde_stage_complete.json'
    if not marker.is_file():return False
    try:
        data=json.loads(marker.read_text(encoding='utf-8'))
        return data['returncode']==0 and all(Path(p).is_file() and hashlib.sha256(Path(p).read_bytes()).hexdigest()==h
                                            for p,h in data['inputs'].items())
    except (ValueError,KeyError,OSError):return False


def _environment(env):
    environment = os.environ.copy() if env is None else dict(env)
    package_src = str(Path(__file__).resolve().parents[2])
    entries = [package_src, *environment.get("PYTHONPATH", "").split(os.pathsep)]
    environment["PYTHONPATH"] = os.pathsep.join(dict.fromkeys(x for x in entries if x))
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    environment["PYTHONIOENCODING"] = "utf-8"
    return environment


class Popen(_subprocess.Popen):
    def __init__(self, *args, **kwargs):
        context = _scope.get()
        if context is not None and context.cancelled.is_set():
            raise CancelledError("Job stopped")
        kwargs["env"] = _environment(kwargs.get("env"))
        kwargs.setdefault("creationflags", getattr(_subprocess, "CREATE_NO_WINDOW", 0))
        command = args[0] if args else kwargs.get("args")
        if isinstance(command, (list, tuple)) and command:
            command = list(command)
            if Path(str(command[0])).name.lower() in {"lensde_cpu.exe", "lensde_de_cuda.exe", "lensde_material_cpu.exe", "lensde_material_cuda.exe"} and "--glass-database" not in command:
                project = Path(__file__).resolve().parents[3]
                command += ["--glass-database", str(project / "catalogs/database")]
            if args:
                args = (command, *args[1:])
            else:
                kwargs["args"] = command
        super().__init__(*args, **kwargs)
        self._lensde_scope = context
        if context is not None:
            context.add(self)
            context.log("$ " + _subprocess.list2cmdline([str(x) for x in (args[0] if args and isinstance(args[0], (list, tuple)) else [args[0] if args else kwargs.get('args')])]))


def run(*popenargs, input=None, capture_output=False, timeout=None, check=False, **kwargs):
    if input is not None:
        if kwargs.get("stdin") is not None:
            raise ValueError("stdin and input arguments may not both be used")
        kwargs["stdin"] = _subprocess.PIPE
    if capture_output:
        if kwargs.get("stdout") is not None or kwargs.get("stderr") is not None:
            raise ValueError("stdout and stderr may not be used with capture_output")
        kwargs["stdout"] = kwargs["stderr"] = _subprocess.PIPE
    with Popen(*popenargs, **kwargs) as process:
        started = time.monotonic()
        context = process._lensde_scope
        first = True
        while True:
            if context is not None and context.cancelled.is_set():
                process.terminate()
                process.communicate()
                raise CancelledError("Job stopped")
            remaining = None if timeout is None else timeout - (time.monotonic() - started)
            if remaining is not None and remaining <= 0:
                process.kill()
                stdout, stderr = process.communicate()
                raise _subprocess.TimeoutExpired(process.args, timeout, output=stdout, stderr=stderr)
            try:
                stdout, stderr = process.communicate(input if first else None,
                    timeout=remaining if context is None else (0.2 if remaining is None else min(0.2, remaining)))
                break
            except _subprocess.TimeoutExpired:
                first = False
                if context is None:
                    process.kill()
                    stdout, stderr = process.communicate()
                    raise _subprocess.TimeoutExpired(process.args, timeout, output=stdout, stderr=stderr)
        if context is not None:
            for text in (stdout, stderr):
                if text:
                    context.log(text.decode("utf-8", "replace") if isinstance(text, bytes) else text)
            with context._lock:
                context._children.discard(process)
        completed = _subprocess.CompletedProcess(process.args, process.returncode, stdout, stderr)
        mark_stage(process.args,process.returncode)
        if check:
            completed.check_returncode()
        return completed


def __getattr__(name):
    return getattr(_subprocess, name)
