// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "lmic.h"
#include "peripherals.h"

#include "bootloader.h"
#include "boottab.h"

#include "hal/nrf_ppi.h"

#include "nrfx_clock.h"
#include "nrfx_gpiote.h"
#include "nrfx_rtc.h"
#include "nrfx_spim.h"
#include "nrfx_timer.h"
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

static inline void nrfx_spim_suspend (nrfx_spim_t const * p_instance) {
    nrf_spim_disable(p_instance->p_reg);
}

static inline void nrfx_spim_resume (nrfx_spim_t const * p_instance) {
    nrf_spim_enable(p_instance->p_reg);
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
    PANIC_LFCLK_NOSTART = 1,
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

__attribute__((always_inline)) static inline uint32_t getpc (void) {
    uint32_t addr;
    __asm__ volatile ("mov %[addr], pc" : [addr]"=r" (addr) : : );
    return addr;
}

// Busy wait on condition with timeout (about 10-20s)
#define SAFE_while(reason, expr) do { \
    volatile uint32_t __timeout = (1 << 27); \
    while( expr ) { \
        if( __timeout-- == 0 ) { \
            panic(reason, getpc()); \
        } \
    } \
} while (0)


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

    nrfx_clock_enable(); // this is necessary to switch clock source
    nrfx_clock_start(NRF_CLOCK_DOMAIN_LFCLK);
    SAFE_while(PANIC_LFCLK_NOSTART, !nrfx_clock_is_running(NRF_CLOCK_DOMAIN_LFCLK, NULL));

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
    return os_getRndU2(); // XXX
}


// -----------------------------------------------------------------------------
// Radio SPI and antenna switching

static const nrfx_spim_t radio_spi = NRFX_SPIM_INSTANCE(0);

static void radio_spi_init (void) {
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

    pio_set(GPIO_SX_NSS, 1);
    // TODO: pull-down on MOSI/SCK?

    nrfx_err_t rv;
    rv = nrfx_spim_init(&radio_spi, &cfg, NULL, NULL);
    ASSERT(rv == NRFX_SUCCESS);

    nrfx_spim_suspend(&radio_spi);
}

void hal_spi_select (int on) {
    if( on ) {
        nrfx_spim_resume(&radio_spi);
        pio_set(GPIO_SX_NSS, 0);
    } else {
        pio_set(GPIO_SX_NSS, 1);
        nrfx_spim_suspend(&radio_spi);
    }
}

void hal_spi_transact (const u1_t* txbuf, u1_t txlen, u1_t* rxbuf, u1_t rxlen) {
    u1_t buf[txlen + rxlen];
    nrfx_spim_xfer_desc_t xfr = {
        .p_tx_buffer = txbuf,
        .tx_length = txlen,
        .p_rx_buffer = buf,
        .rx_length = txlen + rxlen,
    };

    nrfx_err_t rv;
    rv = nrfx_spim_xfer(&radio_spi, &xfr, 0);
    ASSERT(rv == NRFX_SUCCESS);

    if( rxlen ) {
        memcpy(rxbuf, buf + txlen, rxlen);
    }
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
#ifdef GPIO_ANT_TXRX_EN
        pio_set(GPIO_ANT_TXRX_EN, 0);
#endif
    } else {
#ifdef SVC_pwrman
        t1 = now;
        ctype = (val == HAL_ANTSW_RX) ? PWRMAN_C_RX : PWRMAN_C_TX;
        radio_ua = LMIC.radioPwr_ua;
#endif
#ifdef GPIO_ANT_TXRX_EN
        pio_set(GPIO_ANT_TXRX_EN, 1);
#endif
    }
#ifdef GPIO_ANT_RX
    pio_set(GPIO_ANT_RX, (val == HAL_ANTSW_RX));
#endif
#ifdef GPIO_ANT_TX
    pio_set(GPIO_ANT_TX, (val == HAL_ANTSW_TX));
#endif
#ifdef GPIO_ANT_TX2
    pio_set(GPIO_ANT_TX2, (val == HAL_ANTSW_TX2));
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


// -----------------------------------------------------------------------------
// Radio interrupt handling

static const nrfx_timer_t dio_timer = NRFX_TIMER_INSTANCE(1);
static u4_t dio_tick_base;

enum {
    DIO_PPICH_CLOCK = NRF_PPI_CHANNEL0,
    DIO_PPICH_DIO   = NRF_PPI_CHANNEL1,
};
enum {
    DIO_TMRCH_DIO,
};

