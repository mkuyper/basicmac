# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

import asyncio

from devtest import vtime, DeviceTest
from peripherals import FastUART, GPIO
from perso import PTE
from simperso import FUART_PTESerialPort

from ward import fixture, test

@fixture
async def createtest(_=vtime):
    dut = DeviceTest()
    dut.start()
    yield dut
    await dut.stop()


@test('Perso')
async def _(dut=createtest):
    gpio = dut.sim.get_peripheral(GPIO)
    pte = PTE(FUART_PTESerialPort(dut.sim.get_peripheral(FastUART)))
    gpio.drive(24, True)
    await asyncio.sleep(5)
    await pte.nop(timeout=5)
