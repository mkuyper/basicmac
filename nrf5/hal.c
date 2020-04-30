// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "lmic.h"
#include "peripherals.h"

#include "bootloader.h"
#include "boottab.h"

#include "nrfx_clock.h"
#include "nrfx_uarte.h"
#include "nrfx_rtc.h"

// Important Note: This HAL is currently written for the nRF52832 with S132,
// and assumptions may be made that do not hold true for other nRF5x MCUs and
// SoftDevices.


// -----------------------------------------------------------------------------
// HAL state

static struct {
    s4_t irqlevel;
    uint32_t ticks;

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

static const nrfx_rtc_t rtc1 = NRFX_RTC_INSTANCE(1);

static void rtc1_handler (nrfx_rtc_int_type_t int_type) {
    debug_printf("RTC1 handler (%d)\r\n", (int) int_type); // XXX
    if( int_type == NRFX_RTC_INT_OVERFLOW ) {
        HAL.ticks += 1;
    }
}

static void clock_handler (nrfx_clock_evt_type_t event) {
    debug_printf("clock handler\r\n");
}

// this should be in nrfx.... ;-)
static inline bool nrfx_rtc_overflow_pending (nrfx_rtc_t const * p_instance) {
    return nrf_rtc_event_check(p_instance->p_reg, NRF_RTC_EVENT_OVERFLOW);
}

static void clock_init (void) {
    nrfx_rtc_config_t cfg = {
        .prescaler          = RTC_FREQ_TO_PRESCALER(32768),
        .interrupt_priority = HAL_IRQ_PRIORITY,
        .tick_latency       = NRFX_RTC_US_TO_TICKS(2000, 32768),
        .reliable           = false,
    };

    nrfx_err_t rv;
    rv = nrfx_clock_init(clock_handler);
    ASSERT(rv == NRFX_SUCCESS);

    nrfx_clock_start(NRF_CLOCK_DOMAIN_LFCLK);

    rv = nrfx_rtc_init(&rtc1, &cfg, rtc1_handler);
    ASSERT(rv == NRFX_SUCCESS);

    nrfx_rtc_overflow_enable(&rtc1, true);
    nrfx_rtc_enable(&rtc1);
}

u1_t hal_sleep (u1_t type, u4_t targettime) {
    if( targettime <= hal_ticks() ) {
        return 0;
    }

    nrfx_err_t rv;
    rv = nrfx_rtc_cc_set(&rtc1, 0, targettime & 0xffffff, true);
    ASSERT(rv == NRFX_SUCCESS);

    asm volatile("wfi");

    rv = nrfx_rtc_cc_disable(&rtc1, 0);

    return 1;
}

static uint64_t xticks_unsafe (void) {
    uint32_t lt = nrfx_rtc_counter_get(&rtc1);
    uint32_t ht = HAL.ticks;
    if( nrfx_rtc_overflow_pending(&rtc1) ) {
        // take pending overflow into consideration
        lt = nrfx_rtc_counter_get(&rtc1);
        ht += 1;
    }
    return ((uint64_t) ht << 24) | lt;
}

u4_t hal_ticks (void) {
    return hal_xticks();
}

u8_t hal_xticks (void) {
    hal_disableIRQs();
    uint64_t xt = xticks_unsafe();
    hal_enableIRQs();
    return xt;
}

void hal_waitUntil (u4_t time) {
}


#ifdef CFG_DEBUG
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

#if CFG_DEBUG != 0
    debug_str("\r\n============== DEBUG STARTED ==============\r\n");
#endif
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

#endif


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

#ifdef CFG_DEBUG
    debug_init();
#endif
    clock_init();
}


// -----------------------------------------------------------------------------
// IRQ Handlers

const irqdef HAL_irqdefs[] = {
    { RTC1_IRQn, nrfx_rtc_1_irq_handler },

    { ~0, NULL } // end of list
};
