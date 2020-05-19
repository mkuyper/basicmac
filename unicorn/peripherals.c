// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "lmic.h"
#include "peripherals.h"

#define SVC_PERIPH_FUNC(pid, func) (SVC_PERIPH_BASE | (pid << 16) | func)

#define PERIPH_BASE     0x40000000
#define PERIPH_REG(pid) ((void*) (PERIPH_BASE | ((pid) << 12)))

static inline void psvc_2 (uint32_t pid, uint32_t func, uint32_t p1, uint32_t p2) {
    ((void (*) (uint32_t, uint32_t, uint32_t)) HAL_svc)(SVC_PERIPH_FUNC(pid, func), p1, p2);
}

static inline void psvc_64 (uint32_t pid, uint32_t func, uint64_t p1) {
    ((void (*) (uint32_t, uint64_t)) HAL_svc)(SVC_PERIPH_FUNC(pid, func), p1);
}

static inline void preg (uint32_t pid, const unsigned char* uuid) {
    ((void (*) (uint32_t, uint32_t, uint32_t)) HAL_svc)(SVC_PERIPH_REG, pid, (uint32_t) uuid);
}


// -----------------------------------------------------------------------------
// 4c25d84a-9913-11ea-8de8-23fb8fc027a4
//
// Debug Unit

void dbg_init (void) {
    static const unsigned char uuid[16] = {
        0x4c, 0x25, 0xd8, 0x4a, 0x99, 0x13, 0x11, 0xea, 0x8d, 0xe8, 0x23, 0xfb, 0x8f, 0xc0, 0x27, 0xa4
    };
    preg(HAL_PID_DEBUG, uuid);
#if CFG_DEBUG != 0
    debug_str("\r\n============== DEBUG STARTED ==============\r\n");
#endif
}

void dbg_str (const char* str, int len) {
    psvc_2(HAL_PID_DEBUG, 0, (uint32_t) str, strlen(str));
}


// -----------------------------------------------------------------------------
// 20c98436-994e-11ea-8de8-23fb8fc027a4
//
// Timer

typedef struct {
    uint64_t ticks;
} timer_reg;


void timer_init (void) {
    static const unsigned char uuid[16] = {
        0x20, 0xc9, 0x84, 0x36, 0x99, 0x4e, 0x11, 0xea, 0x8d, 0xe8, 0x23, 0xfb, 0x8f, 0xc0, 0x27, 0xa4
    };
    preg(HAL_PID_TIMER, uuid);
}

uint64_t timer_ticks (void) {
    timer_reg* reg = PERIPH_REG(HAL_PID_TIMER);
    return reg->ticks;
}

void timer_set (uint64_t target) {
    psvc_64(HAL_PID_TIMER, 0, target);
}


// -----------------------------------------------------------------------------
// ???
//
// Radio

void radio_init (bool calibrate) {}

bool radio_irq_process (ostime_t irqtime, u1_t diomask) { return true; }
void radio_starttx (bool txcontinuous) {}
void radio_startrx (bool rxcontinuous) {}
void radio_sleep (void) {}
void radio_cca (void) {}
void radio_cad (void) {}
void radio_cw (void) {}

