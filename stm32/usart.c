// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "peripherals.h"

#ifdef BRD_USART

typedef struct {
    unsigned int on;
    unsigned int brr;

    struct {
        osjob_t* job;
        osjobcb_t cb;
    } tx;
    struct {
        osjob_t* job;
        osjobcb_t cb;
        int* pn;
        ostime_t dl;            // deadline
        ostime_t it;            // idle timeout
    } rx;
} usart_state;

typedef struct {
    USART_TypeDef* port;        // port
    volatile uint32_t* enr;     // peripheral clock enable register
    uint32_t enb;               // peripheral clock enable bit
    uint32_t irqn;              // IRQ number
    uint32_t (*brr) (uint32_t); // baud rate conversion function
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

#if BRD_USART_EN(BRD_USART1 | BRD_USART2)
uint32_t br2brr (uint32_t br) {
    return 32000000U / br;
}
#endif

#if BRD_USART_EN(BRD_LPUART1)
uint32_t br2brr_lp (uint32_t br) {
    uint32_t brr = ((32000000U << 7) / br) << 1;
    ASSERT(brr >= 0x300);
    return brr;
}
#endif

#if BRD_USART_EN(BRD_USART1)
static usart_state state_u1;
static const usart_port port_u1 = {
    .port    = USART1,
    .enr     = &RCC->APB2ENR,
    .enb     = RCC_APB2ENR_USART1EN,
    .irqn    = USART1_IRQn,
    .brr     = br2brr,
#ifdef BRD_USART2_DMA
    .dma.tx  = BRD_DMA_CHAN_A(BRD_USART1_DMA),
    .dma.rx  = BRD_DMA_CHAN_B(BRD_USART1_DMA),
    .dma.pid = DMA_USART1,
#else
    .dma.pid = DMA_NONE,
#endif
    .gpio.rx = GPIO_USART1_RX,
    .gpio.tx = GPIO_USART1_TX,
    .state   = &state_u1
};
const void* const usart_port_u1 = &port_u1;
void usart1_irq (void) {
    usart_irq(usart_port_u1);
}
#endif

#if BRD_USART_EN(BRD_USART2)
static usart_state state_u2;
static const usart_port port_u2 = {
    .port    = USART2,
    .enr     = &RCC->APB1ENR,
    .enb     = RCC_APB1ENR_USART2EN,
    .irqn    = USART2_IRQn,
    .brr     = br2brr,
#ifdef BRD_USART2_DMA
    .dma.tx  = BRD_DMA_CHAN_A(BRD_USART2_DMA),
    .dma.rx  = BRD_DMA_CHAN_B(BRD_USART2_DMA),
    .dma.pid = DMA_USART2,
#else
    .dma.pid = DMA_NONE,
#endif
    .gpio.rx = GPIO_USART2_RX,
    .gpio.tx = GPIO_USART2_TX,
    .state   = &state_u2
};
const void* const usart_port_u2 = &port_u2;
void usart2_irq (void) {
    usart_irq(usart_port_u2);
}
#endif

#if BRD_USART_EN(BRD_LPUART1)
static usart_state state_lpu1;
static const usart_port port_lpu1 = {
    .port    = LPUART1,
    .enr     = &RCC->APB1ENR,
    .enb     = RCC_APB2ENR_USART1EN,
    .irqn    = LPUART1_IRQn,
    .brr     = br2brr_lp,
#ifdef BRD_LPUART1_DMA
    .dma.tx  = BRD_DMA_CHAN_A(BRD_LPUART1_DMA),
    .dma.rx  = BRD_DMA_CHAN_B(BRD_LPUART1_DMA),
    .dma.pid = DMA_LPUART1,
#else
    .dma.pid = DMA_NONE,
#endif
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
        usart->port->BRR = usart->state->brr;
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

static void rx_on (const usart_port* usart, bool idle) {
    // turn on usart
    usart_on(usart, RX_ON);
    // flush data
    usart->port->RQR |= USART_RQR_RXFRQ;
    // configure DMA
    ASSERT(usart->dma.pid != DMA_NONE);
    dma_config(usart->dma.rx, usart->dma.pid, DMA_CCR_MINC | DMA_CCR_PSIZE_1, DMA_CB_COMPLETE, rx_dma_cb, (void*) usart);
    // enable DMA
    usart->port->CR3 |= USART_CR3_DMAR;
    if( idle ) {
        // enable interrupt
        usart->port->CR1 |= USART_CR1_IDLEIE;
    }
    // setup I/O line
    CFG_PIN_AF(usart->gpio.rx, GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_NONE);
    // enable receiver
    usart->port->CR1 |= USART_CR1_RE;
}

static int rx_off (const usart_port* usart) {
    // deconfigure I/O line
    CFG_PIN_DEFAULT(usart->gpio.rx);
    // disable DMA
    usart->port->CR3 &= ~USART_CR3_DMAR;
    // disable receiver and interrupts
    usart->port->CR1 &= ~(USART_CR1_RE | USART_CR1_IDLEIE);
    // deconfigure DMA
    int n = dma_deconfig(usart->dma.rx);
    // turn off usart
    usart_off(usart, RX_ON);
    // return remaining bytes
    return n;
}

static void tx_on (const usart_port* usart, bool dma) {
    // turn on usart
    usart_on(usart, TX_ON);
    // enable transmitter
    usart->port->CR1 |= USART_CR1_TE;
    // setup I/O line
    CFG_PIN_AF(usart->gpio.tx, GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_NONE);
    if( dma ) {
        // configure DMA
        ASSERT(usart->dma.pid != DMA_NONE);
        dma_config(usart->dma.tx, usart->dma.pid, DMA_CCR_MINC | DMA_CCR_DIR | DMA_CCR_PSIZE_1, 0, NULL, NULL);
        // enable DMA
        usart->port->CR3 |= USART_CR3_DMAT;
        // clear and enable interrupt
        usart->port->ICR = USART_ICR_TCCF;
        usart->port->CR1 |= USART_CR1_TCIE;
    }
}

static void tx_off (const usart_port* usart, bool dma) {
    // deconfigure I/O line, activate pullup
    CFG_PIN(usart->gpio.tx, GPIOCFG_MODE_INP | GPIOCFG_OSPEED_400kHz | GPIOCFG_OTYPE_OPEN | GPIOCFG_PUPD_PUP);
    // disable DMA
    usart->port->CR3 &= ~USART_CR3_DMAT;
    // disable receiver and interrupts
    usart->port->CR1 &= ~(USART_CR1_TE | USART_CR1_TCIE);
    if( dma ) {
        // deconfigure DMA
        dma_deconfig(usart->dma.tx);
    }
    // turn off usart
    usart_off(usart, TX_ON);
}

void usart_start (const void* port, unsigned int br) {
    const usart_port* usart = port;
    // activate pullup on tx line
    CFG_PIN(usart->gpio.tx, GPIOCFG_MODE_INP | GPIOCFG_OSPEED_400kHz | GPIOCFG_OTYPE_OPEN | GPIOCFG_PUPD_PUP);
    usart->state->brr = usart->brr(br);
}

void usart_stop (const void* port) {
    const usart_port* usart = port;
    CFG_PIN_DEFAULT(usart->gpio.tx);
}

void usart_send (const void* port, void* src, int n, osjob_t* job, osjobcb_t cb) {
    const usart_port* usart = port;

    usart->state->tx.job = job;
    usart->state->tx.cb = cb;

    tx_on(usart, true);
    dma_transfer(usart->dma.tx, &usart->port->TDR, src, n);
}

void usart_str (const void* port, const char* str) {
    const usart_port* usart = port;

    tx_on(usart, false);

    char c;
    while( (c = *str++) ) {
        while( (usart->port->ISR & USART_ISR_TXE) == 0 );
        usart->port->TDR = c;
    }
    while( (usart->port->ISR & USART_ISR_TC) == 0 );

    tx_off(usart, false);
}

static void rx_done (const usart_port* usart) {
    *usart->state->rx.pn -= rx_off(usart);
    os_setCallback(usart->state->rx.job, usart->state->rx.cb);
}

static void rx_dma_cb (int status, void* arg) {
    rx_done(arg);
}

void usart_abort_recv (const void* port) {
    hal_disableIRQs();
    rx_done(port);
    hal_enableIRQs();
}

static bool _rx_timeout(osjob_t* job, const usart_port* usart) {
    if( usart->state->rx.job == job ) {
        usart_abort_recv(usart);
        return true;
    } else {
        return false;
    }
}

static void rx_timeout (osjob_t* job) {
#if BRD_USART_EN(BRD_USART1)
    if( _rx_timeout(job, usart_port_u1) ) return;
#endif
#if BRD_USART_EN(BRD_USART2)
    if( _rx_timeout(job, usart_port_u2) ) return;
#endif
#if BRD_USART_EN(BRD_LPUART1)
    if( _rx_timeout(job, usart_port_lpu1) ) return;
#endif
    ASSERT(0);
}

static void rewind_timeout (const usart_port* usart, bool idle) {
    ostime_t it, dl = usart->state->rx.dl;
    if( idle && (dl - (it = (os_getTime() + usart->state->rx.it))) > 0 ) {
        dl = it;
    }
    os_setTimedCallback(usart->state->rx.job, dl, rx_timeout);
}

void usart_recv (const void* port, void* dst, int* n, ostime_t timeout, ostime_t idle_timeout, osjob_t* job, osjobcb_t cb) {
    const usart_port* usart = port;

    usart->state->rx.job = job;
    usart->state->rx.cb = cb;
    usart->state->rx.pn = n;
    usart->state->rx.dl = os_getTime() + timeout;
    usart->state->rx.it = idle_timeout;

    rewind_timeout(usart, false);

    rx_on(usart, (idle_timeout != 0));
    dma_transfer(usart->dma.rx, &usart->port->RDR, dst, *n);
}

static void usart_irq (const usart_port* usart) {
    unsigned int isr = usart->port->ISR;
    unsigned int cr1 = usart->port->CR1;
    if( (cr1 & USART_CR1_TCIE) && (isr & USART_ISR_TC) ) {
        tx_off(usart, true);
        os_setCallback(usart->state->tx.job, usart->state->tx.cb);
    }
    if( (cr1 & USART_CR1_IDLEIE) && (isr & USART_ISR_IDLE) ) {
        // clear IDLE interrupt
        usart->port->ICR = USART_ICR_IDLECF;
        if( dma_remaining(usart->dma.rx) != *usart->state->rx.pn ) {
            rewind_timeout(usart, true);
        }
    }
}

#endif
