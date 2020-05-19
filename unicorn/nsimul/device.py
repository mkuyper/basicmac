# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
# Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import cast, Any, Callable, Dict, List, Optional, TextIO, Tuple, Type

import argparse
import ctypes
import asyncio
import struct
import sys
import types

import unicorn as uc
import unicorn.arm_const as uca

from colorama import Fore, Style, init as colorama_init
from intelhex import IntelHex
from uuid import UUID

class EventHub:
    LOG  = 0
    LORA = 1

    def event(self, type:int, **kwargs:Any) -> None:
        raise NotImplementedError

    def log(self, src:Any, msg:str) -> None:
        self.event(EventHub.LOG, src=src, msg=msg)

class LogWriter:
    def __init__(self, buf:TextIO) -> None:
        self.buf = buf

    def write(self, s:str, **kwargs:Any) -> None:
        self.buf.write(s)

class ColoramaStream(LogWriter):
    def write(self, s:str, style:str='', **kwargs:Any) -> None:
        super().write(f'{style}{s}{Style.RESET_ALL}')

class LoggingEventHub(EventHub):
    def __init__(self, writer:LogWriter) -> None:
        self.writer = writer

    def event(self, type:int, **kwargs:Any) -> None:
        if type == EventHub.LOG:
            self.writer.write(cast(str, kwargs['msg']), style=Fore.BLUE)

class Peripheral:
    uuid:Optional[UUID] = None

    @staticmethod
    def create(sim:'Simulation', pid: int) -> 'Peripheral':
        raise NotImplementedError

    def svc(self, fid:int, p1:int, p2:int, p3:int) -> None:
        raise NotImplementedError

class Peripherals:
    peripherals:Dict[UUID,Type[Peripheral]] = {}

    @staticmethod
    def add(cls:Type[Peripheral]) -> Type[Peripheral]:
        assert(cls.uuid is not None)
        Peripherals.peripherals[cls.uuid] = cls
        return cls

    @staticmethod
    def register(cls:Type['ctypes._CData']) -> Type['ctypes._CData']:
        t = cast(Type[ctypes.Union], type(cls.__name__ + '_regpage', (ctypes.Union,), {}))
        t._anonymous_ = ('_regs',)
        t._fields_ = [('_regs', cls), ('_page', ctypes.c_uint8 * 0x1000)]
        return t

    @staticmethod
    def create(uuid:UUID, sim:'Simulation', irqline:int) -> Peripheral:
        return Peripherals.peripherals[uuid].create(sim, irqline)

PreRunHook = Callable[[], None]

@Peripherals.add
class DebugUnit(Peripheral):
    uuid = UUID('4c25d84a-9913-11ea-8de8-23fb8fc027a4')

    def __init__(self, sim:'Simulation'):
        self.sim = sim

    @staticmethod
    def create(sim:'Simulation', pid: int) -> Peripheral:
        return DebugUnit(sim)

    def svc(self, fid:int, p1:int, p2:int, p3:int) -> None:
        assert(fid == 0)
        self.sim.log(self.sim.get_string(p1, p2))

@Peripherals.add
class Timer(Peripheral):
    uuid = UUID('20c98436-994e-11ea-8de8-23fb8fc027a4')

    TICKS_PER_SEC = 32768

    @Peripherals.register
    class TimerRegister(ctypes.LittleEndianStructure):
        _fields_ = [('ticks', ctypes.c_uint64)]

    def __init__(self, sim:'Simulation', pid:int):
        self.sim = sim
        self.pid = pid
        self.epoch = asyncio.get_running_loop().time()
        self.reg = Timer.TimerRegister()
        self.sim.map_peripheral(self.pid, self.reg)
        self.sim.prerunhooks.append(self.update)
        self.th:Optional[asyncio.TimerHandle] = None
        self.update()

    @staticmethod
    def create(sim:'Simulation', pid: int) -> Peripheral:
        return Timer(sim, pid)

    def update(self) -> None:
        now = asyncio.get_running_loop().time() - self.epoch
        self.reg.ticks = int(now * Timer.TICKS_PER_SEC)

    def cancel(self) -> None:
        if self.th is not None:
            self.th.cancel()
            self.th = None

    def alarm(self) -> None:
        self.sim.running.set()

    def svc(self, fid:int, p1:int, p2:int, p3:int) -> None:
        assert(fid == 0)
        target = p2 | (p3 << 8)
        self.cancel()
        self.th = asyncio.get_running_loop().call_at(
                self.epoch + (target / Timer.TICKS_PER_SEC), self.alarm)


