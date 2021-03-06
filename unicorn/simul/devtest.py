# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import Any, Callable, List, Generator, Optional, TextIO

import asyncio
import os
import shlex
import sys

from ward import expect, fixture, Scope
from colorama import Fore, Style

import rtlib as rt
import loradefs as ld
import loramsg as lm
import loraopts as lo

from device import Simulation
from eventhub import EventHub
from lorawan import LNS, LoraWanFormatter, LoraWanMsg, Gateway, Session, SessionManager, UniversalGateway
from medium import LoraMsg, Rps, SimpleMedium
from peripherals import Radio
from runtime import Runtime
from vtimeloop import VirtualTimeLoop

def explain(s:Optional[str]='', *, explain:Optional[str]=None, **kwargs:Any) -> Optional[str]:
    if s is None:
        if explain is None:
            return None
        else:
            s = ''
    return s if explain is None else f'{s} ({explain})'

@fixture(scope=Scope.Module) # type: ignore
def vtime() -> Generator[None,None,None]:
    loop = asyncio.get_event_loop()
    asyncio.set_event_loop(VirtualTimeLoop()) # type: ignore
    yield
    asyncio.set_event_loop(loop)

class LogWriter:
    def __init__(self, buf:TextIO) -> None:
        self.buf = buf

    def write(self, s:str, **kwargs:Any) -> None:
        try:
            self.buf.write(s)
        except ValueError:
            pass

class ColoramaStream(LogWriter):
    def write(self, s:str, style:str='', **kwargs:Any) -> None:
        super().write(f'{style}{s}{Style.RESET_ALL}')

class LoggingEventHub(EventHub):
    def __init__(self, writer:LogWriter, *, sm:Optional[SessionManager]=None) -> None:
        self.writer = writer
        self.lwf = LoraWanFormatter(sm)

    @staticmethod
    def src2str(src:Any) -> str:
        if isinstance(src, Gateway):
            return 'G'
        elif isinstance(src, Radio):
            return 'D'
        else:
            return '?'

    def event(self, type:int, **kwargs:Any) -> None:
        if type == EventHub.LOG:
            self.writer.write(str(kwargs['msg']), style=Fore.BLUE)
        if type == EventHub.LORA:
            m:LoraMsg = kwargs['msg']
            s = f'{self.src2str(m.src)}-> '
            s += str(m)
            if (info := self.lwf.format_msg(kwargs['msg'])) is not None:
                s += f' -- {info}'
            self.writer.write(f'{s}\n', style=Fore.GREEN)

class DeviceTest:
    def __init__(self, *, hexfiles:Optional[List[str]]=None) -> None:
        self.runtime = Runtime()
        self.sm = SessionManager()
        self.log = LoggingEventHub(ColoramaStream(sys.stdout), sm=self.sm)
        self.medium = SimpleMedium(evhub=self.log)
        self.gateway = UniversalGateway(self.runtime, self.medium)
        self.session:Optional[Session] = None

        self.sim = Simulation(self.runtime, context={ 'evhub': self.log, 'medium': self.medium})

        if hexfiles is None:
            hexfiles = shlex.split(os.environ.get('TEST_HEXFILES', ''))

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

    async def up(self, *, timeout:Optional[float]=None, **kwargs:Any) -> LoraWanMsg:
        return await asyncio.wait_for(self.gateway.next_up(), timeout)

    def dn(self, uplwm:LoraWanMsg, pdu:bytes, *, rx2:bool=False, rx1delay:int=0, xpow:Optional[float]=None,
            toff:float=0, freq:Optional[int]=None,
            join:bool=False, **kwargs:Any) -> None:
        assert self.session is not None
        rxdelay = rx1delay or self.session['rx1delay']
        if rx2:
            rxdelay += 1
            (f, rps) = LNS.dn_rx2(self.session, join)
        else:
            (f, rps) = LNS.dn_rx1(self.session, uplwm.msg.freq, uplwm.msg.rps, join)
        if xpow is None:
            xpow = uplwm.reg.max_eirp
        if freq is None:
            freq = f
        self.gateway.sched_dn(LoraMsg(uplwm.msg.xend + rxdelay + toff, pdu, freq, rps, xpow=xpow))

    async def join(self, *, timeout:Optional[float]=None, region:Optional[ld.Region]=None, **kwargs:Any) -> None:
        jreq = await self.up(timeout=timeout, **kwargs)
        if region:
            if not isinstance(region, type(jreq.reg)):
                raise ValueError(explain(f'Incompatible regions {region} and {jreq.reg}', **kwargs))
        else:
            region = jreq.reg

        if self.session:
            self.sm.remove(self.session)
        jacc, self.session = LNS.join(jreq.msg.pdu, region, **kwargs)
        self.sm.add(self.session)
        kwargs.setdefault('rx1delay', ld.JaccRxDelay)
        self.dn(jreq, jacc, join=True, **kwargs)

    def verify(self, lwm:LoraWanMsg, *, expectport:Optional[int]=None, **kwargs:Any) -> rt.types.Msg:
        assert self.session is not None
        try:
            updf = LNS.unpack(self.session, lwm.msg.pdu)
        except (ValueError, lm.VerifyError) as e:
            raise ValueError(explain('Verification failed', **kwargs)) from e
        if expectport is not None:
            expect.assert_equal(expectport, updf['FPort'], explain('port mismatch', **kwargs))
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
        assert False, explain(f'No matching message received within limit of {limit} messages', **kwargs)

    def dndf(self, uplwm:LoraWanMsg, port:Optional[int]=None, payload:Optional[bytes]=None, *,
            fctrl:int=0, fopts:Optional[bytes]=None, confirmed:bool=False, invalidmic:bool=False, fcntdn_adj:int=0, **kwargs:Any) -> None:
        assert self.session is not None
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
