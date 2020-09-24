# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
# Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import Any, Awaitable, Callable, Dict, Generator, List, Optional, TypeVar, Union
from typing import cast

import asyncio
import heapq

from contextvars import Context

T = TypeVar('T')

class VirtualTimeLoop(asyncio.AbstractEventLoop):
    def __init__(self) -> None:
        self._time:float = 0
        self._tasks:List[asyncio.TimerHandle] = list()
        self._ex:Optional[BaseException] = None

    def get_debug(self) -> bool:
        return False

    def time(self) -> float:
        return self._time

    def call_exception_handler(self, context:Dict[str,Any]) -> None:
        self._ex = context.get('exception', None)

    def _run(self, future:Optional['asyncio.Future[Any]']) -> None:
        try:
            asyncio.events._set_running_loop(self)
            while len(self._tasks) and (future is None or not future.done()):
                th = heapq.heappop(self._tasks)
                self._time = th.when()
                if not th.cancelled():
                    th._run()
                if self._ex is not None:
                    raise self._ex
        finally:
            self._ex = None
            asyncio.events._set_running_loop(None)

    def run_until_complete(self, future:Union[Generator[Any,None,T],Awaitable[T]]) -> T:
        f = asyncio.ensure_future(future, loop=self)
        self._run(f)
        return f.result()

    def create_task(self, coro): # type: ignore
        return asyncio.Task(coro, loop=self)

    def create_future(self) -> 'asyncio.Future[Any]':
        return asyncio.Future(loop=self)

    def call_at(self, when:float, callback:Callable[...,Any], *args:Any, context:Optional[Context]=None) -> asyncio.TimerHandle:
        th = asyncio.TimerHandle(when, callback, list(args), self, context) # type:ignore
        heapq.heappush(self._tasks, th)
        return th

    def call_later(self, delay:float, callback:Callable[...,Any], *args:Any, context:Optional[Context]=None) -> asyncio.TimerHandle:
        return self.call_at(self._time + delay, callback, *args, context=context)

    def call_soon(self, callback:Callable[...,Any], *args:Any, context:Optional[Context]=None) -> asyncio.TimerHandle:
        return self.call_later(0, callback, *args, context=context)

    def _timer_handle_cancelled(self, handle:asyncio.TimerHandle) -> None:
        pass
