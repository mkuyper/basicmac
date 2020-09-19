// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "peripherals.h"

#ifdef BRD_USART

typedef struct {
    unsigned int on;
    unsigned int br;

    struct {
        osjob_t* job;
        osjobcb_t cb;
    } tx;
    struct {
        osjob_t* job;
        osjobcb_t cb;
        int* pn;
    } rx;
} usart_state;

typedef struct {
    USART_TypeDef* port;        // port
    volatile uint32_t* enr;     // peripheral clock enable register
    uint32_t enb;               // peripheral clock enable bit
    uint32_t irqn;              // IRQ number
    struct {
        uint8_t tx, rx;         // DMA channels
        uint8_t pid;            // DMA peripheral ID
    } dma;
    struct {
        uint32_t rx, tx;        // RX and TX lines
    } gpio;
    usart_state* state;         // pointer to state (in RAM)
} usart_port;

static void usart_irq (const usart_port* usart);

#if BRD_USART_EN(BRD_USART1)
static usart_state state_u1;
static const usart_port port_u1 = {
    .port    = USART1,
    .enr     = &RCC->APB2ENR,
    .enb     = RCC_APB2ENR_USART1EN,
    .irqn    = USART1_IRQn,
    .dma.tx  = BRD_DMA_CHAN_A(BRD_USART1_DMA),
    .dma.rx  = BRD_DMA_CHAN_B(BRD_USART1_DMA),
    .dma.pid = DMA_USART1,
    .gpio.rx = GPIO_USART1_RX,
    .gpio.tx = GPIO_USART1_TX,
    .state   = &state_u1
};
const void* const usart_port_u1 = &port_u1;
void usart1_irq (void) {
    usart_irq(usart_port_u1);
}
#endif

#if BRD_USART_EN(BRD_LPUART1)
static usart_state state_lpu1;
static const usart_port port_lpu1 = {
    .port    = LPUART1,
    .enr     = &RCC->APB1ENR,
    .enb     = RCC_APB2ENR_USART1EN,
    .irqn    = LPUART1_IRQn,
    .dma.tx  = BRD_DMA_CHAN_A(BRD_LPUART1_DMA),
    .dma.rx  = BRD_DMA_CHAN_B(BRD_LPUART1_DMA),
    .dma.pid = DMA_LPUART1,
    .gpio.rx = GPIO_LPUART1_RX,
    .gpio.tx = GPIO_LPUART1_TX,
    .state   = &state_lpu1
};
const void* const usart_port_lpu1 = &port_lpu1;
void lpuart1_irq (void) {
    usart_irq(usart_port_lpu1);
}
#endif

enum {
    RX_ON       = (1 << 0),
    TX_ON       = (1 << 1),
};

static void usart_on (const usart_port* usart, unsigned int flag) {
    hal_disableIRQs();
    if( usart->state->on == 0 ) {
        // disable sleep (keep clock at full speed during transfer
        hal_setMaxSleep(HAL_SLEEP_S0);
        // enable peripheral clock
        *usart->enr |= usart->enb;
        // set baudrate
        // TODO
        // usart enable
        usart->port->CR1 = USART_CR1_UE;
        // enable interrupts in NVIC
        NVIC_EnableIRQ(usart->irqn);
    }
    usart->state->on |= flag;
    hal_enableIRQs();
}

static void usart_off (const usart_port* usart, unsigned int flag) {
    hal_disableIRQs();
    usart->state->on &= ~flag;
    if (usart->state->on == 0 ) {
        // disable USART
        usart->port->CR1 = 0;
        // disable peripheral clock
        *usart->enr &= ~usart->enb;
        // disable interrupts in NVIC
        NVIC_DisableIRQ(usart->irqn);
        // re-enable sleep
        hal_clearMaxSleep(HAL_SLEEP_S0);
    }
    hal_enableIRQs();
}

static void rx_dma_cb (int status, void* port); // fwd decl

static void rx_on (const usart_port* usart) {
    // turn on usart
    usart_on(usart, RX_ON);
    // flush data
    usart->port->RQR |= USART_RQR_RXFRQ;
    // configure DMA
    dma_config(usart->dma.rx, usart->dma.pid, DMA_CCR_MINC | DMA_CCR_PSIZE_1, DMA_CB_COMPLETE, rx_dma_cb, (void*) usart);
    // enable DMA
    usart->port->CR3 |= USART_CR3_DMAR;
    // enable receiver
    usart->port->CR1 |= USART_CR1_RE;
    // setup I/O line
    CFG_PIN_AF(usart->gpio.rx, GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_NONE);
}

