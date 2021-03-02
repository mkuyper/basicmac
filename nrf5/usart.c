// Copyright (C) 2020-2021 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "lmic.h"
#include "peripherals.h"

#include "nrfx_uarte.h"
#include "nrfx_helpers.h"

#ifdef BRD_USART

typedef struct {
    unsigned int on;
    struct {
        osjob_t* job;
        osjobcb_t cb;
        volatile bool busy;     // set during blocking tx
    } tx;
} usart_state;

typedef struct {
    nrfx_uarte_t port;          // port
    struct {
        uint32_t rx, tx;        // RX and TX lines
    } gpio;
    usart_state* state;         // pointer to state (in RAM)
} usart_port;

#if BRD_USART_EN(BRD_UARTE0)
static usart_state state_u0;
static const usart_port port_u0 = {
    .port    = NRFX_UARTE_INSTANCE(0),
    .gpio.rx = GPIO_UARTE0_RX,
    .gpio.tx = GPIO_UARTE0_TX,
    .state   = &state_u0
};
const void* const usart_port_u0 = &port_u0;
#endif

#define CASE_BR(x) case x: return NRF_UARTE_BAUDRATE_ ## x 
static nrf_uarte_baudrate_t baudrate (unsigned int br) {
    switch( br ) {
        CASE_BR(1200);
        CASE_BR(2400);
        CASE_BR(4800);
        CASE_BR(9600);
        CASE_BR(14400);
        CASE_BR(19200);
        CASE_BR(28800);
        CASE_BR(31250);
        CASE_BR(38400);
        CASE_BR(56000);
        CASE_BR(57600);
        CASE_BR(76800);
        CASE_BR(115200);
        CASE_BR(230400);
        CASE_BR(250000);
        CASE_BR(460800);
        CASE_BR(921600);
        CASE_BR(1000000);
        default: hal_failed(); __builtin_unreachable();
    }
}
#undef CASE_BR

enum {
    RX_ON       = (1 << 0),
    TX_ON       = (1 << 1),
};

static void usart_on (const usart_port* usart, unsigned int flag) {
    hal_disableIRQs();
    if( usart->state->on == 0 ) {
        nrfx_uarte_resume(&usart->port);
    }
    usart->state->on |= flag;
    hal_enableIRQs();
}

static void usart_off (const usart_port* usart, unsigned int flag) {
    hal_disableIRQs();
    usart->state->on &= ~flag;
    if (usart->state->on == 0 ) {
        nrfx_uarte_suspend(&usart->port);
    }
    hal_enableIRQs();
}

static void uarte_evt (const nrfx_uarte_event_t* p_event, void* p_context) {
    const usart_port* usart = p_context;

    switch( p_event->type ) {
        case NRFX_UARTE_EVT_TX_DONE:
            if( usart->state->tx.job ) {
                os_setCallback(usart->state->tx.job, usart->state->tx.cb);
            }
            usart_off(usart, TX_ON);
            usart->state->tx.busy = false;
            break;

        default:
            hal_failed();
    }
}

void usart_start (const void* port, unsigned int br) {
    const usart_port* usart = port;

    nrfx_uarte_config_t cfg = {
        .pseltxd            = BRD_GPIO_PIN(usart->gpio.tx),
        .pselrxd            = BRD_GPIO_PIN(usart->gpio.rx),
        .pselcts            = NRF_UARTE_PSEL_DISCONNECTED,
        .pselrts            = NRF_UARTE_PSEL_DISCONNECTED,
        .p_context          = (void*) usart,
        .baudrate           = baudrate(br),
        .interrupt_priority = HAL_IRQ_PRIORITY,
        .hal_cfg            = {
            .hwfc           = NRF_UARTE_HWFC_DISABLED,
            .parity         = NRF_UARTE_PARITY_EXCLUDED,
        }
    };

    pio_set(usart->gpio.tx, 1);
    if( nrfx_uarte_init(&usart->port, &cfg, uarte_evt) != NRFX_SUCCESS ) {
        hal_failed();
    }
    nrfx_uarte_suspend(&usart->port);
}

void usart_stop (const void* port) {
    const usart_port* usart = port;
    pio_default(usart->gpio.tx);
}

void usart_send (const void* port, void* src, int n, osjob_t* job, osjobcb_t cb) {
    const usart_port* usart = port;

    usart->state->tx.job = job;
    usart->state->tx.cb = cb;
    usart->state->tx.busy = true;

    usart_on(usart, TX_ON);
    nrfx_uarte_tx(&usart->port, src, n);
}

static void usart_send_blocking (const void* port, void* src, int n) {
    const usart_port* usart = port;
    usart_send(port, src, n, NULL, NULL);
    while( usart->state->tx.busy );
}

void usart_str (const void* port, const char* str) {
    int n = strlen(str);
    // transmit buffer must be in RAM
    if( (uintptr_t) str < 0x2000000 || (uintptr_t) str > 0x20010000 ) {
        unsigned char buf[n];
        memcpy(buf, str, n);
        usart_send_blocking(port, buf, n);
    } else {
        usart_send_blocking(port, (void*) str, n);
    }
}

// TODO: RX

#endif
