# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
# Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import Generator, List

import asyncio
import sys

import loramsg as lm
import loradefs as ld

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

@test('2.3 Over The Air Activation')
async def _(dut=createtest):
    m = await dut.start_testmode()

    # create a new region with an additional channel (for test part 3)
    extra_ch = [ ld.ChDef(freq=867850000, minDR=0, maxDR=5) ]
    reg = ld.Region_EU868()
    reg.upchannels += extra_ch

    dut.gateway.regions.append(reg)

    joinopts = [
            { 'dlset': lm.DLSettings.pack(rx1droff=2, rx2dr=3, optneg=False) },
            { 'rxdly': 2 },
            { 'region': reg },
            { 'rx2': True } ]

    for jo in joinopts:
        dut.request_rejoin(m)

        await dut.start_testmode(**jo, explain=f'join options: {jo}')

        # test rx1 and rx2
        for rx2 in [ False, True ]:
            m = await dut.test_updf()
            dc = dut.unpack_dnctr(m)
            dut.request_mode(m, False, rx2=rx2)

            m = await dut.test_updf()
            dc = dut.unpack_dnctr(m, expected=dc+1, explain=f'{jo}/{"rx2" if rx2 else "rx1"}')

        # test used frequencies
        fstats = {}
        chans = dut.session['region'].upchannels
        m = await dut.upstats(16 * len(chans), fstats=fstats)
        assert set(fstats.keys()) == set(ch[0] for ch in chans), explain(f'{jo}')

    return True



asyncio.set_event_loop(VirtualTimeLoop())
