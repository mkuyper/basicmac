# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import Optional

import asyncio
import ctypes

from uuid import UUID

from device import Peripheral, Peripherals, Simulation


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
class Timer(Peripheral):
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
        self.th:Optional[asyncio.TimerHandle] = None
        self.update()

    def update(self) -> None:
        now = asyncio.get_running_loop().time() - self.epoch
        self.reg.ticks = int(now * Timer.TICKS_PER_SEC)

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