class Simulation:
    RAM_BASE    = 0x10000000
    FLASH_BASE  = 0x20000000
    EE_BASE     = 0x30000000
    PERIPH_BASE = 0x40000000

    SVC_PANIC       = 0
    SVC_PERIPH_REG  = 1
    SVC_WFI         = 2
    SVC_PERIPH_BASE = 0x01000000

    class ResetException(BaseException):
        pass

    def __init__(self, ramsz:int=16*1024, flashsz:int=128*1024, eesz:int=8*1024,
            evhub:Optional[EventHub]=None) -> None:
        self.emu = uc.Uc(uc.UC_ARCH_ARM, uc.UC_MODE_THUMB)

        #self.emu.hook_add(uc.UC_HOOK_CODE,
        #        lambda uc, addr, size, sim: sim.trace(addr), self)
        self.emu.hook_add(uc.UC_HOOK_INTR,
                lambda uc, intno, sim: sim.intr(intno), self)
        #self.emu.hook_add(uc.UC_HOOK_BLOCK,
        #        lambda uc, address, size, sim:
        #        sim.block_special(address), self,
        #        begin=0xfffff000, end=0xffffffff)

        self.emu.mem_map(Simulation.RAM_BASE, ramsz)
        self.emu.mem_map(Simulation.FLASH_BASE, flashsz)
        if eesz:
            self.emu.mem_map(Simulation.EE_BASE, eesz)
        self.emu.mem_map(0xfffff000, 0x1000) # special (return from interrupt)

        self.evhub = evhub
        self.peripherals:Dict[int,Peripheral] = { }
        self.prerunhooks:List[PreRunHook] = []

        self.running = asyncio.Event()
        self.ex:Optional[BaseException] = None

    def map_peripheral(self, pid:int, regs:'ctypes._CData') -> None:
        self.emu.mem_map_ptr(Simulation.PERIPH_BASE + (pid * 0x1000),
                ctypes.sizeof(regs), uc.UC_PROT_ALL, ctypes.byref(regs))

    def log(self, msg:str) -> None:
        if self.evhub:
            self.evhub.log(self, msg)

    def trace(self, addr:int) -> None:
        print('PC=%08x' % addr)

    def load_hexfile(self, hexfile:str) -> None:
        ih = IntelHex()
        ih.loadhex(hexfile)
        for (beg, end) in ih.segments():
            try:
                mem = bytes(ih.gets(beg, end - beg))
                self.emu.mem_write(beg, mem)
            except:
                print('Error loading %s at 0x%08x (%d bytes):' % (hexfile, beg, len(mem)))
                raise

    def get_string(self, addr:int, length:int) -> str:
        return cast(bytes, self.emu.mem_read(addr, length)).decode('utf-8')

    def reset(self) -> None:
        # read SP and entry point address from header in flash
        (sp, ep) = struct.unpack('<II',
                self.emu.mem_read(Simulation.FLASH_BASE, 8))

        self.peripherals.clear()
        self.prerunhooks.clear()

        self.pc = ep
        self.emu.reg_write(uca.UC_ARM_REG_SP, sp)
        self.emu.reg_write(uca.UC_ARM_REG_LR, 0xffffff10)
        self.emu.reg_write(uca.UC_ARM_REG_CPSR, 0x33)

        self.running.set()

    def svc_panic(self, ptype:int, reason:int, addr:int, lr:int) -> None:
        raise RuntimeError(
                f'PANIC: type={ptype} ({ {0: "ex", 1: "bl", 2: "fw"}.get(ptype, "??") })'
                f', reason={reason} (0x{reason:x})'
                f', addr=0x{addr:08x}, lr=0x{lr:08x}')

    def svc_register(self, pid:int, uuid:int, p3:int, lr:int) -> bool:
        self.peripherals[pid] = Peripherals.create(
                UUID(bytes=bytes(self.emu.mem_read(uuid, 16))), self, pid)
        return True

    def svc_wfi(self, p1:int, p2:int, p3:int, lr:int) -> bool:
        self.running.clear()
        return False

    svc_lookup = {
            SVC_PANIC      : svc_panic,
            SVC_PERIPH_REG : svc_register,
            SVC_WFI        : svc_wfi,
            }

    def _intr(self, intno:int) -> None:
        lr = self.emu.reg_read(uca.UC_ARM_REG_LR)
        if intno == 2: # SVC
            svcid = self.emu.reg_read(uca.UC_ARM_REG_R0)
            params = (self.emu.reg_read(uca.UC_ARM_REG_R1),
                    self.emu.reg_read(uca.UC_ARM_REG_R2),
                    self.emu.reg_read(uca.UC_ARM_REG_R3))
            if svcid < Simulation.SVC_PERIPH_BASE:
                handler = Simulation.svc_lookup.get(svcid)
                if handler is None:
                    raise RuntimeError(f'Unknown SVCID {svcid}, lr=0x{lr:08x}')
                if handler(self, *params, lr):
                    self.emu.reg_write(uca.UC_ARM_REG_PC, lr)
                else:
                    self.pc = lr
                    self.emu.emu_stop()
            else:
                pid = (svcid >> 16) & 0xff
                p = self.peripherals.get(pid)
                if p is None:
                    raise RuntimeError(f'Unknown peripheral ID {svcid-Simulation.SVC_PERIPH_BASE}, lr=0x{lr:08x}')
                p.svc(svcid & 0xffff, *params)
                self.emu.reg_write(uca.UC_ARM_REG_PC, lr)
        else:
            raise RuntimeError('Unexpected interrupt {intno}, lr=0x{lr:08x}')

    def intr(self, intno:int) -> None:
        try:
            self._intr(intno)
        except BaseException as ex:
            self.ex = ex
            self.emu.emu_stop()

    async def run(self) -> None:
        while True:
            self.reset()

            while True:
                await self.running.wait()
                for prh in self.prerunhooks:
                    prh()
                self.emu.emu_start(self.pc, 0xffffffff)
                if self.ex is not None:
                    raise self.ex


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
