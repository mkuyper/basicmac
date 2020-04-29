// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "lmic.h"
#include "hw.h"

#if defined(NRF52832_XXAA) || defined(NRF52832_XXAB)
#define MAX_IRQn                39      /* 0-38, nRF52832 PS, pg. 24-25 */
#else
#error "Unsupported MCU"
#endif

static uint32_t irqvector[16 + MAX_IRQn];

void _start (boot_boottab* boottab) {
    // symbols provided by linker script
    extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss;

    // initialize data
    uint32_t* src = &_sidata;
    uint32_t* dst = &_sdata;
    while( dst < &_edata ) {
        *dst++ = *src++;
    }

    // initialize bss
    dst = &_sbss;
    while( dst < &_ebss ) {
        *dst++ = 0;
    }

    // copy current Cortex M IRQ + NVIC vector to RAM
    src = boottab->vector;
    dst = irqvector;
    for( int i = 0; i < (16 + MAX_IRQn); i++ ) {
        *dst++ = *src++;
    }
    // fix-up vector with handlers from firmware
    for( const irqdef* id = HAL_irqdefs; id->handler; id++ ) {
        irqvector[16 + id->num] = (uint32_t) id->handler;
    }
    // set application interrupt vector in SoftDevice
    sd_softdevice_vector_table_base_set((uint32_t) irqvector);

    // call main function
    extern void main (boot_boottab* boottab);
    main(boottab);
}

// Firmware header
__attribute__((section(".fwhdr")))
const volatile hal_fwhdr fwhdr = {
    // CRC and size will be patched by external tool
    .boot.crc           = 0,
    .boot.size          = BOOT_MAGIC_SIZE,
    .boot.entrypoint    = (uint32_t) _start,

    .version            = 0,
};
