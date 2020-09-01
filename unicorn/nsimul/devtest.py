# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import Any, Callable, List, Optional

import asyncio
import sys

from ward import expect

import rtlib as rt
import loradefs as ld
import loramsg as lm

from device import Simulation
from eventhub import ColoramaStream, LoggingEventHub
from lorawan import LNS, LoraWanMsg, UniversalGateway
from medium import LoraMsg, SimpleMedium
from runtime import Runtime

class DeviceTest:
    def __init__(self, hexfiles:List[str]) -> None:
        self.runtime = Runtime()
        self.log = LoggingEventHub(ColoramaStream(sys.stdout))
        self.medium = SimpleMedium()
        self.gateway = UniversalGateway(self.runtime, self.medium)

        self.sim = Simulation(self.runtime, context={ 'evhub': self.log, 'medium': self.medium})
        for hf in hexfiles:
            self.sim.load_hexfile(hf)

    def start(self) -> None:
        self.simtask = asyncio.create_task(self.sim.run())

    async def stop(self) -> None:
        self.simtask.cancel()
        try:
            await self.simtask
        except asyncio.CancelledError:
            pass

    @staticmethod
    def explain(s:str='', *, explain:Optional[str]=None, **kwargs:Any) -> str:
        return s if explain is None else f'{s} ({explain})'

    async def up(self, *, timeout:Optional[float]=None, **kwargs:Any) -> LoraWanMsg:
        return await asyncio.wait_for(self.gateway.next_up(), timeout)

    def dn(self, uplwm:LoraWanMsg, pdu:bytes, *, rx2:bool=False, rx1delay:int=0, xpow:Optional[float]=None, **kwargs:Any) -> None:
        rxdelay = rx1delay or self.session['rx1delay']
        if rx2:
            rxdelay += 1
            (freq, rps) = LNS.dn_rx2(self.session)
        else:
            (freq, rps) = LNS.dn_rx1(self.session, uplwm.msg.freq, uplwm.msg.rps)
        if xpow is None:
            xpow = uplwm.reg.max_eirp
        self.gateway.sched_dn(LoraMsg(uplwm.msg.xend + rxdelay, pdu, freq, rps, xpow=xpow))

    async def join(self, *, timeout:Optional[float]=None, **kwargs:Any) -> None:
        jreq = await self.up(timeout=timeout, **kwargs)
        jacc, self.session = LNS.join(jreq.msg.pdu, jreq.reg, **kwargs)
        kwargs.setdefault('rx1delay', ld.JaccRxDelay)
        self.dn(jreq, jacc, **kwargs)

    def verify(self, lwm:LoraWanMsg, *, expectport:Optional[int]=None, **kwargs:Any) -> rt.types.Msg:
        updf = LNS.unpack(self.session, lwm.msg.pdu)
        if expectport is not None:
            expect.assert_equal(expectport, updf['FPort'], DeviceTest.explain('port mismatch', **kwargs))
            #assert expectport == updf['FPort'], DeviceTest.explain('port mismatch', **kwargs)
        return updf

    async def updf(self, *, timeout:Optional[float]=None,
            filter:Callable[[LoraWanMsg],bool]=lambda m: True, limit:int=1,
            **kwargs:Any) -> LoraWanMsg:
        loop = asyncio.get_running_loop()
        deadline = timeout and loop.time() + timeout
        for _ in range(limit):
            timeout = deadline and max(0, deadline - loop.time())
            upmsg = await self.up(timeout=timeout)
            upmsg.rtm = self.verify(upmsg, **kwargs)
            if filter(upmsg):
                return upmsg
        assert False, DeviceTest.explain(f'No matching message received within limit of {limit} messages', **kwargs)

    def dndf(self, uplwm:LoraWanMsg, port:Optional[int]=None, payload:Optional[bytes]=None, *,
            fctrl:int=0, fopts:Optional[bytes]=None, confirmed:bool=False, invalidmic:bool=False, fcntdn_adj:int=0, **kwargs:Any) -> None:
        pdu = lm.pack_dataframe(
                mhdr=(lm.FrmType.DCDN if confirmed else lm.FrmType.DADN) | lm.Major.V1,
                devaddr=self.session['devaddr'],
                fcnt=self.session['fcntdn'] + fcntdn_adj,
                fctrl=fctrl,
                fopts=fopts,
                port=port,
                payload=payload,
                nwkskey=self.session['nwkskey'],
                appskey=self.session['appskey'])
        if invalidmic:
            pdu = pdu[:-4] + bytes(map(lambda x: ~x & 0xff, pdu[-4:]))
        if fcntdn_adj >= 0:
            self.session['fcntdn'] += (1 + fcntdn_adj)
        self.dn(uplwm, pdu, **kwargs)
