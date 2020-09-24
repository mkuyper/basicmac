# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import Any, List, Optional, Set

import asyncio
import ctypes

from uuid import UUID

from device import IrqHandler, Peripheral, Peripherals, Simulation
from medium import LoraMsg, LoraMsgReceiver, LoraMsgTransmitter, Medium
from runtime import Clock, Job, Runtime


# -----------------------------------------------------------------------------
# Peripheral: NVIC

@Peripherals.add
class NVIC(Peripheral, IrqHandler):
    uuid = UUID('439a2c60-ac1b-11ea-99f0-d1119d1d4e55')

    @Peripherals.register
    class NVICRegister(ctypes.LittleEndianStructure):
        _fields_ = [('vtor', ctypes.c_uint32 * 128), ('prio', ctypes.c_ubyte * 128)]

    def init(self) -> None:
        self.reg = NVIC.NVICRegister()
        self.sim.map_peripheral(self.pid, self.reg)
        self.sim.irqhandler = self

        self.reqs:Set[int] = set()
        self.cprio:List[int] = [-1]

    def requested(self) -> bool:
        return bool(self.reqs)

    def handler(self) -> Optional[int]:
        assert bool(self.reqs)
        pid = sorted(self.reqs, key=lambda x: self.reg.prio[x], reverse=True)[0]
        prio = self.reg.prio[pid]
        if prio <= self.cprio[-1]:
            return None
        self.cprio.append(prio)
        return int(self.reg.vtor[pid])

    def done(self) -> None:
        p = self.cprio.pop()
        assert p != -1

    def set(self, pid:int) -> None:
        assert pid < 128
        self.reqs.add(pid)
        self.sim.running.set()

    def clear(self, pid:int) -> None:
        assert pid < 128
        self.reqs.discard(pid)


# -----------------------------------------------------------------------------
# Peripheral: Debug

@Peripherals.add
class DebugUnit(Peripheral):
    uuid = UUID('4c25d84a-9913-11ea-8de8-23fb8fc027a4')

    @Peripherals.register
    class DebugRegister(ctypes.LittleEndianStructure):
        _fields_ = [('n', ctypes.c_uint32), ('s', ctypes.c_ubyte * 1024)]

    def init(self) -> None:
        self.reg = DebugUnit.DebugRegister()
        self.sim.map_peripheral(self.pid, self.reg)

    def svc(self, fid:int) -> None:
        assert fid == 0
        self.sim.log(bytes(self.reg.s[:self.reg.n]).decode('utf-8'))


# -----------------------------------------------------------------------------
# Peripheral: Timer

@Peripherals.add
class Timer(Peripheral, Clock):
    uuid = UUID('20c98436-994e-11ea-8de8-23fb8fc027a4')

    TICKS_PER_SEC = 32768

    @Peripherals.register
    class TimerRegister(ctypes.LittleEndianStructure):
        _fields_ = [('ticks', ctypes.c_uint64), ('target', ctypes.c_uint64)]

    def init(self) -> None:
        self.epoch = asyncio.get_running_loop().time()
        self.reg = Timer.TimerRegister()
        self.sim.map_peripheral(self.pid, self.reg)
        self.sim.prerunhooks.append(self.update)
        self.sim.runtime.setclock(self)
        self.th:Optional[asyncio.TimerHandle] = None
        self.update()

    def update(self) -> None:
        self.reg.ticks = self.time2ticks(asyncio.get_running_loop().time())

    def time(self, update:bool=False) -> float:
        return self.ticks2time(self.ticks(update))

    def ticks(self, update:bool=False) -> int:
        if update:
            self.update()
        return int(self.reg.ticks)

    def ticks2time(self, ticks:int) -> float:
        return self.epoch + (ticks / Timer.TICKS_PER_SEC)

    def time2ticks(self, time:float) -> int:
        return int((time - self.epoch) * Timer.TICKS_PER_SEC)

    def sec2ticks(self, sec:float) -> int:
        return round(sec * Timer.TICKS_PER_SEC)

    def cancel(self) -> None:
        if self.th is not None:
            self.th.cancel()
            self.th = None

    def alarm(self) -> None:
        self.sim.running.set()

    def svc(self, fid:int) -> None:
        assert fid == 0
        self.cancel()
        self.th = asyncio.get_running_loop().call_at(
                self.epoch + (self.reg.target / Timer.TICKS_PER_SEC), self.alarm)


# -----------------------------------------------------------------------------
# Peripheral: Radio

@Peripherals.add
class Radio(Peripheral):
    uuid = UUID('3888937c-ab4c-11ea-aeed-27009b59e638')

    S_IDLE   = 0
    S_BUSY   = 1
    S_TXDONE = 2
    S_RXDONE = 3
    S_RXTOUT = 4

    @Peripherals.register
    class RadioRegister(ctypes.LittleEndianStructure):
        _fields_ = [('buf', ctypes.c_ubyte*256), ('xtime', ctypes.c_uint64), *[(r, ctypes.c_uint32) for r in ('plen', 'freq', 'rps', 'xpow', 'rssi', 'snr', 'npreamble', 'status')]]

    def init(self) -> None:
        self.reg = Radio.RadioRegister()
        self.sim.map_peripheral(self.pid, self.reg)
        self.medium:Medium = self.sim.context.get('medium', Medium())
        self.rcvr = LoraMsgReceiver(self.sim.runtime, self.medium, cb=self.rxdone)
        self.xmtr = LoraMsgTransmitter(self.sim.runtime, self.medium, cb=self.txdone)

    def txdone(self, msg:LoraMsg) -> None:
        self.reg.status = Radio.S_TXDONE
        self.reg.xtime = self.sim.runtime.clock.time2ticks(msg.xend)
        self.sim.irqhandler.set(self.pid)

    def rxdone(self, msg:Optional[LoraMsg]) -> None:
        if msg:
            self.reg.status = Radio.S_RXDONE
            self.reg.xtime = self.sim.runtime.clock.time2ticks(msg.xend)
            self.reg.buf[:len(msg.pdu)] = msg.pdu
            self.reg.plen = len(msg.pdu)
            pass
        else:
            self.reg.status = Radio.S_RXTOUT
            self.reg.xtime = self.sim.runtime.clock.ticks(update=True)
        self.sim.irqhandler.set(self.pid)

    def svc_reset(self) -> None:
        pass

    def svc_clearirq(self) -> None:
        self.sim.irqhandler.clear(self.pid)

    def svc_rx(self) -> None:
        t = self.sim.runtime.clock.ticks2time(self.reg.xtime)
        self.rcvr.receive(t, self.reg.freq, self.reg.rps, minsyms=self.reg.npreamble)

    def svc_tx(self) -> None:
        now = self.sim.runtime.clock.time()
        msg = LoraMsg(now, bytes(self.reg.buf[:self.reg.plen]), self.reg.freq, self.reg.rps,
                xpow=self.reg.xpow, npreamble=self.reg.npreamble, src=self)
        self.xmtr.transmit(msg)

    svc_lookup = {
            0: svc_reset,
            1: svc_tx,
            2: svc_rx,
            3: svc_clearirq,
            }

    def svc(self, fid:int) -> None:
        Radio.svc_lookup[fid](self)
