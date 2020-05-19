// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "lmic.h"
#include "peripherals.h"

// -----------------------------------------------------------------------------
// 4c25d84a-9913-11ea-8de8-23fb8fc027a4
//
// Debug Unit

void debug_init (void) {
    static const unsigned char uuid[16] = {
        0x4c, 0x25, 0xd8, 0x4a, 0x99, 0x13, 0x11, 0xea, 0x8d, 0xe8, 0x23, 0xfb, 0x8f, 0xc0, 0x27, 0xa4
    };
    hal_periph_register(HAL_PID_DEBUG, uuid);
#if CFG_DEBUG != 0
    debug_str("\r\n============== DEBUG STARTED ==============\r\n");
#endif
}

#ifdef CFG_DEBUG

void hal_debug_str (const char* str) {
    hal_periph_svc(HAL_PID_DEBUG, 0, (uint32_t) str, strlen(str));
}

void hal_debug_led (int val) {
}

#endif


// -----------------------------------------------------------------------------
// 20c98436-994e-11ea-8de8-23fb8fc027a4
//
// Timer

void timer_init (void) {
    static const unsigned char uuid[16] = {
        0x20, 0xc9, 0x84, 0x36, 0x99, 0x4e, 0x11, 0xea, 0x8d, 0xe8, 0x23, 0xfb, 0x8f, 0xc0, 0x27, 0xa4
    };
    hal_periph_register(HAL_PID_TIMER, uuid);
}
