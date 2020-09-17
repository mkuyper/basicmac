# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import Any

class EventHub:
    LOG  = 0
    LORA = 1

    def event(self, type:int, **kwargs:Any) -> None:
        raise NotImplementedError

    def log(self, src:Any, msg:str) -> None:
        self.event(EventHub.LOG, src=src, msg=msg)
