# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import Any, Callable, List, Optional, Union

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
    def __init__(self, ticks:int, callback:Callable[...,Any], **kwargs:Any) -> None:
        self.ticks = ticks
        self.callback = callback
        self.kwargs = kwargs
        self.cancelled = False

    def __lt__(self, other:Any) -> bool:
        if isinstance(other, Job):
            return self.ticks < other.ticks
        return NotImplemented

    def cancel(self) -> None:
        self.cancelled = True

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

    def schedule(self, t:Union[int,float], callback:Callable[...,Any], **kwargs:Any) -> Job:
        if isinstance(t, float):
            t = self.clock.time2ticks(t)
        j = Job(t, callback, **kwargs)
        heapq.heappush(self.jobs, j)
        self.rewind()
        return j

    def prune(self) -> None:
        while self.jobs and self.jobs[0].cancelled:
            heapq.heappop(self.jobs)

    def step(self) -> None:
        now = self.clock.ticks(update=True)
        self.stepping = True
        while self.jobs and (j := self.jobs[0]).ticks <= now:
            heapq.heappop(self.jobs)
            if not j.cancelled:
                j.callback(**j.kwargs)
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
            t = self.clock.ticks2time(self.jobs[0].ticks)
            self.handle = asyncio.get_running_loop().call_at(t, self.step)
