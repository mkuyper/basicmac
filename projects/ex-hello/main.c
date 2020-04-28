// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "lmic.h"

static void hello (osjob_t* job) {
    static int cnt = 0;
    debug_printf("Hello World! cnt=%d\r\n", cnt++);
    hal_debug_led(cnt & 1);
    os_setApproxTimedCallback(job, os_getTime() + sec2osticks(1), hello);
}

void app_main (osjob_t* job) {
    os_setCallback(job, hello);
}
