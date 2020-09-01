# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
# Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import cast, Any, Dict, Optional, Tuple

import struct

from ward import expect

from devtest import explain, DeviceTest
from lorawan import LoraWanMsg

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
            expect.assert_equal(expected, dnctr, explain(None, **kwargs))
        return dnctr

    @staticmethod
    def unpack_echo(lwm:LoraWanMsg, *, orig:Optional[bytes]=None, **kwargs:Any) -> bytes:
        assert lwm.rtm is not None
        payload:bytes = lwm.rtm['FRMPayload'];
        expect.assert_equal(0x04, payload[0], explain(None, **kwargs))
        echo = payload[1:]
        if orig is not None:
            expected = bytes((x + 1) & 0xff for x in orig)
            expect.assert_equal(expected, echo, explain(None, **kwargs))
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

    async def upstats(self, count:int, *,
            fstats:Optional[Dict[int,int]]=None) -> LoraWanMsg:
        for _ in range(count):
            m = await self.test_updf()
            if fstats is not None:
                f = m.msg.freq
                fstats[f] = fstats.get(f, 0) + 1
        return m
