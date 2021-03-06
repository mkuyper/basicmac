# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
# Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import Any, Callable, Optional, Set, Tuple

import asyncio
import math

from eventhub import EventHub
from runtime import Job, JobGroup, Runtime

class Rps:
    # Extensions that are not in LMiC's 16-bit RPS
    IQINV = (1 << 16)

    @staticmethod
    def makeRps(sf:int=7, bw:int=125000, cr:int=1, crc:int=1, ih:int=0, *, iqinv:bool=False) -> int:
        return ((sf-6) | ([125000,250000,500000].index(bw)<<3)
                | ((cr-1)<<5) | ((crc^1)<<7) | ((ih&0xFF)<<8)
                | (Rps.IQINV if iqinv else 0)) if sf else 0

    @staticmethod
    def getSf(rps:int) -> int:
        sf = rps & 0x7
        return sf + 6 if sf else 0

    @staticmethod
    def getBw(rps:int) -> int:
        return (1 << ((rps >> 3) & 0x3)) * 125000

    @staticmethod
    def getCr(rps:int) -> int:
        return ((rps >> 5) & 0x3) + 1

    @staticmethod
    def getCrc(rps:int) -> int:
        return ((rps >> 7) & 0x1) ^ 1

    @staticmethod
    def getIh(rps:int) -> int:
        return (rps >> 8) & 0xff

    @staticmethod
    def getParams(rps:int) -> Tuple[int,int,int,int,int]:
        return (Rps.getSf(rps),
                Rps.getBw(rps),
                Rps.getCr(rps),
                Rps.getCrc(rps),
                Rps.getIh(rps))

    @staticmethod
    def validate(rps:int) -> None:
        (sf, bw, cr, crc, ih) = Rps.getParams(rps)
        if sf:
            assert bw in [125000,250000,500000], f'unsupported bw: {bw}'
            assert sf >= 7 and sf <= 12,         f'unsupported sf: {sf}'
            assert cr >= 1 and cr <= 4,          f'unsupported cr: {cr}'
            assert ih==0 or ih==1,               f'unsupported ih: {ih}'
            assert crc==0 or crc==1,             f'unsupported crc: {crc}'

    @staticmethod
    def isFSK(rps:int) -> bool:
        return (rps & 0x7) == 0

    @staticmethod
    def isIqInv(rps:int) -> bool:
        return bool(rps & Rps.IQINV)

    @staticmethod
    def getSfBw(rps:int) -> Tuple[int,int]:
        sf = Rps.getSf(rps)
        bw = Rps.getBw(rps) if sf else 0
        return sf, bw

    @staticmethod
    def sfbwstr(rps:int) -> str:
        sf, bw = Rps.getSfBw(rps)
        return f'SF{sf}BW{bw//1000}' if sf else 'FSK'

class LoraMsg:
    def __init__(self, time:float, pdu:bytes, freq:int, rps:int, *,
            xpow:Optional[float]=None, rssi:Optional[float]=None, snr:Optional[float]=None,
            dro:Optional[int]=None, npreamble:int=8, src:Optional[Any]=None) -> None:

        assert len(pdu) >= 0 and len(pdu) <= 255
        Rps.validate(rps)

        sf = Rps.getSf(rps)
        bw = Rps.getBw(rps)
        if sf:
            if dro is None:
                dro = 1 if ((sf>=11 and bw==125000)
                        or  (sf==12 and bw==250000)) else 0
            else:
                assert dro==0 or dro==1
        else:
            dro = 0

        self.pdu = pdu
        self.freq = freq
        self.rps = rps
        self.xpow = xpow
        self.rssi = rssi
        self.snr = snr
        self.dro = dro
        self.npreamble = npreamble
        self.src = src

        Tpreamble, Tpayload = self.airtimes()
        self.xbeg = time
        self.xpld = time + Tpreamble
        self.xend = time + Tpreamble + Tpayload

    def __str__(self) -> str:
        sf = Rps.getSf(self.rps)
        bw = Rps.getBw(self.rps)
        return (f'xbeg={self.xbeg:.6f}, xend={self.xend:.6f}, freq={self.freq}, '
                f'{f"sf={sf}, bw={bw}" if sf else "fsk"}, '
                f'pdu={self.pdu.hex()}')

    def __repr__(self) -> str:
        return f'LoraMsg<{self.__str__()}>'

    def match(self, freq:int, rps:int) -> bool:
        return (self.freq == freq) and (Rps.isFSK(rps) if Rps.isFSK(self.rps)
                else (self.rps == rps))

    @staticmethod
    def symtime(rps:int, nsym:int=1) -> float:
        (sf, bw, cr, crc, ih) = Rps.getParams(rps)
        if sf == 0:
            return 8*nsym / 50000
        # Symbol rate / time for one symbol (secs)
        Rs = bw / (1<<sf)
        Ts = 1 / Rs
        return nsym * Ts

    def airtimes(self) -> Tuple[float,float]:
        Ts = LoraMsg.symtime(self.rps)
        if Rps.isFSK(self.rps):
            return (8*Ts, (3+1+2+len(self.pdu))*Ts)
        # Length/time of preamble
        Tpreamble = (self.npreamble + 4.25) * Ts
        # Symbol length of payload and time
        (sf, bw, cr, crc, ih) = Rps.getParams(self.rps)
        tmp = math.ceil(
                (8*len(self.pdu) - 4*sf + 28 + 16*crc - ih*20)
                / (4*sf - self.dro*8)) * (cr+4)
        npayload = 8 + max(0, tmp)
        Tpayload = npayload * Ts
        return (Tpreamble, Tpayload)

    def airtime(self) -> float:
        return sum(self.airtimes())

