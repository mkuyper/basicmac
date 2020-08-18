# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import Any, List, Tuple

import asyncio
import numpy

from binascii import crc32

import loramsg as lm
import loradefs as ld
import rtlib as rt

from medium import LoraMsg, LoraMsgProcessor, LoraMsgTransmitter, Medium, Rps
from runtime import Runtime

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

    async def next_up(self) -> LoraMsg:
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


class LNS:
    def __init__(self) -> None:
        pass

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



