# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
# Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import Generator, List, Optional, Tuple

import asyncio
import itertools
import sys

import loramsg as lm
import loradefs as ld
import loraopts as lo

from lorawan import LNS, LoraWanMsg
from lwtest import LWTest, PowerStats
from vtimeloop import VirtualTimeLoop

from ward import fixture, test, Scope


@fixture(scope=Scope.Module)
def vtime():
    loop = asyncio.get_event_loop()
    asyncio.set_event_loop(VirtualTimeLoop())
    yield
    asyncio.set_event_loop(loop)


@fixture
async def createtest(_=vtime):
    dut = LWTest()
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
        m = await dut.upstats(m, 16 * len(chans), fstats=fstats)
        assert set(fstats.keys()) == set(ch[0] for ch in chans), f'{jo}'

    return True


@test('2.4 Packet error rate RX2 default DR')
async def _(dut=createtest):
    m = await dut.start_testmode()

    dc0 = dut.unpack_dnctr(m)

    # Note: this test does not make a lot of sense in the simulation...
    ct = 60
    for i in range(ct):
        dut.request_mode(m, False, rx2=True)
        m = await dut.test_updf()
    per = (ct - (dut.unpack_dnctr(m) - dc0)) / ct
    print(f'Packet error rate: {(per/100):.1f} %')
    assert per < .05


@test('2.5 Cryptography')
async def _(dut=createtest):
    m = await dut.start_testmode()

    # a. AES Encryption
    for i in range(1,19):
        m = await dut.echo(m, bytes(range(1, i + 1)))

    # b. MIC
    m = await dut.test_updf()
    dc = dut.unpack_dnctr(m)
    for i in range(3):
        m = dut.request_echo(m, bytes(range(1, i + 1)), invalidmic=True)
        m = await dut.test_updf()
        dut.unpack_dnctr(m, expected=dc)


@test('2.6 Downlink Window Timing')
async def _(dut=createtest):
    m = await dut.start_testmode()

    for rx2, toff in itertools.product([False, True], [20e-6, -20e-6]):
        m = await dut.echo(m, b'\1\2\3', rx2=rx2, toff=toff)


@test('2.7 Frame Sequence Number')
async def _(dut=createtest):
    m = await dut.start_testmode()

    # c. [sic] Uplink sequence number
    for i in range(10):
        up0 = m.rtm['FCnt']
        m = await dut.test_updf()
        assert m.rtm['FCnt'] == up0 + 1

    # d. [sic] Downlink sequence number
    dc = dut.unpack_dnctr(m)
    for i in range(10):
        dut.request_mode(m, False)
        m = await dut.test_updf()
        dc = dut.unpack_dnctr(m, expected=dc+1)
    for adj in [-3, -4, -2]:
        dut.request_mode(m, False, fcntdn_adj=adj)
        m = await dut.test_updf()
        dc = dut.unpack_dnctr(m, expected=dc)


@test('2.8 DevStatusReq MAC Command')
async def _(dut=createtest):
    m = await dut.start_testmode()

    dut.dndf(m, 0, lo.pack_opts([lo.DevStatusReq()]))
    m = await dut.updf()

    opts = m.unpack_opts()
    assert len(opts) == 1
    assert type(opts[0]) is lo.DevStatusAns
    print(opts[0])


@test('2.9 MAC commands')
async def _(dut=createtest):
    m = await dut.start_testmode()

    dc = dut.unpack_dnctr(m)

    for _ in range(2):
        cmd = lo.pack_opts([lo.DevStatusReq()])
        dut.dndf(m, 0, cmd, fopts=cmd)
        m = await dut.updf()

        opts = m.unpack_opts()
        assert len(opts) == 0 and m.rtm['FPort'] != 0

    m = await dut.test_updf()
    dc = dut.unpack_dnctr(m, expected=dc)


