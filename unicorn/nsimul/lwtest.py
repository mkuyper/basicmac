# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
# Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import cast, Any, Dict, Generator, List, Optional, Set, Tuple

import contextlib
import struct

from dataclasses import dataclass

import loramsg as lm
import loraopts as lo

from devtest import explain, DeviceTest
from lorawan import LNS, LoraWanMsg, Session

from ward import expect

@dataclass
class PowerStats:
    accu:float = 0.0
    count:int = 0

    def avg(self) -> float:
        return (self.accu / self.count) if self.count else 0

    def reset(self) -> None:
        self.accu = 0.0
        self.count = 0


class LWTest(DeviceTest):
    def request_test(self, uplwm:LoraWanMsg, **kwargs:Any) -> None:
        self.dndf(uplwm, port=224, payload=b'\1\1\1\1', **kwargs)

    def request_echo(self, uplwm:LoraWanMsg, echo:bytes, **kwargs:Any) -> None:
        self.dndf(uplwm, port=224, payload=b'\x04' + echo, **kwargs)

    def request_mode(self, uplwm:LoraWanMsg, mode_conf:bool, **kwargs:Any) -> None:
        self.dndf(uplwm, port=224, payload=b'\x02' if mode_conf else b'\x03', **kwargs)

    def request_rejoin(self, uplwm:LoraWanMsg, **kwargs:Any) -> None:
        self.dndf(uplwm, port=224, payload=b'\x06', **kwargs)

    @staticmethod
    def unpack_dnctr(lwm:LoraWanMsg, *, expected:Optional[int]=None, **kwargs:Any) -> int:
        assert lwm.rtm is not None
        payload = lwm.rtm['FRMPayload'];
        try:
            dnctr, = cast(Tuple[int], struct.unpack('>H', payload))
        except struct.error as e:
            raise ValueError(f'invalid payload: {payload.hex()}') from e
        if expected is not None:
            expect.assert_equal(expected, dnctr, explain('Unexpected downlink counter', **kwargs))
        return dnctr

    @staticmethod
    def unpack_echo(lwm:LoraWanMsg, *, orig:Optional[bytes]=None, **kwargs:Any) -> bytes:
        assert lwm.rtm is not None
        payload:bytes = lwm.rtm['FRMPayload'];
        expect.assert_equal(0x04, payload[0], explain('Invalid echo packet', **kwargs))
        echo = payload[1:]
        if orig is not None:
            expected = bytes((x + 1) & 0xff for x in orig)
            expect.assert_equal(expected, echo, explain('Unexpected echo response', **kwargs))
        return echo

    async def test_updf(self, **kwargs:Any) -> LoraWanMsg:
        kwargs.setdefault('timeout', 60)
        return await self.updf(port=224, **kwargs)


    # join network, start test mode, return first test upmsg
    async def start_testmode(self, **kwargs:Any) -> LoraWanMsg:
        kwargs.setdefault('timeout', 60)
        await self.join(**kwargs)

        m = await self.updf(**kwargs)
        self.request_test(m)

        return await self.test_updf(**kwargs)

    async def echo(self, uplwm:LoraWanMsg, echo:bytes, **kwargs:Any) -> LoraWanMsg:
        self.request_echo(uplwm, echo, **kwargs)

        m = await self.updf()
        self.unpack_echo(m, orig=echo, **kwargs)

        return m

    async def upstats(self, m:LoraWanMsg, count:int, *,
            fstats:Optional[Dict[int,int]]=None,
            pstats:Optional[PowerStats]=None) -> LoraWanMsg:
        for _ in range(count):
            if fstats is not None:
                f = m.msg.freq
                fstats[f] = fstats.get(f, 0) + 1
            if pstats is not None:
                rssi = m.msg.rssi
                assert rssi is not None
                pstats.accu += rssi
                pstats.count += 1
            m = await self.test_updf()
        return m

    # check NewChannelAns
    @staticmethod
    def check_ncr_o(o:lo.Opt, ChnlAck:Optional[int]=1, DRAck:Optional[int]=1, **kwargs:Any) -> None:
        expect.assert_equal(type(o), lo.NewChannelAns, explain('Unexpected MAC command', **kwargs))
        o = cast(lo.NewChannelAns, o)
        if ChnlAck is not None:
            expect.assert_equal(o.ChnlAck.value, ChnlAck, explain('Unexpected ChnlAck value', **kwargs)) # type: ignore
        if DRAck is not None:
            expect.assert_equal(o.DRAck.value, DRAck, explain('Unexpected DRAck value', **kwargs)) # type: ignore

    # check LinkADRAns
    @staticmethod
    def check_laa_o(o:lo.Opt, ChAck:Optional[int]=1, DRAck:Optional[int]=1, TXPowAck:Optional[int]=1, **kwargs:Any) -> None:
        expect.assert_equal(type(o), lo.LinkADRAns, explain('Unexpected MAC command', **kwargs))
        o = cast(lo.LinkADRAns, o)
        if ChAck is not None:
            expect.assert_equal(o.ChAck.value, ChAck, explain('Unexpected ChAck', **kwargs)) # type: ignore
        if DRAck is not None:
            expect.assert_equal(o.DRAck.value, DRAck, explain('Unexpected DRAck', **kwargs)) # type: ignore
        if TXPowAck is not None:
            expect.assert_equal(o.TXPowAck.value, TXPowAck, explain('Unexpected TXPowAck', **kwargs)) # type: ignore

    # check frequency usage
    async def check_freqs(self, m:LoraWanMsg, freqs:Set[int], count:Optional[int]=None, **kwargs:Any) -> LoraWanMsg:
        if count is None:
            count = 16 * len(freqs)
        fstats:Dict[int,int] = {}
        m = await self.upstats(m, count, fstats=fstats)
        expect.assert_equal(fstats.keys(), freqs, explain('Unexpected channel usage', **kwargs))
        return m

    def rps2dr(self, m:LoraWanMsg) -> int:
        assert self.session is not None
        return LNS.rps2dr(self.session['region'], m.msg.rps)

    @contextlib.contextmanager
    def modified_session(self, **kwargs:Any) -> Generator[None,None,None]:
        session = self.session
        assert session is not None
        changed:Dict[str,Any] = {}
        added:List[str] = []
        for key in kwargs:
            if key in session:
                changed[key] = session[key]
            else:
                added.append(key)
            session[key] = kwargs[key]
        yield
        session.update(changed)
        for key in added:
            del session[key]
