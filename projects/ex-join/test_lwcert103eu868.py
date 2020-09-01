# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import Generator, List

import asyncio
import sys

from lwtest import LWTest
from vtimeloop import VirtualTimeLoop

from ward import fixture, test

@fixture
async def createtest():
    dut = LWTest([
        '../../basicloader/build/boards/simul-unicorn/bootloader.hex',
        'build-simul/ex-join.hex'])
    dut.start()
    yield dut
    await dut.stop()

@test('2.1 Device Activation')
async def _(dut=createtest):
    await dut.start_testmode()


@test('2.2 Test Application Functionality')
async def _(dut=createtest):
    m = await dut.start_testmode()
    dc = dut.unpack_dnctr(m)

    await dut.echo(m, b'\x04\x01')

    m = await dut.test_updf()
    dut.unpack_dnctr(m, expected=dc+1)

    dut.request_mode(m, False)

    m = await dut.test_updf()
    dut.unpack_dnctr(m, expected=dc+2)



asyncio.set_event_loop(VirtualTimeLoop())
