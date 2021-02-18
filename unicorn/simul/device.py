# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
# Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import cast, Any, Callable, Dict, List, Optional, Tuple, Type, TypeVar

import asyncio
import ctypes
import struct

import unicorn as uc
import unicorn.arm_const as uca

from intelhex import IntelHex
from uuid import UUID

from eventhub import EventHub
from runtime import Clock, Runtime


# -----------------------------------------------------------------------------
# Peripherals

class Peripheral:
    uuid:Optional[UUID] = None

    def __init__(self, sim:'Simulation', pid:int):
        self.sim = sim
        self.pid = pid
        self.init()

    def init(self) -> None:
        pass

    def svc(self, fid:int) -> None:
        raise NotImplementedError


T = TypeVar('T', bound=Peripheral)

class Peripherals:
    peripherals:Dict[UUID,Type[Peripheral]] = {}

    @staticmethod
    def add(cls:Type[Peripheral]) -> Type[Peripheral]:
        assert cls.uuid is not None
        Peripherals.peripherals[cls.uuid] = cls
        return cls

    @staticmethod
    def register(cls:Type['ctypes._CData']) -> Type['ctypes._CData']:
        t = cast(Type[ctypes.Union], type(cls.__name__ + '_regpage', (ctypes.Union,), {}))
        t._anonymous_ = ('_regs',)
        t._fields_ = [('_regs', cls), ('_page', ctypes.c_uint8 * 0x1000)]
        return t

    @staticmethod
    def create(uuid:UUID, sim:'Simulation', pid:int) -> Peripheral:
        if uuid not in Peripherals.peripherals:
            raise ValueError(f'Unknown peripheral {uuid}')
        return Peripherals.peripherals[uuid](sim, pid)


class IrqHandler:
    def requested(self) -> bool:
        return False

    def handler(self) -> Optional[int]:
        raise NotImplementedError

    def done(self) -> None:
        pass

    def set(self, pid:int) -> Optional[int]:
        raise NotImplementedError

    def clear(self, pid:int) -> Optional[int]:
        raise NotImplementedError


# -----------------------------------------------------------------------------
# Device Simulation

PreRunHook = Callable[[], None]
Context = Dict[str, Any]

