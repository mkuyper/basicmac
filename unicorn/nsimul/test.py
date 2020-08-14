# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

import argparse
import asyncio
import sys

from device import Simulation
from eventhub import ColoramaStream, LoggingEventHub
from lorawan import UniversalGateway
from medium import LoraMsg, SimpleMedium
from runtime import Runtime


async def gwloop(gw:UniversalGateway) -> None:
    while True:
        msg = await gw.next_up()
        print(f'gw recv: {msg}')
        nmsg = LoraMsg(msg.xend + 5, bytes.fromhex('0102030405060708'), freq=msg.freq, rps=msg.rps | 0x10080)
        gw.sched_dn(nmsg)

async def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument('-v', '--virtual-time', action='store_true',
            help='Use virtual time')
    p.add_argument('hexfiles', metavar='HEXFILE', nargs='+',
            help='Firmware files to load')
    args = p.parse_args()

    rt = Runtime()

    log = LoggingEventHub(ColoramaStream(sys.stdout))
    med = SimpleMedium()

    gw = UniversalGateway(rt, med)

    sim = Simulation(rt, context={ 'evhub': log, 'medium': med})
    for hf in args.hexfiles:
        sim.load_hexfile(hf)

    await asyncio.gather(gwloop(gw), sim.run())


if __name__ == '__main__':
    asyncio.get_event_loop().run_until_complete(main())
