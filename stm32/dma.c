// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "hw.h"

static struct {
    unsigned int active;
    struct {
        void (*callback) (int);
    } chan[7];
} dma;

static void dma_on (unsigned int ch) {
    hal_disableIRQs();
    if( dma.active == 0 ) {
        RCC->AHBENR |= RCC_AHBENR_DMA1EN;
    }
    dma.active |= (1 << ch);
    if( (dma.active & 0x01) ) {
	NVIC_EnableIRQ(DMA1_Channel1_IRQn);
    }
    if( (dma.active & (0x02|0x04)) ) {
	NVIC_EnableIRQ(DMA1_Channel2_3_IRQn);
    }
    if( (dma.active & (0x08|0x10|0x20|0x40)) ) {
	NVIC_EnableIRQ(DMA1_Channel4_5_6_7_IRQn);
    }
    hal_enableIRQs();
}

static void dma_off (unsigned int ch) {
    hal_disableIRQs();
    dma.active &= ~(1 << ch);
    if( (dma.active & 0x01) == 0 ) {
	NVIC_DisableIRQ(DMA1_Channel1_IRQn);
    }
    if( (dma.active & (0x02|0x04)) == 0 ) {
	NVIC_DisableIRQ(DMA1_Channel2_3_IRQn);
    }
    if( (dma.active & (0x08|0x10|0x20|0x40)) == 0 ) {
	NVIC_DisableIRQ(DMA1_Channel4_5_6_7_IRQn);
    }
    if( dma.active == 0 ) {
        RCC->AHBENR &= ~RCC_AHBENR_DMA1EN;
    }
    hal_enableIRQs();
}

#define DMACHAN(n) ((DMA_Channel_TypeDef*)(DMA1_Channel1_BASE + (n) * (DMA1_Channel2_BASE-DMA1_Channel1_BASE)))

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
