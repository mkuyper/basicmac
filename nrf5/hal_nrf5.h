// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _hal_stm32_h_
#define _hal_stm32_h_

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

#endif
