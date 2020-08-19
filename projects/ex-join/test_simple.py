# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import Generator, List

import asyncio
import sys

from devtest import DeviceTest
from vtimeloop import VirtualTimeLoop

from ward import fixture, test

from foo import Foo

Foo.createtest

@fixture
async def createtest():
    dt = DeviceTest([
        '../../basicloader/build/boards/simul-unicorn/bootloader.hex',
        'build-simul/ex-join.hex'])
    dt.start()
    yield dt
    await dt.stop()

@test("simple first test")
async def _(dt=createtest):
    await dt.join()
    await dt.updf()

asyncio.set_event_loop(VirtualTimeLoop())
