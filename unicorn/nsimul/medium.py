# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
# Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import Callable, Optional, Set, Tuple

import asyncio
import math

from runtime import Job, Runtime

class Rps:
    @staticmethod
    def makeRps(sf:int=7, bw:int=125000, cr:int=1, crc:int=1, ih:int=0) -> int:
        return ((sf-6) | ([125000,250000,500000].index(bw)<<3)
                | ((cr-1)<<5) | ((crc^1)<<7) | ((ih&0xFF)<<8)) if sf else 0

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

TxDoneCb = Callable[['LoraMsg'], None]
RxDoneCb = Callable[[Optional['LoraMsg']], None]

class LoraMsg:
    def __init__(self, time:float, pdu:bytes, freq:int, rps:int, *,
            xpow:Optional[int]=None, rssi:Optional[int]=None, snr:Optional[int]=None,
            dro:Optional[int]=None, npreamble:int=8) -> None:

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
            return (5*Ts, (3+1+2+len(self.pdu))*Ts)
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

    def transmit(self, runtime:Runtime, proc:'LoraMsgProcessor', cb:Optional[TxDoneCb]=None) -> None:
        self.xrt = runtime
        self.xproc = proc
        self.xcb = cb
        self.xjob:Optional[Job] = self.xrt.schedule(self.xrt.clock.time2ticks(self.xbeg), self.tx_pre)

    def tx_pre(self) -> None:
        self.xproc.msg_preamble(self)
        self.xjob = self.xrt.schedule(self.xrt.clock.time2ticks(self.xpld), self.tx_pld)

    def tx_pld(self) -> None:
        self.xproc.msg_payload(self)
        self.xjob = self.xrt.schedule(self.xrt.clock.time2ticks(self.xend), self.tx_end)

    def tx_end(self) -> None:
        self.xproc.msg_complete(self)
        self.xjob = None
        if self.xcb:
            self.xcb(self)

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
    def __init__(self) -> None:
        self.pmsg:Set['LoraMsg'] = set()
        self.listeners:Set['LoraMsgProcessor'] = set()

    def add_listener(self, proc:LoraMsgProcessor, t:Optional[float]=None) -> None:
        self.listeners.add(proc)
        for msg in self.pmsg:
            proc.msg_preamble(msg, t)

    def remove_listener(self, proc:LoraMsgProcessor) -> None:
        self.listeners.remove(proc)

    def msg_preamble(self, msg:LoraMsg, t:Optional[float]=None) -> None:
        print(f'pre: {msg.xbeg}')
        self.pmsg.add(msg)
        for l in self.listeners:
            l.msg_preamble(msg)

    def msg_payload(self, msg:LoraMsg) -> None:
        print(f'pld: {msg.xpld}')
        self.pmsg.discard(msg)
        for l in self.listeners:
            l.msg_payload(msg)

    def msg_complete(self, msg:LoraMsg) -> None:
        print(f'cmp: {msg.xend}')
        for l in self.listeners:
            l.msg_complete(msg)

    def msg_abort(self, msg:LoraMsg) -> None:
        self.pmsg.discard(msg)
        for l in self.listeners:
            l.msg_abort(msg)

class LoraMsgReceiver(LoraMsgProcessor):
    def __init__(self, runtime:Runtime, medium:Medium, symdetect:int=5) -> None:
        self.rt = runtime
        self.medium = medium
        self.symdetect = symdetect

    def receive(self, freq:int, rps:int, *,
            rxtime:Optional[float]=None, minsyms:int=5, callback:Optional[RxDoneCb]=None) -> None:
        self.freq = freq
        self.rps = rps
        self.minsyms = minsyms
        self.callback = callback

        if rxtime is None:
            rxtime = self.rt.clock.time()

        self.rxtime = rxtime
        self.job1 = self.rt.schedule(rxtime, self.rxstart)
        self.job2:Optional[Job] = None
        self.msg:Optional[LoraMsg] = None

    def rxstart(self) -> None:
        print('rxstart')
        self.medium.add_listener(self, self.rxtime)
        self.job1 = self.rt.schedule(self.rxtime + LoraMsg.symtime(self.rps, nsym=self.minsyms), self.timeout)

    def timeout(self) -> None:
        print('timeout')
        if self.job2:
            self.job2.cancel()
        self.medium.remove_listener(self)
        if self.callback:
            self.callback(None)

    def msg_preamble(self, msg:LoraMsg, t:Optional[float]=None) -> None:
        print(f't={t}')
        if t is None:
            t = msg.xbeg
        print(f'l={t+LoraMsg.symtime(self.rps, nsym=self.symdetect)}')
        if msg.freq == self.freq and msg.rps == self.rps and self.job2 is None:
            self.job2 = self.rt.schedule(t + LoraMsg.symtime(self.rps, nsym=self.symdetect), self.msg_lock, msg=msg)

    def msg_lock(self, msg:LoraMsg) -> None:
        self.job1.cancel() # cancel timeout
        print(f'locking onto: {msg}')

    def msg_payload(self, msg:LoraMsg) -> None:
        if msg == self.msg:
            # and enough preamble overlap
            pass