static void dio_timer_handler (nrf_timer_event_t event_type, void* p_context) {
    debug_printf("dio timer handler\r\n");
}

static void dio_pin_handler (nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action) {
    int mask = 0;
#ifdef GPIO_SX_DIO0
    if( nrf_gpio_pin_read(BRD_GPIO_PIN(GPIO_SX_DIO0)) ) {
        mask |= HAL_IRQMASK_DIO0;
    }
#endif
#ifdef GPIO_SX_DIO1
    if( nrf_gpio_pin_read(BRD_GPIO_PIN(GPIO_SX_DIO1)) ) {
        mask |= HAL_IRQMASK_DIO1;
    }
#endif
#ifdef GPIO_SX_DIO2
    if( nrf_gpio_pin_read(BRD_GPIO_PIN(GPIO_SX_DIO2)) ) {
        mask |= HAL_IRQMASK_DIO2;
    }
#endif
#ifdef GPIO_SX_DIO3
    if( nrf_gpio_pin_read(BRD_GPIO_PIN(GPIO_SX_DIO3)) ) {
        mask |= HAL_IRQMASK_DIO3;
    }
#endif
    u4_t tstamp = dio_tick_base + nrfx_timer_capture_get(&dio_timer, DIO_TMRCH_DIO);

    hal_disableIRQs();
    radio_irq_handler(mask, (ostime_t) tstamp);
    hal_enableIRQs();
}

static void dio_init (void) {
    static const nrfx_timer_config_t cfg = {
        .mode = NRF_TIMER_MODE_COUNTER,
        .bit_width = NRF_TIMER_BIT_WIDTH_24,
        .interrupt_priority = HAL_IRQ_PRIORITY,
    };

    nrfx_err_t rv;
    rv = nrfx_timer_init(&dio_timer, &cfg, dio_timer_handler);
    ASSERT(rv == NRFX_SUCCESS);

    rv = nrfx_gpiote_init(HAL_IRQ_PRIORITY);
    ASSERT(rv == NRFX_SUCCESS);

    static const nrfx_gpiote_in_config_t pincfg = {
        .sense = NRF_GPIOTE_POLARITY_LOTOHI,
        .hi_accuracy = false, // use sense
        .skip_gpio_setup = false,
    };
#ifdef GPIO_SX_DIO0
    rv = nrfx_gpiote_in_init(BRD_GPIO_PIN(GPIO_SX_DIO0), &pincfg, dio_pin_handler);
    ASSERT(rv == NRFX_SUCCESS);
#endif
#ifdef GPIO_SX_DIO1
    rv = nrfx_gpiote_in_init(BRD_GPIO_PIN(GPIO_SX_DIO1), &pincfg, dio_pin_handler);
    ASSERT(rv == NRFX_SUCCESS);
#endif
#ifdef GPIO_SX_DIO2
    rv = nrfx_gpiote_in_init(BRD_GPIO_PIN(GPIO_SX_DIO2), &pincfg, dio_pin_handler);
    ASSERT(rv == NRFX_SUCCESS);
#endif
#ifdef GPIO_SX_DIO3
    rv = nrfx_gpiote_in_init(BRD_GPIO_PIN(GPIO_SX_DIO3), &pincfg, dio_pin_handler);
    ASSERT(rv == NRFX_SUCCESS);
#endif

    // PPI: RTC tick -> Timer count
    nrf_ppi_channel_endpoint_setup(NRF_PPI, DIO_PPICH_CLOCK,
            nrfx_rtc_event_address_get(&rtc1, NRF_RTC_EVENT_TICK),
            nrfx_timer_task_address_get(&dio_timer, NRF_TIMER_TASK_COUNT));

    // PPI: GPIOTE PORT event -> Timer capture
    nrf_ppi_channel_endpoint_setup(NRF_PPI, DIO_PPICH_DIO,
            nrf_gpiote_event_address_get(NRF_GPIOTE, NRF_GPIOTE_EVENT_PORT),
            nrfx_timer_capture_task_address_get(&dio_timer, DIO_TMRCH_DIO));
}

