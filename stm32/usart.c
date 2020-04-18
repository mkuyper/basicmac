// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "peripherals.h"

#ifdef BRD_USART

#if BRD_USART == 1
#define USARTx                  USART1
#define USARTx_enable()         do { RCC->APB2ENR |= RCC_APB2ENR_USART1EN; } while (0)
#define USARTx_disable()        do { RCC->APB2ENR &= ~RCC_APB2ENR_USART1EN; } while (0)
#define USARTx_IRQn             USART1_IRQn
#define DMA_USARTx              DMA_USART1
#elif BRD_USART == BRD_LPUART(1)
#define USARTx                  LPUART1
#define USARTx_enable()         do { RCC->APB1ENR |= RCC_APB1ENR_LPUART1EN; } while (0)
#define USARTx_disable()        do { RCC->APB1ENR &= ~RCC_APB1ENR_LPUART1EN; } while (0)
#define USARTx_IRQn             LPUART1_IRQn
#define DMA_USARTx              DMA_LPUART1
#else
#error "Unsupported USART"
#endif

#define DMA_CHAN_TX             BRD_DMA_CHAN_A(BRD_USART_DMA)
#define DMA_CHAN_RX             BRD_DMA_CHAN_B(BRD_USART_DMA)

enum {
    RX_ON       = (1 << 0),
    TX_ON       = (1 << 1),
};

static struct {
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
} usart;

static void usart_on (unsigned int flag) {
    hal_disableIRQs();
    if (usart.on == 0) {
        // disable sleep (keep clock at full speed during transfer
        hal_setMaxSleep(HAL_SLEEP_S0);
        // enable peripheral clock
        USARTx_enable();
        // set baudrate
        USARTx->BRR = usart.br;
        // usart enable
        USARTx->CR1 = USART_CR1_UE;
        // enable interrupts in NVIC
        NVIC_EnableIRQ(USARTx_IRQn);
    }
    usart.on |= flag;
    hal_enableIRQs();
}

static void usart_off (unsigned int flag) {
    hal_disableIRQs();
    usart.on &= ~flag;
    if (usart.on == 0) {
        // disable USART
        USARTx->CR1 = 0;
        // disable peripheral clock
        USARTx_disable();
        // disable interrupts in NVIC
        NVIC_DisableIRQ(USARTx_IRQn);
        // re-enable sleep
        hal_clearMaxSleep(HAL_SLEEP_S0);
    }
    hal_enableIRQs();
}

static void rx_dma_cb (int status); // fwd decl

static void rx_on (void) {
    // turn on usart
    usart_on(RX_ON);
    // flush data
    USARTx->RQR |= USART_RQR_RXFRQ;
    // configure DMA
    dma_config(DMA_CHAN_RX, DMA_USART1, DMA_CCR_MINC | DMA_CCR_PSIZE_1, DMA_CB_COMPLETE, rx_dma_cb);
    // enable DMA
    USARTx->CR3 |= USART_CR3_DMAR;
    // enable receiver
    USARTx->CR1 |= USART_CR1_RE;
    // setup I/O line
    CFG_PIN_AF(GPIO_USART_RX, GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_NONE);
}

static int rx_off (void) {
    // deconfigure I/O line
    CFG_PIN_DEFAULT(GPIO_USART_RX);
    // disable DMA
    USARTx->CR3 &= ~USART_CR3_DMAR;
    // disable receiver and interrupts
    USARTx->CR1 &= ~USART_CR1_RE;
    // deconfigure DMA
    int n = dma_deconfig(DMA_CHAN_RX);
    // turn off usart
    usart_off(RX_ON);
    // return remaining bytes
    return n;
}

static void tx_on (void) {
    // turn on usart
    usart_on(TX_ON);
    // enable transmitter
    USARTx->CR1 |= USART_CR1_TE;
    // setup I/O line
    CFG_PIN_AF(GPIO_USART_TX, GPIOCFG_OSPEED_40MHz | GPIOCFG_OTYPE_PUPD | GPIOCFG_PUPD_NONE);
    // configure DMA
    dma_config(DMA_CHAN_TX, DMA_USART1, DMA_CCR_MINC | DMA_CCR_DIR | DMA_CCR_PSIZE_1, 0, NULL);
    // enable DMA
    USARTx->CR3 |= USART_CR3_DMAT;
    // clear and enable interrupt
    USARTx->ICR = USART_ICR_TCCF;
    USARTx->CR1 |= USART_CR1_TCIE;
}

static void tx_off (void) {
    // deconfigure I/O line, activate pullup
    CFG_PIN(GPIO_USART_TX, GPIOCFG_MODE_INP | GPIOCFG_OSPEED_400kHz | GPIOCFG_OTYPE_OPEN | GPIOCFG_PUPD_PUP);
    // disable DMA
    USARTx->CR3 &= ~USART_CR3_DMAT;
    // disable receiver and interrupts
    USARTx->CR1 &= ~(USART_CR1_TE | USART_CR1_TCIE);
    // deconfigure DMA
    dma_deconfig(DMA_CHAN_TX);
    // turn off usart
    usart_off(TX_ON);
}

void usart_start (unsigned int br) {
    // activate pullup on tx line
    CFG_PIN(GPIO_USART_TX, GPIOCFG_MODE_INP | GPIOCFG_OSPEED_400kHz | GPIOCFG_OTYPE_OPEN | GPIOCFG_PUPD_PUP);
    usart.br = br;
}

void usart_stop (void) {
    CFG_PIN_DEFAULT(GPIO_USART_TX);
}

void usart_send (void* src, int n, osjob_t* job, osjobcb_t cb) {
    usart.tx.job = job;
    usart.tx.cb = cb;

    tx_on();
    dma_transfer(DMA_CHAN_TX, &USARTx->TDR, src, n);
}

static void rx_done (void) {
    *usart.rx.pn -= rx_off();
    os_setCallback(usart.rx.job, usart.rx.cb);
}

static void rx_dma_cb (int status) {
    rx_done();
}

static void rx_timeout (osjob_t* job) {
    usart_abort_recv();
}

void usart_abort_recv (void) {
    hal_disableIRQs();
    rx_done();
    hal_enableIRQs();
}

void usart_recv (void* dst, int* n, ostime_t timeout, osjob_t* job, osjobcb_t cb) {
    usart.rx.job = job;
    usart.rx.cb = cb;
    usart.rx.pn = n;

    os_setTimedCallback(usart.rx.job, os_getTime() + timeout, rx_timeout);

    rx_on();
    dma_transfer(DMA_CHAN_RX, &USARTx->RDR, dst, *n);
}

void usart_irq (void) {
    unsigned int isr = USARTx->ISR;
    unsigned int cr1 = USARTx->CR1;
    if( (cr1 & USART_CR1_TCIE) && (isr & USART_ISR_TC) ) {
        tx_off();
        os_setCallback(usart.tx.job, usart.tx.cb);
    }
}

#endif