@test('2.10 NewChannelReq MAC Command')
async def _(dut=createtest):
    m = await dut.start_testmode()

    # create a new region with additional channels
    reg = ld.Region_EU868()
    reg.upchannels.extend([ ld.ChDef(freq=f, minDR=0, maxDR=5) for f in (867100000, 867300000, 867500000, 868850000) ])
    dut.gateway.regions.append(reg)

    # helper function
    async def ncr_add(m:LoraWanMsg, chans:List[Tuple[int,int]]) -> LoraWanMsg:
        opts = [lo.NewChannelReq(Chnl=ch, Freq=f//100, MinDR=0, MaxDR=5)
                for ch, f in chans]
        dut.dndf(m, 0, lo.pack_opts(opts))

        m = await dut.test_updf()
        opts = m.unpack_opts()
        assert len(opts) == len(chans)
        for i, o in enumerate(opts):
            if chans[i][0] < len(dut.session['region'].upchannels):
                dut.check_ncr_o(o, ChnlAck=0, DRAck=None)
            else:
                dut.check_ncr_o(o)

        return await dut.check_freqs(m, frozenset(itertools.chain(
            (ch[1] for ch in chans if ch[1]),
            (ch.freq for ch in dut.session['region'].upchannels))))

    # e. [sic] Read-only default channels
    m = await ncr_add(m, list(zip(range(0,3), [0,0,0])))

    # f. [sic] Addition and removal of multiple channels
    m = await ncr_add(m, list(zip(range(3,6), [867100000, 867300000, 867500000])))
    m = await ncr_add(m, list(zip(range(3,6), [0, 0, 0])))

    # g. [sic] Addition of a single channel
    m = await ncr_add(m, [(3, 868850000)])

    # h. [sic] Removal of a single channel
    m = await ncr_add(m, [(3, 0)])


@test('2.11 DlChannelReq MAC Command')
async def _(dut=createtest):
    m = await dut.start_testmode()

    region = dut.session['region']

    for f in [ 868500000, region.upchannels[1].freq, 0 ]:
        # modify channel 1 RX1 frequency
        dut.dndf(m, 0, lo.pack_opts([lo.DlChannelReq(Chnl=1, Freq=f//100)]))

        # wait until message is received on channel 1 AND
        # simultaneously ensure that the DlChannelAns is being
        # repeated while no DL is received
        for i in range(32):
            m = await dut.updf()
            opts = m.unpack_opts()
            assert len(opts) == 1, f'i={i}, f={f}'
            o, = opts
            assert type(o) is lo.DlChannelAns
            assert o.ChnlAck.value == 1
            assert o.FreqAck.value == (1 if f else 0)
            if i and m.ch == 1:
                break
        else:
            assert False, 'no message received on modified channel'

        # save current DL counter and send DL on modified channel freq
        dc = dut.unpack_dnctr(m)
        dut.request_mode(m, False, freq=f or None)

        # check uplink -- DlChannelAns must be cleared
        # *and* DL counter incremented
        m = await dut.test_updf()
        opts = m.unpack_opts()
        assert len(opts) == 0, f'f={f}'
        dut.unpack_dnctr(m, expected=dc+1)

        # make sure we get a message on an unmodified channel, otherwise
        # next command won't be received..
        while m.ch == 1:
            m = await dut.test_updf()

    # Note: the following part of the test expands upon what's required...
    for ch, f in [(1, 333333333), (3, 868500000)]:
        # attempt to modify channel
        dut.dndf(m, 0, lo.pack_opts([lo.DlChannelReq(Chnl=ch, Freq=f//100)]))

        # check that the command is rejected for the correct reason, and
        # simultaneously ensure that the DlChannelAns is being repeated
        # while no DL is received
        for i in range(32):
            m = await dut.updf()
            opts = m.unpack_opts()
            assert len(opts) == 1, f'ch={ch}, f={f}'
            o, = opts
            assert type(o) is lo.DlChannelAns
            if ch < len(region.upchannels):
                assert o.ChnlAck.value == 1
                assert o.FreqAck.value == 0
            else:
                assert o.ChnlAck.value == 0
                assert o.FreqAck.value == 1
            if i and m.ch == 1:
                break
        else:
            assert False, 'no message received on channel 1'

        # save current DL counter and send DL
        dc = dut.unpack_dnctr(m)
        dut.request_mode(m, False)

        # check uplink -- DlChannelAns must be cleared
        # *and* DL counter incremented
        m = await dut.test_updf()
        opts = m.unpack_opts()
        assert len(opts) == 0, f'f={f}'
        dut.unpack_dnctr(m, expected=dc+1)

        # make sure invalid channel didn't get enabled somehow
        m = await dut.check_freqs(m, frozenset(ch.freq for ch in region.upchannels))


@test('2.12 Confirmed Packets')
async def _(dut=createtest):
    m = await dut.start_testmode()

    assert not m.isconfirmed()
    dc = dut.unpack_dnctr(m)
    dut.request_mode(m, True)

    # a. Uplink confirmed packets
    m = await dut.test_updf()
    assert m.isconfirmed()
    dc = dut.unpack_dnctr(m, expected=dc+1)

    dut.dndf(m, fctrl=lm.FCtrl.ACK) # empty downlink as ACK

    m = await dut.test_updf()
    assert m.isconfirmed()
    dc = dut.unpack_dnctr(m, expected=dc+1)

    # b. Uplink retransmission
    m2 = await dut.test_updf()
    assert m2.msg.pdu == m.msg.pdu
    m = m2

    dut.dndf(m, fctrl=lm.FCtrl.ACK) # empty downlink as ACK

    m = await dut.test_updf()
    assert m.isconfirmed()
    dc = dut.unpack_dnctr(m, expected=dc+1)

    # switch back to unconfirmed
    dut.request_mode(m, False)
    m = await dut.test_updf()
    assert not m.isconfirmed()
    dc = dut.unpack_dnctr(m, expected=dc+1)

    # c. Downlink confirmed packet
    # d. Downlink retransmission
    for fcntdn_adj in [0, 0, -1]:
        dut.request_mode(m, False, confirmed=True, fcntdn_adj=fcntdn_adj)

        m = await dut.test_updf()
        assert m.isack()

        # counter should also be increased (unless it was a repeat)
        dc = dut.unpack_dnctr(m, expected=(dc if fcntdn_adj else dc+1), explain=f'fcntdn_adj={fcntdn_adj}')


@test('2.13 RXParamSetupReq MAC Command')
async def _(dut=createtest):
    m = await dut.start_testmode()

    region = dut.session['region']

    # helper function
    def check_rpsa(m:LoraWanMsg, msg:str) -> None:
        opts = m.unpack_opts()
        assert len(opts) == 1, msg
        opt, = opts
        assert type(opt) == lo.RXParamSetupAns, msg
        assert opt.FreqAck.value == 1, msg
        assert opt.RX2DRAck.value == 1, msg
        assert opt.RX1DRoffAck.value == 1, msg

    # Make sure we are DR5 so we can see the rx1droff effect
    assert m.dr == 5

    # -- Modify RX1 and RX2 downlink parameters
    rx1droff, rx2dr, rx2freq = 2, 2, 868525000
    opt = lo.RXParamSetupReq(RX2DR=rx2dr, RX1DRoff=rx1droff, Freq=rx2freq//100)
    dut.dndf(m, 0, lo.pack_opts([opt]))

    with dut.modified_session(rx1droff=rx1droff, rx2dr=rx2dr, rx2freq=rx2freq):
        m = await dut.updf()
        check_rpsa(m, None)

        m = await dut.echo(m, b'\1\2\3')
        m = await dut.echo(m, b'\4\5\6', rx2=True)

        # -- Restore default downlink parameters
        dut.dndf(m, 0, lo.pack_opts([lo.RXParamSetupReq(RX2DR=region.RX2DR, RX1DRoff=0, Freq=region.RX2Freq//100)]))

    # -- Test reply transmission
    for i in range(2):
        m = await dut.updf()
        check_rpsa(m, f'iteration {i+1}')

    m = await dut.echo(m, b'\1\2\3')
    assert len(m.unpack_opts()) ==  0
    m = await dut.echo(m, b'\1\2\3', rx2=True)


@test('2.14 RXTimingSetupReq Command')
async def _(dut=createtest):
    m = await dut.start_testmode()

    def check_rtsa(m:LoraWanMsg, msg:str) -> None:
        opts = m.unpack_opts()
        assert len(opts) == 1, msg
        opt, = opts
        assert type(opt) == lo.RXTimingSetupAns, msg

    # -- Modify RX1 and RX2 timing to X second delay
    for delay in range(1, 16):
        dut.dndf(m, 0, lo.pack_opts([lo.RXTimingSetupReq(Delay=delay)]))
        dut.session['rx1delay'] = delay
        m = await dut.updf()
        check_rtsa(m, f'rxdelay={delay}')
        m = await dut.echo(m, b'\1\2\3')
        assert len(m.unpack_opts()) == 0, f'rxdelay={delay}'
        m = await dut.echo(m, b'\4\5\6', rx2=True)

    # -- Restore default timing
    dut.dndf(m, 0, lo.pack_opts([lo.RXTimingSetupReq(Delay=0)]))
    dut.session['rx1delay'] = 1

    # -- Test reply transmission
    for i in range(2):
        m = await dut.updf()
        check_rtsa(m, f'iteration {i+1}')

    m = await dut.echo(m, b'\1\2\3')
    assert len(m.unpack_opts()) == 0
    m = await dut.echo(m, b'\4\5\6', rx2=True)


@test('2.15 LinkADRReq MAC Command')
async def _(dut=createtest):
    m = await dut.start_testmode()

    def check_laa(m:LoraWanMsg, msg:str) -> None:
        opts = m.unpack_opts()
        assert len(opts) == 1, msg
        opt, = opts
        dut.check_laa_o(opt, explain=msg)

    def check_laa_block(m:LoraWanMsg, n:int, *, ChAck:Optional[int]=1, DRAck:Optional[int]=1, TXPowAck:Optional[int]=1, msg:str) -> None:
        opts = m.unpack_opts()
        assert len(opts) == n, msg
        # check that all have the correct type
        assert [type(o) for o in opts] == [lo.LinkADRAns for _ in range(n)], msg
        # check that all have the same value
        opt = opts[-1]
        assert list(opts) == [opt for _ in range(n)], msg
        # verify last one (others are identical)
        dut.check_laa_o(opt, ChAck, DRAck, TXPowAck, explain=msg)

    async def ncr_optdr(m:LoraWanMsg, freq:int, msg:str) -> LoraWanMsg:
        dut.dndf(m, 0, lo.pack_opts([lo.NewChannelReq(Chnl=3, Freq=freq//100, MinDR=0, MaxDR=7)]))
        m = await dut.updf(explain=msg)
        opts = m.unpack_opts()
        assert len(opts) == 1, msg
        opt, = opts
        dut.check_ncr_o(opt, explain=msg)
        return m

    # a. ADR bit
    assert m.isadren()

    # b. TXPower
    dut.dndf(m, 0, lo.pack_opts([lo.LinkADRReq(TXPow=7, DR=5, ChMaskCntl=6)]))
    m = await dut.updf()
    check_laa(m, 'txpower=7')

    pstats = PowerStats()
    m = await dut.upstats(m, 3, pstats=pstats)
    rssi0 = pstats.avg()

    dut.dndf(m, 0, lo.pack_opts([lo.LinkADRReq(TXPow=0, DR=5, ChMaskCntl=6)]))
    m = await dut.updf()
    check_laa(m, 'txpower=0')

    pstats.reset()
    m = await dut.upstats(m, 3, pstats=pstats)
    rssi1 = pstats.avg()

    print(f'RSSI @  2dBm: {rssi0:6.1f} dBm')
    print(f'RSSI @ 16dBm: {rssi1:6.1f} dBm')
    print(f'Difference:   {rssi1-rssi0:6.1f} dBm')
    assert rssi0 > -80 and rssi0 < -10
    assert rssi1 > -80 and rssi1 < -10
    assert (rssi1 - rssi0) >= 6

    # c. Required DataRates
    for dr in range(6):
        dut.dndf(m, 0, lo.pack_opts([lo.LinkADRReq(TXPow=0, DR=dr, ChMaskCntl=6)]))
        m = await dut.updf()
        check_laa(m, f'dr={dr}')
        assert m.dr == dr

    # d. Optional DataRates
    nchannel = ld.ChDef(freq=869100000, minDR=0, maxDR=7)
    reg = ld.Region_EU868()
    reg.upchannels.append(nchannel)
    dut.gateway.regions.append(reg)

    m = await ncr_optdr(m, nchannel.freq, 'create new channel')
    for dr in range(6, 8):
        dut.dndf(m, 0, lo.pack_opts([lo.LinkADRReq(TXPow=0, DR=dr, ChMaskCntl=6)]))
        m = await dut.updf()
        check_laa(m, f'dr={dr}')
        assert m.msg.freq == nchannel.freq, f'dr={dr}'
        assert m.dr == dr, f'dr={dr}'
    m = await ncr_optdr(m, 0, 'delete new channel')
    assert m.msg.freq in list(ch.freq for ch in dut.session['region'].upchannels)

    # e. ChannelMask
    dut.dndf(m, 0, lo.pack_opts([lo.NewChannelReq(Chnl=3, Freq=nchannel.freq//100, MinDR=0, MaxDR=5),
        lo.LinkADRReq(TXPow=5, DR=5, ChMaskCntl=0, ChMask=7)]))

    m = await dut.updf()
    opts = m.unpack_opts()
    assert len(opts) == 2
    opt1, opt2 = opts
    dut.check_ncr_o(opt1)
    dut.check_laa_o(opt2)

    m = await dut.check_freqs(m, frozenset(ch.freq for ch in dut.session['region'].upchannels))

    dut.dndf(m, 0, lo.pack_opts([lo.LinkADRReq(TXPow=5, DR=5, ChMaskCntl=0, ChMask=0xf)]))

    m = await dut.updf()
    check_laa(m, 'chmask=0xf')

    m = await dut.check_freqs(m, frozenset(ch.freq for ch in reg.upchannels))

    dut.dndf(m, 0, lo.pack_opts([lo.LinkADRReq(TXPow=5, DR=5, ChMaskCntl=0, ChMask=0)]))

    m = await dut.updf()
    opts = m.unpack_opts()
    assert len(opts) == 1
    opt, = opts
    dut.check_laa_o(opt, ChAck=0, DRAck=None, TXPowAck=None)

    dut.dndf(m, 0, lo.pack_opts([lo.NewChannelReq(Chnl=3, Freq=0)]))

    m = await dut.updf()
    opts = m.unpack_opts()
    assert len(opts) == 1
    opt, = opts
    dut.check_ncr_o(opt)

    # f. Redundancy
    dut.dndf(m, 0, lo.pack_opts([lo.LinkADRReq(DR=5, ChMaskCntl=6, NbTrans=2)]))
    m = await dut.updf()
    check_laa(m, 'nbtrans=2')

    l = [await dut.updf() for _ in range(3)]

    assert l[0].rtm['MIC'] == m.rtm['MIC']
    assert l[2].rtm['MIC'] == l[1].rtm['MIC']

    m = l[-1]
    dut.dndf(m, 0, lo.pack_opts([lo.LinkADRReq(DR=5, ChMaskCntl=6, NbTrans=1)]))
    m = await dut.updf()
    check_laa(m, 'nbtrans=1')

    # g. ADRACKReq bit
    dut.dndf(m)

    for i in range(64):
        m = await dut.updf()
        assert m.isadrarq() == False, f'iter={i}'
        assert m.dr == 5, f'iter={i}'
    for dr in [5, 4, 3]:
        for i in range(32):
            m = await dut.updf()
            assert m.isadrarq() == True, f'dr={dr}, iter={i}'
            assert m.dr == dr, f'dr={dr}, iter={i}'

    dut.dndf(m, 0, lo.pack_opts([lo.LinkADRReq(DR=5, ChMaskCntl=6)]))
    m = await dut.updf()
    check_laa(m, 'dr=5')
    assert m.dr == 5

    # h. a.. Successful LinkADRReq commands block
    dut.dndf(m, 0, lo.pack_opts([lo.LinkADRReq(ChMaskCntl=0, ChMask=0),
        lo.LinkADRReq(TXPow=4, DR=4, ChMaskCntl=0, ChMask=3, NbTrans=1),
        lo.LinkADRReq(TXPow=0, DR=3, ChMaskCntl=6, ChMask=0, NbTrans=1)]))
    m = await dut.updf()
    check_laa_block(m, 3, msg='linkadrreq block')

    m = await dut.check_freqs(m, frozenset(ch.freq for ch in dut.session['region'].upchannels))

    dut.dndf(m, 0, lo.pack_opts([lo.LinkADRReq(DR=5, ChMaskCntl=6)]))
    m = await dut.updf()
    check_laa(m, 'dr=5')
    assert m.dr == 5

    # h. b.. Unsuccessful LinkADRReq commands block
    dut.dndf(m, 0, lo.pack_opts([lo.LinkADRReq(ChMask=0x07, DR=4, TXPow=4),
        lo.LinkADRReq(ChMaskCntl=0, ChMask=0)]))

    m = await dut.updf()
    check_laa_block(m, 2, ChAck=0, DRAck=None, TXPowAck=None, msg='invalid linkadrreq block')
    assert m.dr == 5

    dut.dndf(m) # empty downlink to avoid timeout (?)
    m = await dut.check_freqs(m, frozenset(ch.freq for ch in dut.session['region'].upchannels))
