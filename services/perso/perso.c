// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "lmic.h"
#include "peripherals.h"
#include "svcdefs.h" // for type-checking hook functions

// Check prerequisistes and generate nice warnings
#ifndef GPIO_PERSO_DET
#error "Personalization module requires detect I/O line, please define GPIO_PERSO_DET"
#endif
#ifndef BRD_PERSO_UART
#error "Personalization module requires UART, please define BRD_PERSO_UART"
#endif

static int process (unsigned char* buf) { // XXX
    //int len = buf[3];

    buf[0] = 0xff;
    return 0;
}

typedef union {
    unsigned char bytes[256];
    uint32_t words[256];
} commbuf;

static struct {
    commbuf buf;
    int rxn;
} perso;

// Decode COBS in-place
// - *pused will contain number of input bytes consumed
// - returns number of bytes in frame (without trailing 0x00), or -1 if invalid
static int cobs_decode (unsigned char* buf, int len, int* pused) {
    int skip = 0, out = 0, i = 0;
    while ( i < len) {
        unsigned char ch = buf[i++];
        if( ch == 0x00 ) {
            out = skip ? -1 : (out ? out - 1 : 0);
            goto done;
        }
        if( skip == 0 ) {
            skip = ch;
        } else {
            buf[out++] = ch;
        }
        if( --skip == 0 ) {
            buf[out++] = 0x00;
        }
    }
    out = -1;
done:
    *pused = i;
    return out;
}

// Encode COBS in-place
// - output will be 2 bytes longer than input
static void cobs_encode (unsigned char* buf, int len) {
    while( len > 0 ) {
        int l;
        for( l = 0; l < len && buf[l]; l++ );
        len -= (l += 1);
        unsigned char ch1 = l;
        while( l-- > 0 ) {
            unsigned char ch2 = *buf;
            *buf++ = ch1;
            ch1 = ch2;
        }
    }
    *buf = 0x00;
}

static void rx_done (osjob_t* job); // fwd decl
static void tx_done (osjob_t* job); // fwd decl

static void rx_start (osjob_t* job) {
    perso.rxn = sizeof(perso.buf);
    usart_recv(BRD_PERSO_UART, perso.buf.bytes, &perso.rxn, sec2osticks(3600), ms2osticks(100), job, rx_done);
}

static void rx_done (osjob_t* job) {
    int len = perso.rxn;
    debug_printf("rx: (%d) %h\r\n", len, perso.buf.bytes, len);
    while( len > 0) {
        int used;
        int n = cobs_decode(perso.buf.bytes, len, &used);
        debug_printf("decode: %d %h\r\n", n);
        if( n >= 8 && (n & 3) == 0 && 8 + ((perso.buf.bytes[3] + 3) & ~3) == n
                && crc32(perso.buf.words, (n>>2)-1) == perso.buf.words[(n>>2)-1] ) {

            n = process(perso.buf.bytes);

            perso.buf.bytes[3] = n;
            n += 4;
            while( (n & 3) != 0) {
                perso.buf.bytes[n++] = 0xff;
            }
            perso.buf.words[n>>2] = crc32(perso.buf.words, n>>2);
            debug_printf("resp: (%d) %h\r\n", n+4, perso.buf.bytes, n+4);
            cobs_encode(perso.buf.bytes, n+4);
            debug_printf("tx: (%d) %h\r\n", n+4+2, perso.buf.bytes, n+4+2);
            usart_send(BRD_PERSO_UART, perso.buf.bytes, n+4+2, job, tx_done);
            return;
        }
        memmove(perso.buf.bytes, perso.buf.bytes + used, (len -= used));
    }
    rx_start(job);
}

static void tx_done (osjob_t* job) {
    debug_printf("txdone\r\n");
    rx_start(job);
}

bool _perso_main (osjob_t* job) {
    // TODO - check fuse

    // sample detect line, enter perso mode if externally driven
    pio_set(GPIO_PERSO_DET, PIO_INP_PAU);
    hal_waitUntil(os_getTime() + us2osticks(100));
    bool enter_perso = pio_active(GPIO_PERSO_DET);
    pio_default(GPIO_PERSO_DET);

    if( enter_perso ) {
        debug_printf("perso: Entering perso...\r\n");
        // XXX -- setup UART, disable debug uart if same
        usart_start(BRD_PERSO_UART, 115200);

        rx_start(job);
    }

    return enter_perso;
}
