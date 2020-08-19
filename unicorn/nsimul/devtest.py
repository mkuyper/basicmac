# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import Any, Callable, List, Optional

import asyncio
import sys

import rtlib as rt
import loramsg as lm

from device import Simulation
from eventhub import ColoramaStream, LoggingEventHub
from lorawan import LNS, UniversalGateway
from medium import LoraMsg, SimpleMedium
from runtime import Runtime

class DeviceTest:
    def __init__(self, hexfiles:List[str]) -> None:
        self.runtime = Runtime()
        self.log = LoggingEventHub(ColoramaStream(sys.stdout))
        self.medium = SimpleMedium()
        self.gateway = UniversalGateway(self.runtime, self.medium)
        self.lns = LNS()

        self.sim = Simulation(self.runtime, context={ 'evhub': self.log, 'medium': self.medium})
        for hf in hexfiles:
            self.sim.load_hexfile(hf)

    def start(self) -> None:
        self.simtask = asyncio.create_task(self.sim.run())

    async def stop(self) -> None:
        self.simtask.cancel()
        await self.simtask


    @staticmethod
    def explain(s:str, *, explain:Optional[str]=None, **kwargs:Any) -> str:
        return s if explain is None else f'{s} ({explain})'

    async def up(self, *, timeout:Optional[float]=None, **kwargs:Any) -> rt.types.Msg:
        return await asyncio.wait_for(self.gateway.next_up(), timeout)

    async def join(self, *, timeout:Optional[float]=None, **kwargs:Any) -> None:
        jreq = await self.up(timeout=timeout, **kwargs)
        jacc, self.deveui, self.devaddr = self.lns.join(jreq, **kwargs)
        self.gateway.sched_dn(jacc)

    def verify(self, updf:rt.types.Msg, *, port:Optional[int]=None, **kwargs:Any) -> None:
        session = self.lns.verify(updf)
        assert self.deveui == session['deveui']
        assert self.devaddr == session['devaddr']

    async def updf(self, *, timeout:Optional[float]=None,
            filter:Callable[[rt.types.Msg],bool]=lambda m: True, limit:int=1,
            **kwargs:Any) -> rt.types.Msg:
        loop = asyncio.get_running_loop()
        deadline = timeout and loop.time() + timeout
        for _ in range(limit):
            timeout = deadline and max(0, deadline - loop.time())
            updf = await self.up(timeout=timeout)
            self.verify(updf, **kwargs)
            if filter(updf):
                return updf
        assert False, DeviceTest.explain(f'No matching message received within limit of {limit} messages', **kwargs)
