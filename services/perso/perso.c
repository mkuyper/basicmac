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

#ifndef BRD_PERSO_UART_BAUDRATE
#define BRD_PERSO_UART_BAUDRATE 115200
#endif

enum {
    CMD_NOP      = 0x00,
    CMD_RUN      = 0x01,
    CMD_RESET    = 0x02,

    CMD_EE_READ  = 0x90,
    CMD_EE_WRITE = 0x91,
};

enum {
    RES_OK       = 0x00,
    RES_EPARAM   = 0x80,
    RES_INTERR   = 0x81,
    RES_WTX      = 0xFE,
    RES_NOIMPL   = 0xFF,
};

enum {
    OFF_CMD      = 0,
    OFF_LEN      = 3,
    OFF_PAYLOAD  = 4,
};

typedef union {
    unsigned char bytes[256];
    uint32_t words[256];
} commbuf;

static struct {
    commbuf buf;
    int rxn;
    osjobcb_t cb;
} perso;

// forward declarations
static void rx_start (osjob_t* job);
static void rx_done (osjob_t* job);
static void tx_start (osjob_t* job);
static void tx_done (osjob_t* job);

static void cb_reboot (osjob_t* job) {
    hal_reboot();
}

static void perso_process (osjob_t* job) {
    unsigned char* buf = perso.buf.bytes;
    perso.cb = rx_start; // by default, start receiving next command upon completion

    switch( buf[OFF_CMD] ) {
        case CMD_NOP:
            buf[OFF_CMD] = 0x7F;
            goto nopl;

        case CMD_RESET:
            buf[OFF_CMD] = RES_OK;
            perso.cb = cb_reboot;
            goto nopl;

#if defined(PERIPH_EEPROM) && defined(EEPROM_BASE) && defined(EEPROM_SZ)
        case CMD_EE_READ:
            if( buf[OFF_LEN] == 3 ) {
                int off = os_rlsbf2(buf + OFF_PAYLOAD), len = buf[OFF_PAYLOAD + 2];
                if( len < 128 && off + len <= EEPROM_SZ ) {
                    memcpy(buf + OFF_PAYLOAD, (unsigned char*) EEPROM_BASE + off, len);
                    buf[OFF_CMD] = RES_OK;
                    buf[OFF_LEN] = len;
                    break;
                }
            }
            goto eparam;

        case CMD_EE_WRITE:
            if( buf[OFF_LEN] >= 2 ) {
                int off = os_rlsbf2(buf + OFF_PAYLOAD), len = buf[OFF_LEN] - 4;
                if( len < 128 && (len & 3) == 0 && off + len <= EEPROM_SZ ) {
                    eeprom_copy((unsigned char*) EEPROM_BASE + off, buf + OFF_PAYLOAD + 4, len);
                    buf[OFF_CMD] = RES_OK;
                    buf[OFF_LEN] = 0;
                    break;
                }
            }
            goto eparam;
#endif

        default:
            buf[OFF_CMD] = RES_NOIMPL;
            goto nopl;
eparam:
            buf[OFF_CMD] = RES_EPARAM;
nopl:
            buf[OFF_LEN] = 0;
    }
    tx_start(job);
}

// Decode COBS in-place
// - *pused will contain number of input bytes consumed
// - returns number of bytes in frame (without trailing 0x00), or -1 if invalid
static int cobs_decode (unsigned char* buf, int len, int* pused) {
    int skip = 0, out = 0, i = 0;
    while ( i < len ) {
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

static void rx_start (osjob_t* job) {
    perso.rxn = sizeof(perso.buf);
    usart_recv(BRD_PERSO_UART, perso.buf.bytes, &perso.rxn, sec2osticks(3600), ms2osticks(100), job, rx_done);
}

static void rx_done (osjob_t* job) {
    int len = perso.rxn;
    while( len > 0 ) {
        int used;
        int n = cobs_decode(perso.buf.bytes, len, &used);
        if( n >= 8 && (n & 3) == 0 && 8 + ((perso.buf.bytes[OFF_LEN] + 3) & ~3) == n
                && crc32(perso.buf.words, (n>>2)-1) == perso.buf.words[(n>>2)-1] ) {
            perso_process(job);
            return;
        }
        memmove(perso.buf.bytes, perso.buf.bytes + used, (len -= used));
    }
    rx_start(job);
}

static void tx_start (osjob_t* job) {
    int n = perso.buf.bytes[OFF_LEN] + 4;
    ASSERT(n <= 236);
    while( (n & 3) != 0 ) {
        perso.buf.bytes[n++] = 0xff;
    }
    perso.buf.words[n>>2] = crc32(perso.buf.words, n>>2);
    cobs_encode(perso.buf.bytes, n+4);
    usart_send(BRD_PERSO_UART, perso.buf.bytes, n+4+2, job, tx_done);
}

static void tx_done (osjob_t* job) {
    perso.cb(job);
}

bool _perso_main (osjob_t* job) {
    // TODO - check fuse / security bit (if any -- possibly add soft-fuse to pd area?)

    // sample detect line, enter perso mode if externally driven
    pio_set(GPIO_PERSO_DET, PIO_INP_PAU);
    hal_waitUntil(os_getTime() + us2osticks(100));
    bool enter_perso = pio_active(GPIO_PERSO_DET);
    pio_default(GPIO_PERSO_DET);

    if( enter_perso ) {
        debug_printf("perso: entering personalization/test mode\r\n");
#ifdef BRD_DEBUG_UART
        if( BRD_DEBUG_UART == BRD_PERSO_UART ){
            hal_debug_suspend();
        }
#endif
        usart_start(BRD_PERSO_UART, BRD_PERSO_UART_BAUDRATE);
        rx_start(job);
    }

    return enter_perso;
}
