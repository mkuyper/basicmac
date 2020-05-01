// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "lmic.h"
#include "peripherals.h"

#include "bootloader.h"
#include "boottab.h"

#include "nrfx_clock.h"
#include "nrfx_rtc.h"
#include "nrfx_spim.h"
#include "nrfx_uarte.h"

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
// Things that should be in nrfx ;-)

static inline bool nrfx_rtc_overflow_pending (nrfx_rtc_t const * p_instance) {
    return nrf_rtc_event_check(p_instance->p_reg, NRF_RTC_EVENT_OVERFLOW);
}

static inline void nrfx_uarte_suspend (nrfx_uarte_t const * p_instance) {
    nrf_uarte_disable(p_instance->p_reg);
}

static inline void nrfx_uarte_resume (nrfx_uarte_t const * p_instance) {
    nrf_uarte_enable(p_instance->p_reg);
}


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
    // TODO - implement?
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
    // busy wait until timestamp is reached
    while( ((s4_t) time - (s4_t) hal_ticks()) > 0 );
}

void hal_setMaxSleep (unsigned int level) {
}

void hal_sleep (u1_t type, u4_t targettime) {
    nrfx_err_t rv;
    rv = nrfx_rtc_cc_set(&rtc1, 0, targettime & 0xffffff, true);
    ASSERT(rv == NRFX_SUCCESS);

    if( ((s4_t) xticks_unsafe()) - ((s4_t) targettime) < 0 ) {
        // TODO - use softdevice if enabled
        asm volatile("wfi");
    }

    nrfx_rtc_cc_disable(&rtc1, 0);
}


// -----------------------------------------------------------------------------
// LoRaWAN glue

u1_t hal_getBattLevel (void) {
    return 0;
}

void hal_setBattLevel (u1_t level) {
}

u4_t hal_dnonce_next (void) {
    return 0;
}


// -----------------------------------------------------------------------------
// Radio SPI and antenna switching

static const nrfx_spim_t radio_spi = NRFX_SPIM_INSTANCE(0);

static void radio_spi_init (void) {
    pio_set(GPIO_SX_NSS, 1);
}

static void spi_on (void) {
    nrfx_spim_config_t cfg = {
        .sck_pin        = BRD_GPIO_PIN(GPIO_SX_SCK),
        .mosi_pin       = BRD_GPIO_PIN(GPIO_SX_MOSI),
        .miso_pin       = BRD_GPIO_PIN(GPIO_SX_MISO),
        .ss_pin         = NRFX_SPIM_PIN_NOT_USED,
        .irq_priority   = HAL_IRQ_PRIORITY,
        .frequency      = NRF_SPIM_FREQ_8M,
        .mode           = NRF_SPIM_MODE_0,
        .bit_order      = NRF_SPIM_BIT_ORDER_MSB_FIRST,
        .miso_pull      = NRF_GPIO_PIN_NOPULL,
        NRFX_SPIM_DEFAULT_EXTENDED_CONFIG
    };

    nrfx_err_t rv;
    rv = nrfx_spim_init(&radio_spi, &cfg, NULL, NULL);
    ASSERT(rv == NRFX_SUCCESS);
}

static void spi_off (void) {
    nrfx_spim_uninit(&radio_spi);
}

void hal_spi_select (int on) {
    if( on ) {
        spi_on();
        pio_set(GPIO_SX_NSS, 0);
    } else {
        pio_set(GPIO_SX_NSS, 1);
        spi_off();
    }
}

u1_t hal_spi (u1_t out) {
    u1_t in;
    nrfx_spim_xfer_desc_t xfr = {
        .p_tx_buffer = &out,
        .tx_length = 1,
        .p_rx_buffer = &in,
        .rx_length = 1
    };

    nrfx_err_t rv;
    rv = nrfx_spim_xfer(&radio_spi, &xfr, 0);
    ASSERT(rv == NRFX_SUCCESS);

    return in;
}

bool hal_pin_tcxo (u1_t val) {
#if defined(GPIO_TCXO_PWR)
    if( val ) {
        pio_set(GPIO_TCXO_PWR, 1);
    } else {
        pio_default(GPIO_TCXO_PWR);
    }
    return true;
#else
    return false;
#endif
}

void hal_ant_switch (u1_t val) {
#ifdef SVC_pwrman
    static ostime_t t1;
    static int ctype;
    static uint32_t radio_ua;
    ostime_t now = hal_ticks();
    if( radio_ua ) {
        pwrman_consume(ctype, now - t1, radio_ua);
        radio_ua = 0;
    }
#endif
    if( val == HAL_ANTSW_OFF ) {
#ifdef GPIO_TXRX_EN
        pio_set(GPIO_TXRX_EN, 0);
#endif
    } else {
#ifdef SVC_pwrman
        t1 = now;
        ctype = (val == HAL_ANTSW_RX) ? PWRMAN_C_RX : PWRMAN_C_TX;
        radio_ua = LMIC.radioPwr_ua;
#endif
#ifdef GPIO_TXRX_EN
        pio_set(GPIO_TXRX_EN, 1);
#endif
    }
#ifdef GPIO_RX
    pio_set(GPIO_RX, (val == HAL_ANTSW_RX));
#endif
#ifdef GPIO_TX
    pio_set(GPIO_TX, (val == HAL_ANTSW_TX));
#endif
#ifdef GPIO_TX2
    pio_set(GPIO_TX2, (val == HAL_ANTSW_TX2));
#endif
}

void hal_pin_rst (u1_t val) {
    if( val == 0 || val == 1 ) { // drive pin
        pio_set(GPIO_SX_RESET, val);
    } else {
        pio_default(GPIO_SX_RESET);
    }
}

void hal_pin_busy_wait (void) {
#ifdef GPIO_BUSY
    pio_set(GPIO_BUSY, PIO_INP_HIZ);
    while( pio_get(GPIO_BUSY) != 0 );
    pio_default(GPIO_BUSY);
#endif
}

void hal_irqmask_set (int mask) {
}


#ifdef CFG_DEBUG
// -----------------------------------------------------------------------------
// Debug

static const nrfx_uarte_t debug_port = NRFX_UARTE_INSTANCE(0);

static void debug_init (void) {
    nrfx_uarte_config_t cfg = {
        .pseltxd            = BRD_GPIO_PIN(GPIO_DBG_TX),
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

    if( nrfx_uarte_init(&debug_port, &cfg, NULL) != NRFX_SUCCESS ) {
        hal_failed();
    }
    nrfx_uarte_suspend(&debug_port);

#if CFG_DEBUG != 0
    debug_str("\r\n============== DEBUG STARTED ==============\r\n");
#endif
}

static void debug_strbuf (const unsigned char* buf, int n) {
    nrfx_uarte_resume(&debug_port);
    nrfx_uarte_tx(&debug_port, buf, n);
    nrfx_uarte_suspend(&debug_port);
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
#ifdef GPIO_DBG_LED
    if( val ) {
        pio_set(BRD_GPIO_PIN(GPIO_DBG_LED), (GPIO_DBG_LED & BRD_GPIO_ACTIVE_LOW) ? 0 : 1);
    } else {
        pio_default(BRD_GPIO_PIN(GPIO_DBG_LED));
    }
#endif
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
void os_getNwkKey (u1_t* buf) {
}
void os_getJoinEui (u1_t* buf){
}

u1_t os_getRegion (void) {
    return 0;
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
    radio_spi_init();
}


// -----------------------------------------------------------------------------
// IRQ Handlers

const irqdef HAL_irqdefs[] = {
    { RTC1_IRQn, nrfx_rtc_1_irq_handler },

    { ~0, NULL } // end of list
};
