# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import Any, Dict, List, MutableMapping, Tuple

import asyncio
import numpy

from binascii import crc32

import loramsg as lm
import loradefs as ld
import rtlib as rt

from medium import LoraMsg, LoraMsgProcessor, LoraMsgTransmitter, Medium, Rps
from runtime import Runtime

Session = MutableMapping[str,Any]

class UniversalGateway(LoraMsgProcessor):
    def __init__(self, runtime:Runtime, medium:Medium, regions:List[ld.Region]=[ld.EU868,ld.US915]) -> None:
        self.runtime = runtime
        self.medium = medium
        self.regions = regions

        self.upframes:asyncio.Queue[LoraMsg] = asyncio.Queue()
        self.xmtr = LoraMsgTransmitter(runtime, medium)

        medium.add_listener(self)

    def msg_complete(self, msg:LoraMsg) -> None:
        if not Rps.isIqInv(msg.rps):
            self.upframes.put_nowait(msg)

    async def next_up(self) -> lm.Msg:
        return self.unpack(await self.upframes.get())

    def sched_dn(self, msg:LoraMsg) -> None:
        self.xmtr.transmit(msg)

    def getupch(self, msg:LoraMsg) -> Tuple[ld.Region,int]:
        for r in self.regions:
            dr = r.to_dr(*Rps.getSfBw(msg.rps)).dr
            for (idx, ch) in enumerate(r.upchannels):
                if msg.freq == ch.freq and dr >= ch.minDR and dr <= ch.maxDR:
                    return (r, idx)
        raise ValueError(f'Channel not defined in regions {", ".join(r.name for r in self.regions)}: '
                f'{msg.freq/1e6:.6f}MHz/{Rps.sfbwstr(msg.rps)}')

    def unpack(self, msg:LoraMsg) -> lm.Msg:
        m = lm.unpack_nomic(msg.pdu)
        m['region'], m['ch'] = self.getupch(msg)
        m['upmsg'] = msg
        return m


class SessionManager:
    def __init__(self) -> None:
        self.addr2sess:Dict[int,Dict[int,Session]] = {}
        self.eui2sess:Dict[int,Dict[int,Session]] = {}

    def add(self, s:Session) -> None:
        devaddr:int = s['devaddr']
        deveui:int = s['deveui']
        self.addr2sess.setdefault(devaddr, {})[deveui] = s
        self.eui2sess.setdefault(deveui, {})[devaddr] = s

    @staticmethod
    def _remove(outer:Dict[int,Dict[int,Session]], k1:int, k2:int) -> None:
        if (inner := outer.get(k1)) is not None:
            if inner.pop(k2, None):
                if not inner:
                    outer.pop(k1)

    def remove(self, s:Session) -> None:
        devaddr:int = s['devaddr']
        deveui:int = s['deveui']
        SessionManager._remove(self.addr2sess, devaddr, deveui)
        SessionManager._remove(self.eui2sess, deveui, devaddr)

    @staticmethod
    def _get(d:Dict[int,Dict[int,Session]], k:int) -> List[Session]:
        return list(d.get(k, {}).values())

    def get_by_addr(self, devaddr:int) -> List[Session]:
        return SessionManager._get(self.addr2sess, devaddr)

    def get_by_eui(self, deveui:int) -> List[Session]:
        return SessionManager._get(self.eui2sess, deveui)

    def all(self) -> List[Session]:
        return list(v for d in self.addr2sess.values() for v in d.values())

class LNS:
    def __init__(self) -> None:
        self.sm = SessionManager()

    def join(self, m:lm.Msg, **kwargs:Any) -> LoraMsg:
        assert m['msgtype'] == 'jreq'

        msg = m['upmsg']

        nwkkey = kwargs.get('nwkkey', b'@ABCDEFGHIJKLMNO')
        appnonce = kwargs.get('appnonce', 0)
        netid = kwargs.get('netid', 1)
        devaddr = kwargs.get('devaddr', numpy.int32(crc32(rt.Eui(m['DevEUI']).as_bytes())))
        rxdly = kwargs.get('rxdly', 0)
        (rx1droff, rx2dr, optneg) = lm.DLSettings.unpack(kwargs.get('dlset',
                    lm.DLSettings.pack(0, m['region'].RX2DR, False)))

        lm.verify_jreq(nwkkey, msg.pdu)
        print(m['DevEUI'])

        maxnonce = max((s['DevNonce'] for s in self.sm.get_by_eui(m['DevEUI'])), default=-1)
        devnonce = m['DevNonce']
        if maxnonce >= devnonce:
            raise ValueError('DevNonce is not strictly increasing')