static int rx_off (const usart_port* usart) {
    // deconfigure I/O line
    CFG_PIN_DEFAULT(usart->gpio.rx);
    // disable DMA
    usart->port->CR3 &= ~USART_CR3_DMAR;
    // disable receiver and interrupts
    usart->port->CR1 &= ~USART_CR1_RE;
    // deconfigure DMA
    int n = dma_deconfig(usart->dma.rx);
    // turn off usart
    usart_off(usart, RX_ON);
    // return remaining bytes
    return n;
}

static void tx_on (const usart_port* usart) {
    // turn on usart
    usart_on(usart, TX_ON);
    // enable transmitter
    usart->port->CR1 |= USART_CR1_TE;
    // setup I/O line
    CFG_PIN_AF(usart->gpio.tx, GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_NONE);
    // configure DMA
    dma_config(usart->dma.tx, usart->dma.pid, DMA_CCR_MINC | DMA_CCR_DIR | DMA_CCR_PSIZE_1, 0, NULL, NULL);
    // enable DMA
    usart->port->CR3 |= USART_CR3_DMAT;
    // clear and enable interrupt
    usart->port->ICR = USART_ICR_TCCF;
    usart->port->CR1 |= USART_CR1_TCIE;
}

static void tx_off (const usart_port* usart) {
    // deconfigure I/O line, activate pullup
    CFG_PIN(usart->gpio.tx, GPIOCFG_MODE_INP | GPIOCFG_OSPEED_400kHz | GPIOCFG_OTYPE_OPEN | GPIOCFG_PUPD_PUP);
    // disable DMA
    usart->port->CR3 &= ~USART_CR3_DMAT;
    // disable receiver and interrupts
    usart->port->CR1 &= ~(USART_CR1_TE | USART_CR1_TCIE);
    // deconfigure DMA
    dma_deconfig(usart->dma.tx);
    // turn off usart
    usart_off(usart, TX_ON);
}

void usart_start (const void* port, unsigned int br) {
    const usart_port* usart = port;
    // activate pullup on tx line
    CFG_PIN(usart->gpio.tx, GPIOCFG_MODE_INP | GPIOCFG_OSPEED_400kHz | GPIOCFG_OTYPE_OPEN | GPIOCFG_PUPD_PUP);
    usart->state->br = br;
}

void usart_stop (const void* port) {
    const usart_port* usart = port;
    CFG_PIN_DEFAULT(usart->gpio.tx);
}

void usart_send (const void* port, void* src, int n, osjob_t* job, osjobcb_t cb) {
    const usart_port* usart = port;

    usart->state->tx.job = job;
    usart->state->tx.cb = cb;

    tx_on(usart);
    dma_transfer(usart->dma.tx, &usart->port->TDR, src, n);
}

static void rx_done (const usart_port* usart) {
    *usart->state->rx.pn -= rx_off(usart);
    os_setCallback(usart->state->rx.job, usart->state->rx.cb);
}

static void rx_dma_cb (int status, void* arg) {
    rx_done(arg);
}

static void rx_timeout (osjob_t* job) {
    // XXX which port?
    //usart_abort_recv();
}

void usart_abort_recv (const void* port) {
    hal_disableIRQs();
    rx_done(port);
    hal_enableIRQs();
}

void usart_recv (const void* port, void* dst, int* n, ostime_t timeout, osjob_t* job, osjobcb_t cb) {
    const usart_port* usart = port;

    usart->state->rx.job = job;
    usart->state->rx.cb = cb;
    usart->state->rx.pn = n;

    os_setTimedCallback(usart->state->rx.job, os_getTime() + timeout, rx_timeout);

    rx_on(usart);
    dma_transfer(usart->dma.rx, &usart->port->RDR, dst, *n);
}

static void usart_irq (const usart_port* usart) {
    unsigned int isr = usart->port->ISR;
    unsigned int cr1 = usart->port->CR1;
    if( (cr1 & USART_CR1_TCIE) && (isr & USART_ISR_TC) ) {
        tx_off(usart);
        os_setCallback(usart->state->tx.job, usart->state->tx.cb);
    }
}

#endif
