# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

import asyncio

from devtest import vtime, DeviceTest
from peripherals import FastUART, GPIO
from perso import PTE, PTESerialPort, PersoData, PersoDataV1
from rtlib import Eui

from ward import fixture, test

class SimPTESerialPort(PTESerialPort):
    def __init__(self, sim) -> None:
        self.sim = sim
        self.uart = None

    def reinit(self):
        self.uart = None

    def send(self, data:bytes) -> None:
        if self.uart == None:
            self.uart = self.sim.get_peripheral(FastUART)
        self.uart.send(data)

    async def recv(self) -> bytes:
        if self.uart == None:
            self.uart = self.sim.get_peripheral(FastUART)
        return await self.uart.recv()

class SimPTE(PTE):
    def __init__(self, dut):
        super().__init__(SimPTESerialPort(dut.sim))
        self.dut = dut

    def activate(self, active):
        self.dut.sim.get_peripheral(GPIO).drive(24, active)


@fixture
async def createtest(_=vtime):
    dut = DeviceTest()
    dut.start()
    yield SimPTE(dut)
    await dut.stop()


@test('Enter Personalization Mode')
async def _(pte=createtest):
    pte.activate(True)
    await asyncio.sleep(1)
    await pte.nop()


@test('Reset Command')
async def _(pte=createtest):
    pte.activate(True)
    await asyncio.sleep(1)
    await pte.reset()

    # Set I/O pin to re-enter perso
    pte.port.reinit()
    pte.activate(True)
    await asyncio.sleep(1)
    await pte.nop()
    await pte.reset()

    # GPIO is no longer set, so we should enter "regular" mode
    await pte.dut.join()
    await pte.dut.updf()


@test('Read/Write EEPROM Command')
async def _(pte=createtest):
    pte.activate(True)
    await asyncio.sleep(1)

    # Write new personalization data to EEPROM
    deveui = Eui('01-02-03-04-05-06-07-08')
    joineui = Eui('F1-F2-F3-F4-F5-F6-F7-F8')
    nwkkey = b'QWERTYUIASDFGHJK'
    appkey = b'qwertyuiasdfghjk'
    pd = PersoDataV1(0, 0, 'TestSerial', deveui, joineui, nwkkey, appkey)
    await pte.ee_write(0x0060, pd.pack())

    # Read back personalization data
    pd2 = PersoData.unpack(await pte.ee_read(0x0060, PersoData.V1_SIZE))
    assert pd == pd2

    # Reset and verify device joins with new settings
    await pte.reset()
    await pte.dut.join(deveui=deveui, nwkkey=nwkkey)
    await pte.dut.updf()
