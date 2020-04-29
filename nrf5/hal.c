// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "lmic.h"
#include "peripherals.h"

#include "bootloader.h"
#include "boottab.h"

#include "nrfx_uarte.h"

// Important Note: This HAL is currently written for the nRF52832 with S132,
// and assumptions may be made that do not hold true for other nRF5x MCUs and
// SoftDevices.


// -----------------------------------------------------------------------------
// HAL state

static struct {
    s4_t irqlevel;

    boot_boottab* boottab;
} HAL;


// -----------------------------------------------------------------------------
// Panic

// don't change these values, so we know what they are in the field...
enum {
    PANIC_HAL_FAILED    = 0,
};

__attribute__((noreturn))
static void panic (uint32_t reason, uint32_t addr) {
    // disable interrupts
    __disable_irq();

    // call bootloader's panic function
    HAL.boottab->panic(reason, addr);
    // not reached
}

__attribute__((noreturn, naked))
void hal_failed () {
    // get return address
    uint32_t addr;
    __asm__("mov %[addr], lr" : [addr]"=r" (addr) : : );
    // in thumb mode the linked address is the address of the calling instruction plus 4 bytes
    addr -= 4;

    // call panic function
    panic(PANIC_HAL_FAILED, addr);
    // not reached
}

void hal_watchcount (int cnt) {
    // TODO - implement
}

void hal_disableIRQs (void) {
    __disable_irq();
    HAL.irqlevel++;
}

void hal_enableIRQs (void) {
    if(--HAL.irqlevel == 0) {
        __enable_irq();
    }
}


// -----------------------------------------------------------------------------
// Clock and Time

u1_t hal_sleep (u1_t type, u4_t targettime) {
    return 0;
}
u4_t hal_ticks (void) {
    return 0;
}
u8_t hal_xticks (void) {
    return 0;
}
void hal_waitUntil (u4_t time) {
}


// -----------------------------------------------------------------------------
// Debug

static const nrfx_uarte_t debug_port = NRFX_UARTE_INSTANCE(0); // XXX

static void debug_init (void) {
    nrfx_uarte_config_t cfg = {
        .pseltxd            = GPIO_DBG_TX,
        .pselrxd            = NRF_UARTE_PSEL_DISCONNECTED,
        .pselcts            = NRF_UARTE_PSEL_DISCONNECTED,
        .pselrts            = NRF_UARTE_PSEL_DISCONNECTED,
        .p_context          = NULL,
        .baudrate           = NRF_UARTE_BAUDRATE_115200,
        .interrupt_priority = HAL_IRQ_PRIORITY,
        .hal_cfg            = {
            .hwfc           = NRF_UARTE_HWFC_DISABLED,
            .parity         = NRF_UARTE_PARITY_EXCLUDED,
        }
    };

    nrfx_err_t rv;
    rv = nrfx_uarte_init(&debug_port, &cfg, NULL);
    ASSERT(rv == NRFX_SUCCESS);
}

static void debug_strbuf (const unsigned char* buf, int n) {
    nrfx_uarte_tx(&debug_port, buf, n);
}

void hal_debug_str (const char* str) {
    int n = strlen(str);
    if( (uintptr_t) str < 0x2000000 || (uintptr_t) str > 0x20010000 ) {
        unsigned char buf[n];
        memcpy(buf, str, n);
        debug_strbuf(buf, n);
    } else {
        debug_strbuf((const unsigned char*) str, n);
    }
}

void hal_debug_led (int val) {
}

void hal_fwinfo (hal_fwi* fwi) {
    fwi->blversion = HAL.boottab->version;

    extern volatile hal_fwhdr fwhdr;
    fwi->version = fwhdr.version;
    fwi->crc = fwhdr.boot.crc;
    fwi->flashsz = FLASH_SZ;
}

void os_getDevEui (u1_t* buf) {
    os_wlsbf4(buf, 0xdeafbeef);
    os_wlsbf4(buf+4, 0xdeafbeef);
}

u1_t* hal_serial (void) {
    return (u1_t*) "****************";
}

u4_t  hal_hwid (void) {
    return 0;
}


// -----------------------------------------------------------------------------
// HAL initialization

void hal_init (void* bootarg) {
    HAL.boottab = bootarg;

    debug_init();
}


// -----------------------------------------------------------------------------
// IRQ Handlers

const irqdef HAL_irqdefs[] = {

    { ~0, NULL } // end of list
};
