# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

#from typing import Optional, Set, Tuple

import asyncio

from medium import LoraMsg, LoraMsgProcessor, LoraMsgTransmitter, Medium, Rps
from runtime import Runtime

class UniversalGateway(LoraMsgProcessor):
    def __init__(self, runtime:Runtime, medium:Medium) -> None:
        self.upframes:asyncio.Queue[LoraMsg] = asyncio.Queue()
        self.runtime = runtime
        self.medium = medium
        self.xmtr = LoraMsgTransmitter(runtime, medium)
        medium.add_listener(self)

    def msg_complete(self, msg:LoraMsg) -> None:
        if not Rps.isIqInv(msg.rps):
            self.upframes.put_nowait(msg)

    async def next_up(self) -> LoraMsg:
        return await self.upframes.get()

    def sched_dn(self, msg:LoraMsg) -> None:
        self.xmtr.transmit(msg)