class Simulation():
    RAM_BASE    = 0x10000000
    FLASH_BASE  = 0x20000000
    EE_BASE     = 0x30000000
    PERIPH_BASE = 0x40000000

    SVC_PANIC       = 0
    SVC_PERIPH_REG  = 1
    SVC_WFI         = 2
    SVC_IRQ         = 3
    SVC_RESET       = 4
    SVC_PERIPH_BASE = 0x01000000

    dummyirqhandler = IrqHandler()

    class ResetException(BaseException):
        pass

    @staticmethod
    def default_irq_handler() -> int:
        raise NotImplementedError

    def __init__(self, runtime:Runtime, *, context:Context={}) -> None:
        self.emu = uc.Uc(uc.UC_ARCH_ARM, uc.UC_MODE_THUMB)

        self.runtime = runtime
        self.context = context

        #self.emu.hook_add(uc.UC_HOOK_CODE,
        #        lambda uc, addr, size, sim: sim.trace(addr), self)
        self.emu.hook_add(uc.UC_HOOK_INTR,
                lambda uc, intno, sim: sim.intr(intno), self)

        self.emu.mem_map(0xfffff000, 0x1000) # special (return from interrupt)
        self.emu.hook_add(uc.UC_HOOK_BLOCK,
                lambda uc, address, size, sim:
                sim.irq_return(address), self,
                begin=0xfffff000, end=0xffffffff)

        self.emu.mem_map(Simulation.RAM_BASE, context.get('sim.ramsz', 16*1024))
        self.emu.mem_map(Simulation.FLASH_BASE, context.get('sim.flashsz', 128*1024))
        if (eesz := context.get('sim.eesz', 8*1024)):
            self.emu.mem_map(Simulation.EE_BASE, eesz)

        self.evhub:Optional[EventHub] = context.get('evhub')
        self.peripherals:Dict[int,Peripheral] = { }
        self.prerunhooks:List[PreRunHook] = []

        self.running = asyncio.Event()
        self.ex:Optional[BaseException] = None

    def map_peripheral(self, pid:int, regs:'ctypes._CData') -> None:
        self.emu.mem_map_ptr(Simulation.PERIPH_BASE + (pid * 0x1000),
                ctypes.sizeof(regs), uc.UC_PROT_ALL, ctypes.byref(regs))

    def unmap_peripheral(self, pid:int) -> None:
        self.emu.mem_unmap(Simulation.PERIPH_BASE + (pid * 0x1000), 0x1000)

    def get_peripheral(self, ptype:Type[T]) -> T:
        for p in self.peripherals.values():
            if p.uuid == ptype.uuid:
                return cast(T, p)
        raise ValueError(f'Unregistered peripheral {ptype.uuid}')

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

    def get_cpsr(self) -> int:
        return cast(int, self.emu.reg_read(uca.UC_ARM_REG_CPSR))

    def irq_enabled(self) -> bool:
        return (self.get_cpsr() & (1 << 7)) == 0

    def stack_push(self, value:int) -> None:
        sp = self.emu.reg_read(uca.UC_ARM_REG_SP) - 4
        self.emu.mem_write(sp, struct.pack('<I', value))
        self.emu.reg_write(uca.UC_ARM_REG_SP, sp)

    def stack_pop(self) -> int:
        sp = self.emu.reg_read(uca.UC_ARM_REG_SP)
        (value,) = cast(Tuple[int], struct.unpack('<I', self.emu.mem_read(sp, 4)))
        self.emu.reg_write(uca.UC_ARM_REG_SP, sp + 4)
        return value

    def irq_return(self, addr:int) -> None:
        assert addr == 0xfffffff0, f'Unknown special PC: 0x{addr:08x}'
        self.pc = self.stack_pop()
        self.emu.emu_stop()
        self.irqhandler.done()

    def reset(self) -> None:
        # read SP and entry point address from header in flash
        (sp, ep) = struct.unpack('<II',
                self.emu.mem_read(Simulation.FLASH_BASE, 8))

        self.irqhandler = Simulation.dummyirqhandler
        self.runtime.reset()

        for pid in self.peripherals:
            self.unmap_peripheral(pid)
        self.peripherals.clear()
        self.prerunhooks.clear()

        self.pc = ep
        self.emu.reg_write(uca.UC_ARM_REG_SP, sp)
        self.emu.reg_write(uca.UC_ARM_REG_LR, 0xffffff10)
        self.emu.reg_write(uca.UC_ARM_REG_CPSR, 0x33)

        self.running.set()

    SRC_CONTINUE = 0    # continue run loop
    SRC_RETURN   = 1    # return to caller
    SRC_RESET    = 2    # reset simulation

    def svc_panic(self) -> int:
        ptype  = self.emu.reg_read(uca.UC_ARM_REG_R1)
        reason = self.emu.reg_read(uca.UC_ARM_REG_R2)
        addr   = self.emu.reg_read(uca.UC_ARM_REG_R3)
        lr     = self.emu.reg_read(uca.UC_ARM_REG_LR)
        raise RuntimeError(
                f'PANIC: type={ptype} ({ {0: "ex", 1: "bl", 2: "fw"}.get(ptype, "??") })'
                f', reason={reason} (0x{reason:x})'
                f', addr=0x{addr:08x}, lr=0x{lr:08x}')

    def svc_register(self) -> int:
        pid  = self.emu.reg_read(uca.UC_ARM_REG_R1)
        uuid = self.emu.reg_read(uca.UC_ARM_REG_R2)
        self.peripherals[pid] = Peripherals.create(
                UUID(bytes=bytes(self.emu.mem_read(uuid, 16))), self, pid)
        return Simulation.SRC_RETURN

    def svc_wfi(self) -> int:
        if not self.irqhandler.requested():
            self.running.clear()
        return Simulation.SRC_CONTINUE

    def svc_irq(self) -> int:
        return Simulation.SRC_CONTINUE

    def svc_reset(self) -> int:
        return Simulation.SRC_RESET

    svc_lookup = {
            SVC_PANIC      : svc_panic,
            SVC_PERIPH_REG : svc_register,
            SVC_WFI        : svc_wfi,
            SVC_IRQ        : svc_irq,
            SVC_RESET      : svc_reset,
            }

    def _intr(self, intno:int) -> None:
        lr = self.emu.reg_read(uca.UC_ARM_REG_LR)
        if intno == 2: # SVC
            svcid = self.emu.reg_read(uca.UC_ARM_REG_R0)
            if svcid < Simulation.SVC_PERIPH_BASE:
                handler = Simulation.svc_lookup.get(svcid)
                if handler is None:
                    raise RuntimeError(f'Unknown SVCID {svcid}, lr=0x{lr:08x}')
                if (c := handler(self)) == Simulation.SRC_CONTINUE:
                    self.pc = lr
                    self.emu.emu_stop()
                elif c == Simulation.SRC_RETURN:
                    self.emu.reg_write(uca.UC_ARM_REG_PC, lr)
                elif c == Simulation.SRC_RESET:
                    self.reset()
                    self.emu.emu_stop()
                else:
                    raise RuntimeError(f'Invalid svc return code {c}')

            else:
                pid = (svcid >> 16) & 0xff
                p = self.peripherals.get(pid)
                if p is None:
                    raise RuntimeError(f'Unknown peripheral ID {svcid-Simulation.SVC_PERIPH_BASE}, lr=0x{lr:08x}')
                p.svc(svcid & 0xffff)
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
                if self.irqhandler.requested() and (pc := self.irqhandler.handler()) is not None:
                    # push LR to stack
                    self.stack_push(self.emu.reg_read(uca.UC_ARM_REG_LR))
                    # set LR to magic value
                    self.emu.reg_write(uca.UC_ARM_REG_LR, 0xfffffff1)
                else:
                    pc = self.pc
                for prh in self.prerunhooks:
                    prh()
                self.emu.emu_start(pc, 0xffffffff)
                if self.ex is not None:
                    raise self.ex

import peripherals
