// Copyright (C) 2020-2021 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "lmic.h"
#include "peripherals.h"

#include "bootloader.h"
#include "boottab.h"

#include "hal/nrf_ppi.h"

#include "nrf_nvic.h"

#include "nrfx_gpiote.h"
#include "nrfx_rtc.h"
#include "nrfx_spim.h"
#include "nrfx_timer.h"
#include "nrfx_uarte.h"

#include "nrfx_helpers.h"

#include "svcdefs.h"

// Important Note: This HAL is currently written for the nRF52832 with S132,
// and assumptions may be made that do not hold true for other nRF5x MCUs and
// SoftDevices.


// -----------------------------------------------------------------------------
// HAL state

static struct {
    s4_t irqlevel;
    uint32_t ticks;

#ifdef CFG_DEBUG
    u4_t debug_suspend;
#endif

    boot_boottab* boottab;
    uint8_t nested;
} HAL;


// -----------------------------------------------------------------------------
// Panic

// don't change these values, so we know what they are in the field...
enum {
    PANIC_HAL_FAILED    = NRF_FAULT_ID_APP_RANGE_START + 0,
    PANIC_RNG_TIMEOUT   = NRF_FAULT_ID_APP_RANGE_START + 1,
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
    uint8_t nested;
    if( sd_nvic_critical_region_enter(&nested) != NRF_SUCCESS ) {
        hal_failed();
    }
    if( HAL.irqlevel++ == 0 ) {
        HAL.nested = nested;
    } else {
        if( sd_nvic_critical_region_exit(nested) != NRF_SUCCESS ) {
            hal_failed();
        }
    }
}

void hal_enableIRQs (void) {
    if( --HAL.irqlevel == 0 ) {
        if( sd_nvic_critical_region_exit(HAL.nested) != NRF_SUCCESS ) {
            hal_failed();
        }
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
// Soft Device (SD132)

nrf_nvic_state_t nrf_nvic_state; // required for SD NVIC module

static void sd_fault_handler (uint32_t id, uint32_t pc, uint32_t info) {
    panic(id, pc);
}

static void sd_init (void) {
    nrf_clock_lf_cfg_t cfg = {
        .source = NRF_CLOCK_LF_SRC_XTAL,
        .accuracy = NRF_CLOCK_LF_ACCURACY_20_PPM,
    };
    if( sd_softdevice_enable(&cfg, sd_fault_handler) != NRF_SUCCESS ) {
        hal_failed();
    }
    NRFX_IRQ_ENABLE(SD_EVT_IRQn);
}

static void sd_handler (void) {
#ifdef SVC_softdevice
    SVCHOOK_sd_event();
#endif
}


// -----------------------------------------------------------------------------
// NRFX Glue

bool _nrfx_irq_is_pending (uint32_t irq_number) {
    uint32_t pending;
    if( sd_nvic_GetPendingIRQ(irq_number, &pending) != NRF_SUCCESS ) {
        hal_failed();
    }
    return pending;
}


// -----------------------------------------------------------------------------
// Clock and Time

static const nrfx_rtc_t rtc1 = NRFX_RTC_INSTANCE(1);

static void rtc1_handler (nrfx_rtc_int_type_t int_type) {
    //debug_printf("RTC1 handler (%d)\r\n", (int) int_type); // XXX
    if( int_type == NRFX_RTC_INT_OVERFLOW ) {
        HAL.ticks += 1;
    }
}

static void clock_init (void) {
    nrfx_rtc_config_t cfg = {
        .prescaler          = RTC_FREQ_TO_PRESCALER(32768),
        .interrupt_priority = HAL_IRQ_PRIORITY,
        .tick_latency       = NRFX_RTC_US_TO_TICKS(2000, 32768),
        .reliable           = false,
    };

    nrfx_err_t rv;
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
        SCB->SCR |= SCB_SCR_SEVONPEND_Msk;
        sd_app_evt_wait();
        //asm volatile("wfi");
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
#ifdef GPIO_ANT_EN
        pio_set(GPIO_ANT_EN, 0);
#endif
    } else {
#ifdef SVC_pwrman
        t1 = now;
        ctype = (val == HAL_ANTSW_RX) ? PWRMAN_C_RX : PWRMAN_C_TX;
        radio_ua = LMIC.radioPwr_ua;
#endif
#ifdef GPIO_ANT_EN
        pio_set(GPIO_ANT_EN, 1);
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
#ifdef GPIO_SX_BUSY
    pio_set(GPIO_SX_BUSY, PIO_INP_HIZ);
    while( pio_get(GPIO_SX_BUSY) != 0 );
    pio_default(GPIO_SX_BUSY);
#endif
}


// -----------------------------------------------------------------------------
// Timer

static const nrfx_timer_t timer1 = NRFX_TIMER_INSTANCE(1);
static struct {
    uint32_t mask;
    uint32_t tick_base;
} tmr;

static void tmr_handler (nrf_timer_event_t event_type, void* p_context) {
    debug_printf("timer handler\r\n");
}

static void tmr_init (void) {
    static const nrfx_timer_config_t cfg = {
        .mode = NRF_TIMER_MODE_COUNTER,
        .bit_width = NRF_TIMER_BIT_WIDTH_32,
        .interrupt_priority = HAL_IRQ_PRIORITY,
    };

    nrfx_err_t rv;
    rv = nrfx_timer_init(&timer1, &cfg, tmr_handler);
    ASSERT(rv == NRFX_SUCCESS);

    // PPI: RTC tick -> Timer count
    nrf_ppi_channel_endpoint_setup(NRF_PPI, HAL_PPICH_CLOCK,
            nrfx_rtc_event_address_get(&rtc1, NRF_RTC_EVENT_TICK),
            nrfx_timer_task_address_get(&timer1, NRF_TIMER_TASK_COUNT));
}

void tmr_start (uint32_t cid) {
    hal_disableIRQs();
    if( tmr.mask == 0 ) {
        nrfx_rtc_tick_enable(&rtc1, false);
        nrfx_timer_clear(&timer1);
        nrfx_timer_enable(&timer1);
        nrf_ppi_channel_enable(NRF_PPI, HAL_PPICH_CLOCK);
        nrf_ppi_channel_enable(NRF_PPI, HAL_PPICH_DIO);
        tmr.tick_base = xticks_unsafe();
    }
    tmr.mask |= (1 << cid);
    hal_enableIRQs();
}

void tmr_stop (uint32_t cid) {
    hal_disableIRQs();
    tmr.mask &= ~(1 << cid);
    if( tmr.mask == 0 ) {
        nrf_ppi_channel_disable(NRF_PPI, HAL_PPICH_CLOCK);
        nrf_ppi_channel_disable(NRF_PPI, HAL_PPICH_DIO);
        nrfx_timer_disable(&timer1);
        nrfx_rtc_tick_disable(&rtc1);
    }
    hal_enableIRQs();
}

uint32_t tmr_cc_get (uint32_t ch) {
    return tmr.tick_base + nrfx_timer_capture_get(&timer1, ch);
}


// -----------------------------------------------------------------------------
// Radio interrupt handling

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
    uint32_t tstamp = tmr_cc_get(HAL_TMRCH_DIO);

    hal_disableIRQs();
    radio_irq_handler(mask, (ostime_t) tstamp);
    hal_enableIRQs();
}

static void dio_init (void) {
    nrfx_err_t rv;
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

    // PPI: GPIOTE PORT event -> Timer capture
    nrf_ppi_channel_endpoint_setup(NRF_PPI, HAL_PPICH_DIO,
            nrf_gpiote_event_address_get(NRF_GPIOTE, NRF_GPIOTE_EVENT_PORT),
            nrfx_timer_capture_task_address_get(&timer1, HAL_TMRCH_DIO));
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
            tmr_start(HAL_TMRCID_DIO);
        } else {
            tmr_stop(HAL_TMRCID_DIO);
        }
        prevmask = mask;
    }
}