class LoraMsgProcessor:
    def msg_preamble(self, msg:LoraMsg, t:Optional[float]=None) -> None:
        pass

    def msg_payload(self, msg:LoraMsg) -> None:
        pass

    def msg_complete(self, msg:LoraMsg) -> None:
        pass

    def msg_abort(self, msg:LoraMsg) -> None:
        pass

class Medium(LoraMsgProcessor):
    def add_listener(self, proc:LoraMsgProcessor, t:Optional[float]=None) -> None:
        pass

    def remove_listener(self, proc:LoraMsgProcessor) -> None:
        pass

class SimpleMedium(Medium):
    def __init__(self, evhub:Optional[EventHub]=None) -> None:
        self.pmsg:Set['LoraMsg'] = set()
        self.listeners:Set['LoraMsgProcessor'] = set()
        self.evhub = evhub

    def add_listener(self, proc:LoraMsgProcessor, t:Optional[float]=None) -> None:
        self.listeners.add(proc)
        for msg in self.pmsg:
            proc.msg_preamble(msg, t)

    def remove_listener(self, proc:LoraMsgProcessor) -> None:
        self.listeners.remove(proc)

    def msg_preamble(self, msg:LoraMsg, t:Optional[float]=None) -> None:
        if self.evhub:
            self.evhub.event(EventHub.LORA, src=self, msg=msg)
        self.pmsg.add(msg)
        for l in self.listeners:
            l.msg_preamble(msg)

    def msg_payload(self, msg:LoraMsg) -> None:
        self.pmsg.discard(msg)
        for l in self.listeners:
            l.msg_payload(msg)

    def msg_complete(self, msg:LoraMsg) -> None:
        for l in self.listeners:
            l.msg_complete(msg)

    def msg_abort(self, msg:LoraMsg) -> None:
        self.pmsg.discard(msg)
        for l in self.listeners:
            l.msg_abort(msg)


TxDoneCb = Callable[['LoraMsg'], None]
RxDoneCb = Callable[[Optional['LoraMsg']], None]

class LoraMsgTransmitter():
    def __init__(self, runtime:Runtime, medium:Medium, *, cb:Optional[TxDoneCb]=None) -> None:
        self.jobs = JobGroup(runtime)
        self.medium = medium
        self.cb = cb
        self.msg:Optional[LoraMsg] = None

    def transmit(self, msg:LoraMsg) -> None:
        assert self.msg is None
        self.msg = msg
        self.jobs.schedule(None, msg.xbeg, self.txstart)

    def txstart(self) -> None:
        assert self.msg is not None
        self.medium.msg_preamble(self.msg)
        self.jobs.schedule(None, self.msg.xpld, self.txpayload)

    def txpayload(self) -> None:
        assert self.msg is not None
        self.medium.msg_payload(self.msg)
        self.jobs.schedule(None, self.msg.xend, self.txdone)

    def txdone(self) -> None:
        assert self.msg is not None
        self.medium.msg_complete(self.msg)
        if self.cb:
            self.cb(self.msg)
        self.msg = None

class LoraMsgReceiver(LoraMsgProcessor):
    def __init__(self, runtime:Runtime, medium:Medium, *, cb:Optional[RxDoneCb]=None, symdetect:int=5) -> None:
        self.jobs = JobGroup(runtime)
        self.medium = medium
        self.cb = cb
        self.symdetect = symdetect
        self.msg:Optional[LoraMsg] = None
        self.locked = False

    def receive(self, rxtime:float, freq:int, rps:int, *, minsyms:int=5) -> None:
        if Rps.isFSK(rps):
            rps = 0

        self.freq = freq
        self.rps = rps
        self.minsyms = minsyms
        self.rxtime = rxtime

        self.msg = None
        self.locked = False

        self.jobs.schedule(None, rxtime, self.rxstart)

    def rxstart(self) -> None:
        self.medium.add_listener(self, self.rxtime)
        self.jobs.schedule('timeout', self.rxtime + LoraMsg.symtime(self.rps, nsym=self.minsyms), self.timeout)

    def timeout(self) -> None:
        self.medium.remove_listener(self)
        self.jobs.cancel_all()
        if self.cb:
            self.cb(None)

    def msg_preamble(self, msg:LoraMsg, t:Optional[float]=None) -> None:
        if t is None:
            t = msg.xbeg
        if msg.freq == self.freq and msg.rps == self.rps and self.msg is None:
            self.msg = msg
            self.jobs.schedule('lock', t + LoraMsg.symtime(self.rps, nsym=self.symdetect), self.msg_lock)

    def msg_lock(self) -> None:
        self.jobs.cancel('timeout')
        self.locked = True

    def msg_payload(self, msg:LoraMsg) -> None:
        if msg == self.msg and not self.locked:
            self.jobs.cancel('lock')
            self.msg = None

    def msg_complete(self, msg:LoraMsg) -> None:
        if msg == self.msg and self.locked:
            if self.cb:
                self.cb(self.msg)
