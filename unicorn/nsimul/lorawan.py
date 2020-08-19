# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import cast, Any, Dict, List, MutableMapping, Tuple

import asyncio
import numpy
import struct

from binascii import crc32

import loracrypto as lc
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

    async def next_up(self) -> rt.types.Msg:
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

    def unpack(self, msg:LoraMsg) -> rt.types.Msg:
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
        deveui:int = s['deveui'].as_int()
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
        deveui:int = s['deveui'].as_int()
        SessionManager._remove(self.addr2sess, devaddr, deveui)
        SessionManager._remove(self.eui2sess, deveui, devaddr)

    @staticmethod
    def _get(d:Dict[int,Dict[int,Session]], k:int) -> List[Session]:
        return list(d.get(k, {}).values())

    def get_by_addr(self, devaddr:int) -> List[Session]:
        return SessionManager._get(self.addr2sess, devaddr)

    def get_by_eui(self, deveui:rt.Eui) -> List[Session]:
        return SessionManager._get(self.eui2sess, deveui.as_int())

    def get(self, deveui:rt.Eui, devaddr:int) -> Session:
        return self.eui2sess[deveui.as_int()][devaddr]

    def all(self) -> List[Session]:
        return list(v for d in self.addr2sess.values() for v in d.values())

class LNS:
    def __init__(self) -> None:
        self.sm = SessionManager()

    @staticmethod
    def rps2dr(region:ld.Region, rps:int) -> int:
        return region.to_dr(*Rps.getSfBw(rps)).dr

    @staticmethod
    def dndr2rps(region:ld.Region, dr:int) -> int:
        dndr = region.DRs[dr]
        return Rps.makeRps(sf=dndr.sf, bw=dndr.bw*1000, crc=0, iqinv=True)

    @staticmethod
    def up2dn_rx1(session:Session, freq:int, rps:int) -> Tuple[int,int]:
        region = session['region']
        updr = LNS.rps2dr(region, rps)
        return (region.get_dnfreq(freq),
                LNS.dndr2rps(region, region.get_dndr(LNS.rps2dr(region, rps), session['rx1droff'])))

    @staticmethod
    def dn_rx2(session:Session) -> Tuple[int,int]:
        region = session['region']
        return (region.RX2Freq, LNS.dndr2rps(region, session['rx2dr']))

    def join(self, m:rt.types.Msg, *, rx2:bool=False, **kwargs:Any) -> Tuple[LoraMsg,rt.Eui,int]:
        assert m['msgtype'] == 'jreq'

        msg = m['upmsg']

        deveui = rt.Eui(m['DevEUI'])
        region = m['region']

        nwkkey = kwargs.setdefault('nwkkey', b'@ABCDEFGHIJKLMNO')
        appnonce = kwargs.setdefault('appnonce', 0)
        netid = kwargs.setdefault('netid', 1)
        devaddr = kwargs.setdefault('devaddr', numpy.int32(crc32(struct.pack('q', deveui))))
        rxdly = kwargs.setdefault('rxdly', 0)
        (rx1droff, rx2dr, optneg) = lm.DLSettings.unpack(kwargs.setdefault('dlset',
                    lm.DLSettings.pack(0, region.RX2DR, False)))

        lm.verify_jreq(nwkkey, msg.pdu)

        maxnonce = max((s['DevNonce'] for s in self.sm.get_by_eui(deveui)), default=-1)
        devnonce = m['DevNonce']
        if maxnonce >= devnonce:
            raise ValueError('DevNonce is not strictly increasing')

        nwkskey = lc.crypto.derive(nwkkey, devnonce, appnonce, netid, lm.KD_NwkSKey)
        appskey = lc.crypto.derive(nwkkey, devnonce, appnonce, netid, lm.KD_AppSKey)

        jacc = lm.pack_jacc(**kwargs)

        session = {
                'deveui'    : deveui,
                'devaddr'   : devaddr,
                'nwkkey'    : nwkkey,
                'nwkskey'   : nwkskey,
                'appskey'   : appskey,
                'fcntup'    : 0,
                'fcntdn'    : 0,
                'rx1delay'  : max(rxdly, 1),
                'rx1droff'  : rx1droff,
                'rx2dr'     : rx2dr,
                'devnonce'  : devnonce,
                'region'    : region,
                }
 
        rxdelay = ld.JaccRxDelay
        if rx2:
            rxdelay += 1
            (freq, rps) = LNS.dn_rx2(session)
        else:
            (freq, rps) = LNS.up2dn_rx1(session, msg.freq, msg.rps)

        self.sm.add(session)
        return LoraMsg(msg.xend + rxdelay, jacc, freq, rps, xpow=region.max_eirp), deveui, devaddr

    def try_unpack(self, pdu:bytes, devaddr:int) -> Tuple[Session,rt.types.Msg]:
        for s in self.sm.get_by_addr(devaddr):
            try:
                return s, lm.unpack_dataframe(pdu, s['fcntup'], s['nwkskey'], s['appskey'])
            except lm.VerifyError:
                pass
        raise lm.VerifyError(f'no matching session found for devaddr {devaddr}')

    def verify(self, m:rt.types.Msg) -> Session:
        assert m['msgtype'] == 'updf'
        session, updf = self.try_unpack(m['upmsg'].pdu, m['DevAddr'])
        session['fcntup'] = m['FCnt']
        m.update(updf)
        return session
