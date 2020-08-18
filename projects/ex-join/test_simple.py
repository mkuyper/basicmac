# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import Generator, List

import asyncio
import sys

from device import Simulation
from eventhub import ColoramaStream, LoggingEventHub
from lorawan import LNS, UniversalGateway
from medium import LoraMsg, SimpleMedium
from runtime import Runtime
from vtimeloop import VirtualTimeLoop

from ward import fixture, test

async def gwloop(gw:UniversalGateway) -> None:
    while True:
        msg = await gw.next_up()
        print(f'gw recv: {msg}')
        nmsg = LoraMsg(msg.xend + 5, bytes.fromhex('0102030405060708'), freq=msg.freq, rps=msg.rps | 0x10080)
        gw.sched_dn(nmsg)


@fixture
async def create_env() -> Generator[UniversalGateway,None,None]:
    hexfiles=['../../basicloader/build/boards/simul-unicorn/bootloader.hex', 'build-simul/ex-join.hex']
    rt = Runtime()

    log = LoggingEventHub(ColoramaStream(sys.stdout))
    med = SimpleMedium()

    gw = UniversalGateway(rt, med)

    sim = Simulation(rt, context={ 'evhub': log, 'medium': med})
    for hf in hexfiles:
        sim.load_hexfile(hf)

    simtask = asyncio.create_task(sim.run())

    yield gw
    
    simtask.cancel()

@test("simple first test")
async def _(gw=create_env):
    m = await gw.next_up()
    print(f'gw recv: {m}')

    LNS().join(m)

asyncio.set_event_loop(VirtualTimeLoop())
