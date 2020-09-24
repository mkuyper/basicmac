# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

import asyncio

from devtest import vtime, DeviceTest

from ward import fixture, test

@fixture
async def createtest(_=vtime):
    dut = DeviceTest()
    dut.start()
    yield dut
    await dut.stop()


@test('Join')
async def _(dut=createtest):
    await dut.join()
    await dut.updf()


@test('Uplink')
async def _(dut=createtest):
    await dut.join()
    t1 = None
    for _ in range(5):
        m = await dut.updf()
        assert m.rtm['FRMPayload'] == b'hello'
        t0 = t1
        t1 = asyncio.get_event_loop().time()
        if t0 is not None:
            td = (t1 - t0)
            assert td >= 5
            assert td <= 10


@test('Downlink')
async def _(dut=createtest):
    await dut.join()
    m = await dut.updf()
    dut.dndf(m, 15, b'hi there')
    await asyncio.sleep(5)
