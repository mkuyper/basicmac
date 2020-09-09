# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import cast, Any, Dict, List, MutableMapping, Optional, Tuple

import asyncio
import numpy
import struct

from binascii import crc32
from dataclasses import dataclass

import loracrypto as lc
import loramsg as lm
import loradefs as ld
import loraopts as lo
import rtlib as rt

from medium import LoraMsg, LoraMsgProcessor, LoraMsgTransmitter, Medium, Rps
from runtime import Runtime

Session = MutableMapping[str,Any]

@dataclass
class LoraWanMsg:
    msg:LoraMsg
    reg:ld.Region
    ch:int
    dr:int
    rtm:Optional[rt.types.Msg]=None

    def isconfirmed(self) -> bool:
        m = self.rtm
        assert m is not None
        return (m['MHdr'] & lm.MHdr.FTYPE) in [lm.FrmType.DCUP, lm.FrmType.DCDN]

    def isack(self) -> bool:
        m = self.rtm
        assert m is not None
        return (int(m['FCtrl']) & lm.FCtrl.ACK) != 0

    def unpack_opts(self) -> List[lo.Opt]:
        m = self.rtm
        assert m is not None
        if m['FPort'] == 0:
            opts = m['FRMPayload']
            assert len(m['FOpts']) == 0
        else:
            opts = m['FOpts']
        return lo.unpack_optsup(opts)

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

    async def next_up(self) -> LoraWanMsg:
        msg = await self.upframes.get()
        reg, ch, dr = self.getupparams(msg)
        return LoraWanMsg(msg, reg, ch, dr)

    def sched_dn(self, msg:LoraMsg) -> None:
        self.xmtr.transmit(msg)

    def getupparams(self, msg:LoraMsg) -> Tuple[ld.Region,int,int]:
        for r in self.regions:
            dr = r.to_dr(*Rps.getSfBw(msg.rps)).dr
            for (idx, ch) in enumerate(r.upchannels):
                if msg.freq == ch.freq and dr >= ch.minDR and dr <= ch.maxDR:
                    return (r, idx, dr)
        raise ValueError(f'Channel not defined in regions {", ".join(r.name for r in self.regions)}: '
                f'{msg.freq/1e6:.6f}MHz/{Rps.sfbwstr(msg.rps)}')


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
    def dn_rx1(session:Session, upfreq:int, uprps:int, join:bool=False) -> Tuple[int,int]:
        region = session['region']
        rx1droff = 0 if join else session['rx1droff']
        return (region.get_dnfreq(upfreq),
                LNS.dndr2rps(region, region.get_dndr(LNS.rps2dr(region, uprps), rx1droff)))

    @staticmethod
    def dn_rx2(session:Session, join:bool=False) -> Tuple[int,int]:
        region = session['region']
        rx2dr = region.RX2DR if join else session['rx2dr']
        rx2freq = region.RX2Freq if join else session['rx2freq']
        return (rx2freq, LNS.dndr2rps(region, rx2dr))

    @staticmethod
    def join(pdu:bytes, region:ld.Region, *, pdevnonce:int=-1, nwkkey:bytes=b'@ABCDEFGHIJKLMNO',
            appnonce:int=0, netid:int=1, devaddr:Optional[int]=None, dlset:Optional[int]=None, rxdly:int=0,
            **kwargs:Any) -> Tuple[bytes,Session]:
        jreq = lm.unpack_jreq(pdu)
        deveui = rt.Eui(jreq['DevEUI'])

        if devaddr is None:
            devaddr = numpy.int32(crc32(struct.pack('q', deveui)))
        if dlset is None:
            dlset = lm.DLSettings.pack(0, region.RX2DR, False)

        rx1droff, rx2dr, optneg = lm.DLSettings.unpack(dlset)

        lm.verify_jreq(nwkkey, pdu)

        cflist = region.get_cflist()

        devnonce = jreq['DevNonce']
        if pdevnonce >= devnonce:
            raise ValueError('DevNonce is not strictly increasing')

        nwkskey = lc.crypto.derive(nwkkey, devnonce, appnonce, netid, lm.KD_NwkSKey)
        appskey = lc.crypto.derive(nwkkey, devnonce, appnonce, netid, lm.KD_AppSKey)

        return lm.pack_jacc(nwkkey, appnonce, netid, devaddr, dlset, rxdly, cflist, devnonce=devnonce), {
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
                'rx2freq'   : region.RX2Freq,
                'devnonce'  : devnonce,
                'region'    : region,
                }

    def try_unpack(self, pdu:bytes, devaddr:int) -> Tuple[Session,rt.types.Msg]:
        for s in self.sm.get_by_addr(devaddr):
            try:
                return s, lm.unpack_dataframe(pdu, s['fcntup'], s['nwkskey'], s['appskey'])
            except lm.VerifyError:
                pass
        raise lm.VerifyError(f'no matching session found for devaddr {devaddr}')

    @staticmethod
    def unpack(session:Session, pdu:bytes) -> rt.types.Msg:
        updf = lm.unpack_dataframe(pdu, session['fcntup'], session['nwkskey'], session['appskey'])
        assert session['devaddr'] == updf['DevAddr']
        session['fcntup'] = updf['FCnt']
        return updf

    @staticmethod
    def dl(session:Session, port:Optional[int]=None, payload:Optional[bytes]=None, *,
            fctrl:int=0, fopts:Optional[bytes]=None, confirmed:bool=False, invalidmic:bool=False, fcntdn_adj:int=0, **kwargs:Any) -> bytes:
        pdu = lm.pack_dataframe(
                mhdr=(lm.FrmType.DCDN if confirmed else lm.FrmType.DADN) | lm.Major.V1,
                devaddr=session['devaddr'],
                fcnt=session['fcntdn'] + fcntdn_adj,
                fctrl=fctrl,
                fopts=fopts,
                port=port,
                payload=payload,
                nwkskey=session['nwkskey'],
                appskey=session['appskey'])
        if invalidmic:
            pdu = pdu[:-4] + bytes(map(lambda x: ~x & 0xff, pdu[-4:]))
        if fcntdn_adj >= 0:
            session['fcntdn'] += (1 + fcntdn_adj)
        return pdu
