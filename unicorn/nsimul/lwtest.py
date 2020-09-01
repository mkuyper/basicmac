# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
# Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import Any

from devtest import DeviceTest
from lorawan import LoraWanMsg

class LWTest(DeviceTest):
    def req_test(self, uplwm:LoraWanMsg, **kwargs:Any) -> None:
        self.dndf(uplwm, port=224, payload=b'\1\1\1\1', **kwargs)

    # join network (with kwargs), start test mode, return first test upmsg
    async def start_testmode(self, **kwargs:Any) -> LoraWanMsg:
        await self.join(**kwargs)

        m = await self.updf()
        self.req_test(m)

        return await self.updf(expectport=224)
