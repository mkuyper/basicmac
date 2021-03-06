# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import Optional, Tuple, Union

import asyncio
import hashlib
import random
import struct

from binascii import crc32
from cobs import cobs
from dataclasses import dataclass
from rtlib import Eui

class PTESerialPort:
    def send(self, data:bytes) -> None:
        raise NotImplementedError

    async def recv(self) -> bytes:
        raise NotImplementedError

class PTE:
    def __init__(self, port:PTESerialPort, *, timeout:Optional[float]=5.0) -> None:
        self.port = port
        self.tag = random.randint(0x0000, 0xffff)
        self.sync = False
        self.timeout = timeout

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
        p = PTE.frame(cobs.encode(self.pack(cmd, payload)))
        if not self.sync:
            self.sync = True
            p = b'\x55\0\0\0' + p
        self.port.send(p)
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

    async def xchg(self, cmd:int, payload:bytes=b'') -> Tuple[int,bytes]:
        return await asyncio.wait_for(self._xchg(cmd, payload), timeout=self.timeout)

    CMD_NOP      = 0x00
    CMD_RUN      = 0x01
    CMD_RESET    = 0x02

    CMD_EE_READ  = 0x90
    CMD_EE_WRITE = 0x91

    RES_OK       = 0x00
    RES_EPARAM   = 0x80
    RES_INTERR   = 0x81
    RES_WTX      = 0xFE
    RES_NOIMPL   = 0xFF

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

    async def nop(self) -> None:
        res, pl = await self.xchg(PTE.CMD_NOP)
        PTE.check_res(res, expected=0x7F)
        if pl:
            raise ValueError(f'Unexpected response payload {pl.hex()}')

    async def reset(self) -> None:
        res, pl = await self.xchg(PTE.CMD_RESET)
        PTE.check_res(res, expected=PTE.RES_OK)
        if pl:
            raise ValueError(f'Unexpected response payload {pl.hex()}')

    async def ee_read(self, offset:int, length:int) -> bytes:
        res, pl = await self.xchg(PTE.CMD_EE_READ, struct.pack('<HB', offset, length))
        PTE.check_res(res, expected=PTE.RES_OK)
        if pl is None or len(pl) != length:
            raise ValueError(f'Unexpected response payload length {len(pl) if pl else 0}')
        return pl

    async def ee_write(self, offset:int, data:bytes) -> None:
        res, pl = await self.xchg(PTE.CMD_EE_WRITE, struct.pack('<HH', offset, 0) + data)
        PTE.check_res(res, expected=PTE.RES_OK)

class PersoData:
    V1_MAGIC = 0xb2dc4db2
    V1_FORMAT_NH = '<IIII16s8s8s16s16s'
    V1_FORMAT = V1_FORMAT_NH + '32s'
    V1_SIZE = struct.calcsize(V1_FORMAT)

    @staticmethod
    def unpack(data:bytes) -> Union['PersoDataV1']:
        if len(data) < 4:
            raise ValueError('Invalid data size')
        magic, = struct.unpack('<I', data[:4])

        if magic == PersoData.V1_MAGIC:
            if len(data) != PersoData.V1_SIZE:
                raise ValueError('Invalid data size (expected: {PersoData.V1_SIZE}, received: {len(data)}')
            _, hwid, region, reserved, serial, deveui, joineui, nwkkey, appkey, h = struct.unpack(
                    PersoData.V1_FORMAT, data)
            if h != hashlib.sha256(data[:-32]).digest():
                raise ValueError('Hash validation failed')

            if (idx := serial.find(b'\0')) >= 0:
                serial = serial[:idx]
            return PersoDataV1(hwid, region, serial.decode('ascii'), Eui(deveui), Eui(joineui), nwkkey, appkey)

        raise ValueError(f'Unknown magic: 0x{magic:08x}')


@dataclass
class PersoDataV1:
    hwid:int
    region:int
    serial:str
    deveui:Eui
    joineui:Eui
    nwkkey:bytes
    appkey:bytes

    def pack(self) -> bytes:
        pd = struct.pack(PersoData.V1_FORMAT_NH,
                PersoData.V1_MAGIC,
                self.hwid,
                self.region,
                0, # reserved
                self.serial.encode('ascii'),
                self.deveui.as_bytes(),
                self.joineui.as_bytes(),
                self.nwkkey,
                self.appkey)
        h = hashlib.sha256(pd).digest()
        return pd + h