static void dio_timer_start (void) {
    hal_disableIRQs();
    nrfx_rtc_tick_enable(&rtc1, false);
    nrfx_timer_clear(&dio_timer);
    nrfx_timer_enable(&dio_timer);
    nrf_ppi_channel_enable(NRF_PPI, DIO_PPICH_CLOCK);
    nrf_ppi_channel_enable(NRF_PPI, DIO_PPICH_DIO);
    dio_tick_base = xticks_unsafe();
    hal_enableIRQs();
}

static void dio_timer_stop (void) {
    nrf_ppi_channel_disable(NRF_PPI, DIO_PPICH_CLOCK);
    nrf_ppi_channel_disable(NRF_PPI, DIO_PPICH_DIO);
    nrfx_timer_disable(&dio_timer);
    nrfx_rtc_tick_disable(&rtc1);
}

static inline void dio_config(unsigned int pin, bool on) {
    if( on ) {
        nrfx_gpiote_in_event_enable(BRD_GPIO_PIN(pin), true);
        nrf_gpio_cfg_sense_input(BRD_GPIO_PIN(pin), NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_SENSE_HIGH);
    } else {
        nrfx_gpiote_in_event_disable(BRD_GPIO_PIN(pin));
        nrf_gpio_cfg_default(BRD_GPIO_PIN(pin));
    }
}

void hal_irqmask_set (int mask) {
    static int prevmask;

#ifdef GPIO_SX_DIO0
    dio_config(GPIO_SX_DIO0, (mask & HAL_IRQMASK_DIO0));
#endif
#ifdef GPIO_SX_DIO1
    dio_config(GPIO_SX_DIO1, (mask & HAL_IRQMASK_DIO1));
#endif
#ifdef GPIO_SX_DIO2
    dio_config(GPIO_SX_DIO2, (mask & HAL_IRQMASK_DIO2));
#endif
#ifdef GPIO_SX_DIO3
    dio_config(GPIO_SX_DIO3, (mask & HAL_IRQMASK_DIO3));
#endif

    mask = (mask != 0);
    if( prevmask != mask ) {
        if( mask ) {
            dio_timer_start();
        } else {
            dio_timer_stop();
        }
        prevmask = mask;
    }
}


#ifdef CFG_DEBUG
// -----------------------------------------------------------------------------
// Debug

static const nrfx_uarte_t debug_port = NRFX_UARTE_INSTANCE(0);
static bool debug_active;

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

    pio_set(GPIO_DBG_TX, 1);

    if( nrfx_uarte_init(&debug_port, &cfg, NULL) != NRFX_SUCCESS ) {
        hal_failed();
    }
    nrfx_uarte_suspend(&debug_port);

    debug_active = true;

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
    if( debug_active) {
        int n = strlen(str);
        if( (uintptr_t) str < 0x2000000 || (uintptr_t) str > 0x20010000 ) {
            unsigned char buf[n];
            memcpy(buf, str, n);
            debug_strbuf(buf, n);
        } else {
            debug_strbuf((const unsigned char*) str, n);
        }
    }
}

void hal_debug_led (int val) {
#ifdef GPIO_DBG_LED
    if( val ) {
        pio_activate(GPIO_DBG_LED, true);
    } else {
        pio_default(BRD_GPIO_PIN(GPIO_DBG_LED));
    }
#endif
}

#endif

u4_t hal_unique (void) {
    return NRF_FICR->DEVICEID[0];
}

void sha256 (uint32_t* hash, const uint8_t* msg, uint32_t len) {
    HAL.boottab->sha256(hash, msg, len);
}

void hal_fwinfo (hal_fwi* fwi) {
    fwi->blversion = HAL.boottab->version;

    extern volatile hal_fwhdr fwhdr;
    fwi->version = fwhdr.version;
    fwi->crc = fwhdr.boot.crc;
    fwi->flashsz = FLASH_SZ;
}


// -----------------------------------------------------------------------------
// HAL initialization

void hal_init (void* bootarg) {
    HAL.boottab = bootarg;

#ifdef CFG_DEBUG
    debug_init();
#endif
    hal_pd_init();
    clock_init();
    dio_init();
    radio_spi_init();
}


// -----------------------------------------------------------------------------
// IRQ Handlers

const irqdef HAL_irqdefs[] = {
    { RTC1_IRQn, nrfx_rtc_1_irq_handler },
    { GPIOTE_IRQn, nrfx_gpiote_irq_handler },
    { POWER_CLOCK_IRQn, nrfx_clock_irq_handler },

    { ~0, NULL } // end of list
};