// -----------------------------------------------------------------------------
// TRNG

void trng_next (uint32_t* dest, int count) {
    ASSERT(count < 64);
    SAFE_while(PANIC_RNG_TIMEOUT, sd_rand_application_vector_get((uint8_t*) dest, count << 2) == NRF_SUCCESS);
}


#ifdef CFG_DEBUG
// -----------------------------------------------------------------------------
// Debug

static void debug_uartconfig (void) {
    // configure USART (115200/8N1)
    usart_start(BRD_DBG_UART, 115200);
}

static void debug_init (void) {
    debug_uartconfig();
#if CFG_DEBUG != 0
    debug_str("\r\n============== DEBUG STARTED ==============\r\n");
#endif
}

void hal_debug_str (const char* str) {
    usart_str(BRD_DBG_UART, str);
}

void hal_debug_suspend (void) {
    if( HAL.debug_suspend == 0 ) {
        usart_stop(BRD_DBG_UART);
    }
    HAL.debug_suspend += 1;
}

void hal_debug_resume (void) {
    ASSERT(HAL.debug_suspend);
    HAL.debug_suspend -= 1;
    if( HAL.debug_suspend == 0 ) {
        debug_uartconfig();
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


// -----------------------------------------------------------------------------
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

    sd_init();

#ifdef CFG_DEBUG
    debug_init();
#endif

    hal_pd_init();
    clock_init();
    tmr_init();
    dio_init();
    radio_spi_init();

}


// -----------------------------------------------------------------------------
// IRQ Handlers

const irqdef HAL_irqdefs[] = {
    { RTC1_IRQn, nrfx_rtc_1_irq_handler },
    { GPIOTE_IRQn, nrfx_gpiote_irq_handler },
    { SD_EVT_IRQn, sd_handler },

#if BRD_USART_EN(BRD_UARTE0)
    { UART0_IRQn, nrfx_uarte_0_irq_handler },
#endif

    { ~0, NULL } // end of list
};
