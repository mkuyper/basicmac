// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "lmic.h"
#include "peripherals.h"

#define SVC_PERIPH_FUNC(pid, func) (SVC_PERIPH_BASE | (pid << 16) | func)

#define PERIPH_BASE     0x40000000
#define PERIPH_REG(pid) ((void*) (PERIPH_BASE | ((pid) << 12)))

static inline void psvc (uint32_t pid, uint32_t func) {
    ((void (*) (uint32_t)) HAL_svc)(SVC_PERIPH_FUNC(pid, func));
}

static inline void preg (uint32_t pid, const unsigned char* uuid) {
    ((void (*) (uint32_t, uint32_t, uint32_t)) HAL_svc)(SVC_PERIPH_REG, pid, (uint32_t) uuid);
}


// -----------------------------------------------------------------------------
// 439a2c60-ac1b-11ea-99f0-d1119d1d4e55
//
// Nested Vectored Interrupt Controller

typedef struct {
    uint32_t vtor[128];
    uint8_t prio[128];
} nvic_reg;

void nvic_init (void) {
    static const unsigned char uuid[16] = {
        0x43, 0x9a, 0x2c, 0x60, 0xac, 0x1b, 0x11, 0xea, 0x99, 0xf0, 0xd1, 0x11, 0x9d, 0x1d, 0x4e, 0x55
    };
    preg(HAL_PID_NVIC, uuid);

    nvic_reg* reg = PERIPH_REG(HAL_PID_NVIC);
    memset(reg, 0, sizeof(nvic_reg));
}

void nvic_sethandler (uint32_t pid, void* handler) {
    nvic_reg* reg = PERIPH_REG(HAL_PID_NVIC);
    reg->vtor[pid] = (uint32_t) handler;
}


// -----------------------------------------------------------------------------
// 4c25d84a-9913-11ea-8de8-23fb8fc027a4
//
// Debug Unit

typedef struct {
    uint32_t n;
    char s[1024];
} dbg_reg;

void dbg_init (void) {
    static const unsigned char uuid[16] = {
        0x4c, 0x25, 0xd8, 0x4a, 0x99, 0x13, 0x11, 0xea, 0x8d, 0xe8, 0x23, 0xfb, 0x8f, 0xc0, 0x27, 0xa4
    };
    preg(HAL_PID_DEBUG, uuid);
#if CFG_DEBUG != 0
    debug_str("\r\n============== DEBUG STARTED ==============\r\n");
#endif
}

#ifdef CFG_DEBUG
void hal_debug_led (int val) {
}

void hal_debug_str (const char* str) {
    dbg_reg* reg = PERIPH_REG(HAL_PID_DEBUG);
    int i;
    for( i = 0; i < sizeof(reg->s) && str[i]; i++ ) {
        reg->s[i] = str[i];
    }
    reg->n = i;
    psvc(HAL_PID_DEBUG, 0);
}
#endif


// -----------------------------------------------------------------------------
// 20c98436-994e-11ea-8de8-23fb8fc027a4
//
// Timer

