// Copyright (C) 2020-2021 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _hal_nrf5_h_
#define _hal_nrf5_h_

#include "hw.h"
#include "boottab.h"

// Firmware header -- do not modify (append only)
typedef struct {
    boot_fwhdr boot;

    uint32_t version;
} hal_fwhdr;

// Personalization data
extern uint32_t _bperso[];
#define HAL_PERSODATA_BASE ((uintptr_t) &_bperso)

// Interrupt handlers
typedef struct {
    uint32_t    num;            // NVIC interrupt number
    void*       handler;        // Pointer to handler function
} irqdef;
extern const irqdef HAL_irqdefs[];

#define HAL_IRQ_PRIORITY        5

bool _nrfx_irq_is_pending (uint32_t irq_number);


// PPI assignments
enum {
    HAL_PPICH_CLOCK,            // clock to TIMER1
    HAL_PPICH_DIO,              // DIO to TIMER1
#if BRD_USART_EN(BRD_UARTE0)
    HAL_PPICH_UART,             // UART to TIMER1
#endif
    HAL_PPICH_MAX,
};

// TIMER1 client ID assignments
enum {
    HAL_TMRCID_DIO,             // DIO time stamping
};

// TIMER1 channel assignments
enum {
    HAL_TMRCH_DIO,              // DIO time stamping
};

#endif
