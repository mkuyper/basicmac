# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from perso import PTESerialPort
from peripherals import FastUART

class FUART_PTESerialPort(PTESerialPort):
    def __init__(self, uart:FastUART) -> None:
        self.uart = uart

    def send(self, data:bytes) -> None:
        self.uart.send(data)

    async def recv(self) -> bytes:
        return await self.uart.recv()
