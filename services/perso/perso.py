# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import Optional, Tuple

import asyncio
import random
import struct

from binascii import crc32
from cobs import cobs

class PTESerialPort:
    def send(self, data:bytes) -> None:
        raise NotImplementedError

    async def recv(self) -> bytes:
        raise NotImplementedError

class PTE:
    def __init__(self, port:PTESerialPort) -> None:
        self.port = port
        self.tag = random.randint(0x0000, 0xffff)

    def pack(self, cmd:int, payload:bytes) -> bytes:
        assert len(payload) <= 236 
        self.tag = (self.tag + 1) & 0xffff
        l = len(payload)
        p = struct.pack('<BHB', cmd, self.tag, l) + payload
        if (pad := len(p) % 4):
            p += b'\xff\xff\xff'[:4-pad]
        p += struct.pack('<I', crc32(p))
        return p

    def unpack(self, frame:bytes) -> Optional[Tuple[int,bytes]]:
        n = len(frame)
        if n < 8:
            return None
        res, tag, l = struct.unpack('<BHB', frame[:4])
        crc, = struct.unpack('<I', frame[-4:])
        if 8 + ((l + 3) & ~3) != n or crc != crc32(frame[:-4]):
            return None
        return res, frame[4:4+l]

    @staticmethod
    def frame(frame:bytes) -> bytes:
        return frame + b'\0'

    @staticmethod
    def unframe(buf:bytes) -> Tuple[bytes,bytes]:
        n = len(buf)
        i = buf.find(b'\0')
        if i < 0:
            return b'', b''
        else:
            return buf[:i], buf[i+1:]

    async def _xchg(self, cmd:int, payload:bytes=b'') -> Tuple[int,bytes]:
        self.port.send(PTE.frame(cobs.encode(self.pack(cmd, payload))))
        while True:
            b = await self.port.recv()
            while b:
                f, b = PTE.unframe(b)
                try:
                    f = cobs.decode(f)
                except cobs.DecodeError:
                    continue
                if (t := self.unpack(f)):
                    return t

    async def xchg(self, cmd:int, payload:bytes=b'', *, timeout:Optional[float]=None) -> Tuple[int,bytes]:
        return await asyncio.wait_for(self._xchg(cmd, payload), timeout=timeout)

    RES_EPARAM = 0x80
    RES_INTERR = 0x81
    RES_WTX    = 0xFE
    RES_NOIMPL = 0xFF

    @staticmethod
    def check_res(res:int, expected:Optional[int]=None) -> None:
        code2desc = {
                PTE.RES_EPARAM: 'invalid parameter',
                PTE.RES_INTERR: 'internal error',
                PTE.RES_NOIMPL: 'not implemented' }
        if res & 0x80:
            raise ValueError(f'Error response code 0x{res:02x} ({code2desc.get(res, "unknown")})')
        if expected is not None and res != expected:
            raise ValueError(f'Unexpected response code 0x{res:02x}')

    NOP = 0x00

    async def nop(self, *, timeout:Optional[float]=None) -> None:
        res, pl = await self.xchg(PTE.NOP, timeout=timeout)
        PTE.check_res(res, expected=0x7F)
        if pl:
            raise ValueError(f'Unexpected response payload {pl.hex()}')

