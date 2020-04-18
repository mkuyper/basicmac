// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "hw.h"

#ifdef HW_DMA

static struct {
    unsigned int active;
    struct {
        void (*callback) (int);
    } chan[7];
} dma;

// NOTE: We use a 0-based index for DMA channels, while the STM32 numbers them
// starting at 1. Thus, ch=0 refers to DMA channel 1, etc.

#define MASK_BIT(ch)    (1 << ((ch)-1))
#define MASK_CH1        MASK_BIT(1)
#define MASK_CH23       (MASK_BIT(2) | MASK_BIT(3))
#define MASK_CH4567     (MASK_BIT(4) | MASK_BIT(5) | MASK_BIT(6) | MASK_BIT(7))

static void ch_mask_irqn (unsigned int ch, unsigned int* mask, int* irqn) {
    if( ch == 0 ) {
        *mask = MASK_CH1;
        *irqn = DMA1_Channel1_IRQn;
    } else if( ch < 3 ) {
        *mask = MASK_CH23;
        *irqn = DMA1_Channel2_3_IRQn;
    } else {
        *mask = MASK_CH4567;
        *irqn = DMA1_Channel4_5_6_7_IRQn;
    }
}

static void irq_on (unsigned int ch) {
    unsigned int mask;
    int irqn;
    ch_mask_irqn(ch, &mask, &irqn);
    // enable IRQ if no channel is active yet in block
    if( (dma.active & mask) == 0 ) {
        NVIC_EnableIRQ(irqn);
    }
}

static void irq_off (unsigned int ch) {
    unsigned int mask;
    int irqn;
    ch_mask_irqn(ch, &mask, &irqn);
    // disable IRQ if no channel is active anymore in block
    if( (dma.active & mask) == 0 ) {
        NVIC_DisableIRQ(irqn);
    }
}

static void dma_on (unsigned int ch) {
    hal_disableIRQs();
    if( dma.active == 0 ) {
        RCC->AHBENR |= RCC_AHBENR_DMA1EN;
    }
    irq_on(ch);
    dma.active |= (1 << ch);
    hal_enableIRQs();
}

static void dma_off (unsigned int ch) {
    hal_disableIRQs();
    dma.active &= ~(1 << ch);
    irq_off(ch);
    if( dma.active == 0 ) {
        RCC->AHBENR &= ~RCC_AHBENR_DMA1EN;
    }
    hal_enableIRQs();
}

#define DMACHAN(ch) ((DMA_Channel_TypeDef*)(DMA1_Channel1_BASE + (ch) * (DMA1_Channel2_BASE-DMA1_Channel1_BASE)))

void dma_config (unsigned int ch, unsigned int peripheral, unsigned int ccr, unsigned int flags, void (*callback) (int)) {
    dma.chan[ch].callback = callback;
    dma_on(ch);
    DMACHAN(ch)->CCR = ccr;
    DMA1_CSELR->CSELR = (DMA1_CSELR->CSELR & ~(0xf << (ch<<2))) | (peripheral << (ch<<2));
    DMA1->IFCR = 0xf << (ch<<2);
    if( (flags & DMA_CB_COMPLETE) ) {
        DMACHAN(ch)->CCR |= DMA_CCR_TCIE;
    }
    if( (flags & DMA_CB_HALF) ) {
        DMACHAN(ch)->CCR |= DMA_CCR_HTIE;
    }
}

int dma_deconfig (unsigned int ch) {
    int n = DMACHAN(ch)->CNDTR;
    DMACHAN(ch)->CCR = 0;
    dma_off(ch);
    // return remaning bytes
    return n;
}

void dma_transfer (unsigned int ch, volatile void* paddr, void* maddr, int n) {
    DMACHAN(ch)->CPAR = (uint32_t) paddr;
    DMACHAN(ch)->CMAR = (uint32_t) maddr;
    DMACHAN(ch)->CNDTR = n;
    DMACHAN(ch)->CCR |= DMA_CCR_EN;
}

void dma_irq (void) {
    unsigned int isr = DMA1->ISR;
    for( int ch = 0; ch < 7; ch++ ) {
        unsigned int ccr = DMACHAN(ch)->CCR;
        if( (ccr & DMA_CCR_TCIE) && (isr & (DMA_ISR_TCIF1 << (ch<<2))) ) {
            DMA1->IFCR = DMA_IFCR_CTCIF1 << (ch<<2);
            dma.chan[ch].callback(DMA_CB_COMPLETE);
        }
        if( (ccr & DMA_CCR_HTIE) && (isr & (DMA_ISR_HTIF1 << (ch<<2))) ) {
            DMA1->IFCR = DMA_IFCR_CHTIF1 << (ch<<2);
            dma.chan[ch].callback(DMA_CB_HALF);
        }
    }
}

#endif