typedef struct {
    uint64_t ticks;
    uint64_t target;
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

uint64_t timer_extend (uint32_t ticks) {
    uint64_t c = timer_ticks();
    return c + (((int32_t) ticks - (int32_t) c));
}

void timer_set (uint64_t target) {
    timer_reg* reg = PERIPH_REG(HAL_PID_TIMER);
    reg->target = target;
    psvc(HAL_PID_TIMER, 0);
}


// -----------------------------------------------------------------------------
// 76d5885a-ff99-11ea-9aa3-cd4b514dc224
//
// GPIO

typedef struct {
    uint32_t value;     // 0=lo 1=hi
    uint32_t outm;      // 0=in 1=out
    uint32_t outv;      // 0=lo 1=hi
    uint32_t pdn;       // 0=no 1=yes
    uint32_t pup;       // 0=no 1=yes
    uint32_t rise;      // rising edge irq
    uint32_t fall;      // falling edge irq
    uint32_t irq;       // pending irq
} gpio_reg;

static void gpio_irq (void) {
    // TODO - implement
}

void gpio_init (void) {
    static const unsigned char uuid[16] = {
        0x76, 0xd5, 0x88, 0x5a, 0xff, 0x99, 0x11, 0xea, 0x9a, 0xa3, 0xcd, 0x4b, 0x51, 0x4d, 0xc2, 0x24
    };
    preg(HAL_PID_GPIO, uuid);
    nvic_sethandler(HAL_PID_GPIO, gpio_irq);
}

void pio_set (unsigned int pin, int value) {
    gpio_reg* reg = PERIPH_REG(HAL_PID_GPIO);
    uint32_t mask = 1 << BRD_PIN(pin);

    if( value < 0 ) { // input
        // auto-pull
        if( value == PIO_INP_PAU ) {
            if( pin & BRD_GPIO_ACTIVE_LOW ) {
                value = PIO_INP_PUP;
            } else {
                value = PIO_INP_PDN;
            }
        }
        if( value == PIO_INP_PUP ) {
            reg->pup |= mask;
            reg->pdn &= ~mask;
        } else if( value == PIO_INP_PDN ) {
            reg->pup &= ~mask;
            reg->pdn |= mask;
        } else {
            reg->pup &= ~mask;
            reg->pdn &= ~mask;
        }
        reg->outm &= ~mask;
    } else { // output
        reg->outm |= mask;
        if( value ) {
            reg->outv |= mask;
        } else {
            reg->outv &= ~mask;
        }
    }
    psvc(HAL_PID_GPIO, 0);
}

void pio_activate (unsigned int pin, bool active) {
    pio_set(pin, (pin & BRD_GPIO_ACTIVE_LOW) ? !active : active);
}

int pio_get (unsigned int pin) {
    gpio_reg* reg = PERIPH_REG(HAL_PID_GPIO);
    uint32_t mask = 1 << BRD_PIN(pin);
    return reg->value & mask;
}

bool pio_active (unsigned int pin) {
    bool v = pio_get(pin);
    if( (pin & BRD_GPIO_ACTIVE_LOW) ) {
        v = !v;
    }
    return v;
}

void pio_default (unsigned int pin) {
    pio_set(pin, PIO_INP_HIZ);
}

void pio_direct_start (unsigned int pin, pio_direct* dpio) {
    dpio->reg = PERIPH_REG(HAL_PID_GPIO);
    dpio->mask = 1 << BRD_PIN(pin);
}

void pio_direct_stop (pio_direct* dpio) {
}

void pio_direct_inp (pio_direct* dpio) {
    gpio_reg* reg = dpio->reg;
    uint32_t mask = dpio->mask;
    reg->outm &= ~mask;
    psvc(HAL_PID_GPIO, 0);
}

void pio_direct_out (pio_direct* dpio) {
    gpio_reg* reg = dpio->reg;
    uint32_t mask = dpio->mask;
    reg->outm |= mask;
    psvc(HAL_PID_GPIO, 0);
}

void pio_direct_set (pio_direct* dpio, int value) {
    if( value ) {
        pio_direct_set1(dpio);
    } else {
        pio_direct_set0(dpio);
    }
}

void pio_direct_set0 (pio_direct* dpio) {
    gpio_reg* reg = dpio->reg;
    uint32_t mask = dpio->mask;
    reg->outv &= ~mask;
    psvc(HAL_PID_GPIO, 0);
}

void pio_direct_set1 (pio_direct* dpio) {
    gpio_reg* reg = dpio->reg;
    uint32_t mask = dpio->mask;
    reg->outv |= mask;
    psvc(HAL_PID_GPIO, 0);
}

unsigned int pio_direct_get (pio_direct* dpio) {
}


// -----------------------------------------------------------------------------
// a806819e-0134-11eb-a845-f739a072dd5c
//
// Fast UART

typedef struct {
    uint8_t txbuf[1024];
    uint8_t rxbuf[1024];
    uint32_t ctrl;
    uint32_t rxlen;
    uint32_t txlen;
} fuart_reg;

enum {
    FUART_PSVC_SEND,
    FUART_PSVC_CLEARIRQ,
};

enum {
    FUART_C_RXEN = (1 << 0),
};

static void fuart_irq (void) {
    fuart_reg* reg = PERIPH_REG(HAL_PID_FUART);
    fuart_rx_cb(reg->rxbuf, reg->rxlen);
    reg->ctrl &= ~FUART_C_RXEN;
    psvc(HAL_PID_FUART, FUART_PSVC_CLEARIRQ);
}

void fuart_init (void) {
    static const unsigned char uuid[16] = {
        0xa8, 0x06, 0x81, 0x9e, 0x01, 0x34, 0x11, 0xeb, 0xa8, 0x45, 0xf7, 0x39, 0xa0, 0x72, 0xdd, 0x5c
    };
    preg(HAL_PID_FUART, uuid);
    nvic_sethandler(HAL_PID_FUART, fuart_irq);
}

void fuart_tx (unsigned char* buf, int n) {
    fuart_reg* reg = PERIPH_REG(HAL_PID_FUART);
    ASSERT(n <= sizeof(reg->txbuf));
    memcpy(reg->txbuf, buf, n);
    reg->txlen = n;
    psvc(HAL_PID_FUART, FUART_PSVC_SEND);
}

void fuart_rx_start (void) {
    fuart_reg* reg = PERIPH_REG(HAL_PID_FUART);
    reg->ctrl |= FUART_C_RXEN;
}

void fuart_rx_stop (void) {
    fuart_reg* reg = PERIPH_REG(HAL_PID_FUART);
    reg->ctrl &= ~FUART_C_RXEN;
}


// -----------------------------------------------------------------------------
// 3888937c-ab4c-11ea-aeed-27009b59e638
//
// Radio

typedef struct {
    uint8_t buf[256];
    uint64_t xtime;
    uint32_t plen;
    uint32_t freq;
    uint32_t rps;
    uint32_t xpow;
    uint32_t rssi;
    uint32_t snr;
    uint32_t npreamble;
    uint32_t status;
} radio_reg;

enum {
    RADIO_PSVC_RESET,
    RADIO_PSVC_TX,
    RADIO_PSVC_RX,
    RADIO_PSVC_CLEARIRQ,
};

enum {
    RADIO_S_IDLE,
    RADIO_S_BUSY,
    RADIO_S_TXDONE,
    RADIO_S_RXDONE,
    RADIO_S_RXTOUT,
};

// RPS extensions
enum {
    RADIO_ERPS_IQINV = (1 << 16),
};

static void radio_irq (void) {
    psvc(HAL_PID_RADIO, RADIO_PSVC_CLEARIRQ);
    radio_reg* reg = PERIPH_REG(HAL_PID_RADIO);
    radio_irq_handler(0, reg->xtime);
}

void radio_halinit (void) {
    static const unsigned char uuid[16] = {
        0x38, 0x88, 0x93, 0x7c, 0xab, 0x4c, 0x11, 0xea, 0xae, 0xed, 0x27, 0x00, 0x9b, 0x59, 0xe6, 0x38
    };
    preg(HAL_PID_RADIO, uuid);
    nvic_sethandler(HAL_PID_RADIO, radio_irq);
}

void radio_init (bool calibrate) {
    debug_printf("radio_init(calibrate=%d)\r\n", calibrate);
}

bool radio_irq_process (ostime_t irqtime, u1_t diomask) {
    radio_reg* reg = PERIPH_REG(HAL_PID_RADIO);
    switch( reg->status ) {
        case RADIO_S_TXDONE:
            LMIC.txend = irqtime;
            break;
        case RADIO_S_RXDONE:
            LMIC.rssi = reg->rssi;
            LMIC.snr = reg->snr;
            LMIC.dataLen = reg->plen;
            LMIC.rxtime = irqtime;
	    LMIC.rxtime0 = LMIC.rxtime - calcAirTime(LMIC.rps, LMIC.dataLen); // beginning of frame timestamp
            memcpy(LMIC.frame, reg->buf, LMIC.dataLen);
#ifdef DEBUG_RX
            // XXX would be nice if this could be shared with other radio drivers... (radio.c)
	    debug_printf("RX[freq=%.1F,sf=%d,bw=%d,rssi=%d,snr=%.2F,len=%d]: %.80h\r\n",
			 LMIC.freq, 6, getSf(LMIC.rps) + 6, 125 << getBw(LMIC.rps),
			 LMIC.rssi - RSSI_OFF, LMIC.snr * 100 / SNR_SCALEUP, 2,
			 LMIC.dataLen, LMIC.frame, LMIC.dataLen);
#endif
            break;
        case RADIO_S_RXTOUT:
            // indicate timeout
            LMIC.dataLen = 0;
#ifdef DEBUG_RX
            // XXX would be nice if this could be shared with other radio drivers... (radio.c)
            debug_printf("RX[freq=%.1F,sf=%d,bw=%d]: TIMEOUT\r\n",
                    LMIC.freq, 6, getSf(LMIC.rps) + 6, 125 << getBw(LMIC.rps));
#endif
            break;
    }
    return true;
}

void radio_starttx (bool txcontinuous) {
    ASSERT(txcontinuous == 0);

    radio_reg* reg = PERIPH_REG(HAL_PID_RADIO);

    ASSERT(LMIC.dataLen <= sizeof(reg->buf));
    memcpy(reg->buf, LMIC.frame, LMIC.dataLen);
    reg->plen = LMIC.dataLen;

    reg->freq = LMIC.freq;
    reg->rps = LMIC.rps;
    reg->xpow = LMIC.txpow + LMIC.brdTxPowOff;
    reg->npreamble = 8;

    psvc(HAL_PID_RADIO, RADIO_PSVC_TX);
}

void radio_startrx (bool rxcontinuous) {
    ASSERT(rxcontinuous == 0);

    radio_reg* reg = PERIPH_REG(HAL_PID_RADIO);

    reg->xtime = timer_extend(LMIC.rxtime);
    reg->freq = LMIC.freq;
    reg->rps = LMIC.rps;
    reg->npreamble = LMIC.rxsyms;

    if( LMIC.noRXIQinversion == 0 ) {
        reg->rps |= RADIO_ERPS_IQINV;
    }

    psvc(HAL_PID_RADIO, RADIO_PSVC_RX);
}

void radio_sleep (void) {}
void radio_cca (void) {}
void radio_cad (void) {}
void radio_cw (void) {}
