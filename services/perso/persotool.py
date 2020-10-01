#!/usr/bin/env python3

# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import Optional

import aioserial
import asyncio
import click
import functools

from perso import PTE, PTESerialPort, PersoData, PersoDataV1
from rtlib import Eui

class PhysicalPTESerialPort(PTESerialPort):
    def __init__(self, port:str, baudrate:int) -> None:
        self.serial = aioserial.AioSerial(port=port, baudrate=baudrate)

    def send(self, data:bytes) -> None:
        self.serial.write(data)

    async def recv(self) -> bytes:
        return await self.serial.read_until_async(b'\0')


class BasedIntParamType(click.ParamType):
    name = "integer"

    def convert(self, value, param, ctx):
        if isinstance(value, int):
            return value
        try:
            return int(value, 0)
        except ValueError:
            self.fail(f"{value!r} is not a valid integer", param, ctx)

class EuiParamType(click.ParamType):
    name = "eui"

    def convert(self, value, param, ctx):
        try:
            return Eui(value)
        except ValueError:
            self.fail(f"{value!r} is not a valid EUI", param, ctx)

class AESKeyType(click.ParamType):
    name = "aeskey"

    def convert(self, value, param, ctx):
        try:
            key = bytes.fromhex(value)
        except ValueError:
            self.fail(f"{value!r} is not a valid AES key", param, ctx)
        if len(key) != 16:
            self.fail(f"AES key must have a length of 16 bytes", param, ctx)
        return key

BASED_INT = BasedIntParamType()
EUI = EuiParamType()
AESKEY = AESKeyType()

def coro(f):
    @functools.wraps(f)
    def wrapper(*args, **kwargs):
        return asyncio.run(f(*args, **kwargs))
    return wrapper


@click.group()
@click.option('-p', '--port', default='/dev/ttyACM0',
        help='serial port')
@click.option('-b', '--baud', type=int, default=115200,
        help='baud rate')
@click.pass_context
def cli(ctx:click.Context, port:str, baud:int) -> None:
    ctx.obj['pte'] = PTE(PhysicalPTESerialPort(port, baud))


@cli.command(help='Read personalization data from EEPROM')
@click.option('-o', '--offset', type=BASED_INT, default=0x0060,
        help='Offset of personalization data structure in EEPROM')
@click.option('-k', '--show-keys', is_flag=True,
        help='Show network and application keys')
@click.pass_context
@coro
async def pdread(ctx:click.Context, offset:int, show_keys:bool):
    pte = ctx.obj['pte']
    pd = PersoData.unpack(await pte.ee_read(offset, PersoData.V1_SIZE))
    print(f'Hardware ID: 0x{pd.hwid:08x}')
    print(f'Region ID:   0x{pd.region:08x} ({pd.region})')
    print(f'Serial No:   {pd.serial}')
    print(f'Device EUI:  {pd.deveui}')
    print(f'Join EUI:    {pd.joineui}')
    if show_keys:
        print(f'Network key: {pd.nwkkey.hex()}')
        print(f'App key:     {pd.appkey.hex()}')


@cli.command(help='Clear personalization data in EEPROM')
@click.option('-o', '--offset', type=BASED_INT, default=0x0060,
        help='Offset of personalization data structure in EEPROM')
@click.pass_context
@coro
async def pdclear(ctx:click.Context, offset:int):
    pte = ctx.obj['pte']
    pd = PersoData.unpack(await pte.ee_read(offset, PersoData.V1_SIZE))
    await pte.ee_write(offset, bytes(PersoData.V1_SIZE))


@cli.command(help='Write personalization data to EEPROM')
@click.option('-o', '--offset', type=BASED_INT, default=0x0060,
        help='Offset of personalization data structure in EEPROM')
@click.option('--hwid', type=BASED_INT, default=0,
        help='Hardware ID')
@click.option('--region', type=BASED_INT, default=0,
        help='Region ID')
@click.argument('serialno', type=str)
@click.argument('deveui', type=EUI)
@click.argument('joineui', type=EUI)
@click.argument('nwkkey', type=AESKEY)
@click.argument('appkey', type=AESKEY, required=False)
@click.pass_context
@coro
async def pdwrite(ctx:click.Context, offset:int, hwid:int, region:int, serialno:str, deveui:Eui, joineui:Eui, nwkkey:bytes, appkey:Optional[bytes]):
    pte = ctx.obj['pte']
    if appkey is None:
        appkey = nwkkey
    await pte.ee_write(offset, PersoDataV1(hwid, region, serialno, deveui, joineui, nwkkey, appkey).pack())


if __name__ == '__main__':
    cli(obj={})
