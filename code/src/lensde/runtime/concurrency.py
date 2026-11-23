"""Preserve job ownership when existing workflow work is submitted to threads."""
import concurrent.futures as _futures
import contextvars


class ThreadPoolExecutor(_futures.ThreadPoolExecutor):
    def submit(self, function, /, *args, **kwargs):
        context=contextvars.copy_context()
        return super().submit(context.run,function,*args,**kwargs)


class _Futures:
    ThreadPoolExecutor=ThreadPoolExecutor
    def __getattr__(self,name):return getattr(_futures,name)


futures=_Futures()
