# Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from typing import cast, Any, TextIO

from colorama import Fore, Style, init as colorama_init

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
