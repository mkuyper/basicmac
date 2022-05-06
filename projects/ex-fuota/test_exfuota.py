# Copyright (C) 2020-2022 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

import asyncio

from devtest import vtime, DeviceTest

from ward import fixture, test

from zfwtool import Update
import frag
import random
import struct

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
            assert td >= 60
            assert td <= 70


@test('Downlink')
async def _(dut=createtest):
    await dut.join()
    m = await dut.updf()
    dut.dndf(m, 15, b'hi there')
    await asyncio.sleep(5)


@test('FUOTA')
async def _(dut=createtest):
    # load update file (self-contained update signed with testkey.pem)
    updata = open('hallosim.up', 'rb').read()
    up = Update.fromfile(updata)

    # short CRC of updated firmware
    dst_crc = up.fwcrc & 0xFFFF

    # short CRC of referenced firmware
    src_crc = 0

    # choose fragment size (multiple of 4, fragment data plus 8-byte header must fit LoRaWAN payload size!)
    frag_size = 192

    # initialize fragment generator
    fc = frag.FragCarousel.fromfile(updata, frag_size)

    await dut.join()
    for i in range(fc.cct + 20):
        try:
            m = await dut.updf()
        except ValueError:
            # device will send join-request after update complete
            break

        # print progress
        if m.rtm['FPort'] == 16:
            (fwcrc, donecnt, totalcnt) = struct.unpack('<IHH', m.rtm['FRMPayload'])
            print('current FW CRC: %08X, update progress %d/%d (%d%%)' % (fwcrc, donecnt, totalcnt, donecnt * 100 / totalcnt))

        # randomly select non-zero fragment index
        idx = random.randint(1, 65535)

        # generate FUOTA payload (session header plus fragment data)
        payload = struct.pack('<HHHH', src_crc, dst_crc, fc.cct, idx) + fc.chunk(idx)

        # send FUOTA downlink on port 16
        dut.dndf(m, 16, payload)
    else:
        assert False, 'Update did not complete within the expected amount of messages'

    # re-join and check new application message 'hallo' instead of 'hello'
    await dut.join()
    m = await dut.updf()
    assert m.rtm['FPort'] == 15 and m.rtm['FRMPayload'] == b'hallo'
