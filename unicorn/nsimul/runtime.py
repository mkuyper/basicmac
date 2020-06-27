# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import Any, Callable, Dict, List, Optional, Set, Tuple, Union

import asyncio
import heapq

# -----------------------------------------------------------------------------
# Runtime for Serialized Callbacks
#
# --> asyncio.loop.call_at does not preserve order for jobs in the near past
#     when trying to catch up to real-time...

class Clock:
    def time(self, *, update:bool=False) -> float:
        return 0

    def ticks(self, *, update:bool=False) -> int:
        return 0

    def ticks2time(self, ticks:int) -> float:
        return 0

    def time2ticks(self, time:float) -> int:
        return 0

    def sec2ticks(self, sec:float) -> int:
        return 0

class Job:
    def _prepare(self, ticks:int) -> None:
        self._ticks = ticks
        self._cancelled = False

    def __lt__(self, other:Any) -> bool:
        if isinstance(other, Job):
            return self._ticks < other._ticks
        return NotImplemented

    def cancel(self) -> None:
        self._cancelled = True

    def run(self) -> None:
        pass

class CallbacksJob(Job):
    def __init__(self) -> None:
        self.callbacks:List[Tuple[Callable[...,Any],Dict[str,Any]]] = list()

    def add(self, callback:Callable[...,Any], **kwargs:Any) -> None:
        self.callbacks.append((callback, kwargs))

    def run(self) -> None:
        for callback, kwargs in self.callbacks:
            callback(**kwargs)

class Runtime():
    dummyclock = Clock()
    def __init__(self) -> None:
        self.clock = Runtime.dummyclock
        self.jobs:List[Job] = list()
        self.handle:Optional[asyncio.Handle] = None
        self.stepping = False

    def setclock(self, clock:Optional[Clock]) -> None:
        if clock is None:
            clock = Runtime.dummyclock
        self.clock = clock

    def schedule(self, t:Union[int,float], job:Job) -> None:
        if isinstance(t, float):
            t = self.clock.time2ticks(t)
        job._prepare(t)
        heapq.heappush(self.jobs, job)
        self.rewind()

    def prune(self) -> None:
        while self.jobs and self.jobs[0]._cancelled:
            heapq.heappop(self.jobs)

    def step(self) -> None:
        now = self.clock.ticks(update=True)
        self.stepping = True
        while self.jobs and (j := self.jobs[0])._ticks <= now:
            heapq.heappop(self.jobs)
            if not j._cancelled:
                j.run()
        self.stepping = False
        self.handle = None
        self.rewind()

    def rewind(self) -> None:
        if self.stepping:
            return
        if self.handle:
            self.handle.cancel()
            self.handle = None
        self.prune()
        if self.jobs:
            t = self.clock.ticks2time(self.jobs[0]._ticks)
            self.handle = asyncio.get_running_loop().call_at(t, self.step)

class Jobs:
    def __init__(self, runtime:Runtime) -> None:
        self.runtime = runtime
        self.job2name:Dict[Job,Optional[str]] = dict()
        self.name2job:Dict[str,Job] = dict()

    def _add(self, job:Job, name:Optional[str]) -> None:
        if name:
            assert name not in self.name2job
            self.name2job[name] = job
        self.job2name[job] = name

    def _remove(self, job:Job) -> None:
        name = self.job2name.pop(job)
        if name:
            self.name2job.pop(name)

    def prerun_hook(self, job:Job) -> None:
        self._remove(job)

    def schedule(self, name:Optional[str], t:Union[int,float], callback:Callable[...,Any], **kwargs:Any) -> None:
        job = CallbacksJob()
        job.add(self.prerun_hook, job=job)
        job.add(callback, **kwargs)
        self._add(job, name)
        self.runtime.schedule(t, job)

    def cancel(self, name:str) -> bool:
        job = self.name2job.get(name, None)
        if job:
            job.cancel()
            self._remove(job)
            return True
        else:
            return False

    def cancel_all(self) -> None:
        for job in self.job2name:
            job.cancel()
        self.job2name.clear()
        self.name2job.clear()
