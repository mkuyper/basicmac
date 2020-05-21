# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

import argparse
import asyncio
import sys

from device import Simulation
from eventhub import ColoramaStream, LoggingEventHub


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('-v', '--virtual-time', action='store_true',
            help='Use virtual time')
    p.add_argument('hexfiles', metavar='HEXFILE', nargs='+',
            help='Firmware files to load')
    args = p.parse_args()

    log = LoggingEventHub(ColoramaStream(sys.stdout))

    sim = Simulation(evhub=log)
    for hf in args.hexfiles:
        sim.load_hexfile(hf)

    asyncio.get_event_loop().run_until_complete(sim.run())
