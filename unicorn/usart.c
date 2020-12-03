// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "lmic.h"
#include "peripherals.h"


// -----------------------------------------------------------------------------
// Fast UART implementation
// - This UART has an infinite baud rate; transfers happen immediately

static struct {
    struct {
        void* buf;
        int* pn;
        osjob_t* job;
        osjobcb_t cb;
    } rx;
} fuart;

void fuart_rx_cb (unsigned char* buf, int n) {
    if( n > *fuart.rx.pn ) {
        n = *fuart.rx.pn;
    } else {
        *fuart.rx.pn = n;
    }
    memcpy(fuart.rx.buf, buf, n);
    os_setCallback(fuart.rx.job, fuart.rx.cb);
}

static void fuart_send (void* src, int n, osjob_t* job, osjobcb_t cb) {
    fuart_tx(src, n);
    os_setCallback(job, cb);
}

static void fuart_abort_recv (void) {
    fuart_rx_stop();
    *fuart.rx.pn = 0;
    os_setCallback(fuart.rx.job, fuart.rx.cb);
}

static void fuart_timeout (osjob_t* job) {
    fuart_abort_recv();
}

static void fuart_recv (void* dst, int* n, ostime_t timeout, osjob_t* job, osjobcb_t cb) {
    fuart.rx.buf = dst;
    fuart.rx.pn = n;
    fuart.rx.job = job;
    fuart.rx.cb = cb;
    os_setTimedCallback(job, os_getTime() + timeout, fuart_timeout);
    fuart_rx_start();
}


// -----------------------------------------------------------------------------

void usart_start (const void* port, unsigned int br) { }
void usart_stop (const void* port) { }

void usart_send (const void* port, void* src, int n, osjob_t* job, osjobcb_t cb) {
    if( port == USART_FUART1 ) {
        fuart_send(src, n, job, cb);
    } else {
        ASSERT(0);
    }
}
void usart_recv (const void* port, void* dst, int* n, ostime_t timeout, ostime_t idle_timeout, osjob_t* job, osjobcb_t cb) {
    if( port == USART_FUART1 ) {
        fuart_recv(dst, n, timeout, job, cb);
    } else {
        ASSERT(0);
    }
}
void usart_abort_recv (const void* port) {
    if( port == USART_FUART1 ) {
        fuart_abort_recv();
    } else {
        ASSERT(0);
    }
}

void usart_str (const void* port, const char* str) {
    ASSERT(0);
}
