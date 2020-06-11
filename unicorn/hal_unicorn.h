// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _hal_unicorn_h_
#define _hal_unicorn_h_

#include "hw.h"
#include "boottab.h"

// TODO - move this enum to bootloader (boottab.h)
enum {
    SVC_PANIC       = 0,          // panic
    SVC_PERIPH_REG  = 1,          // register peripheral
    SVC_WFI         = 2,          // sleep / wait for interrupt
    SVC_IRQ         = 3,          // run IRQ handlers (if pending and enabled)

    SVC_PERIPH_BASE = 0x01000000, // base for peripheral functions
};

extern void* HAL_svc;

// peripherals
enum {
    HAL_PID_NVIC,
    HAL_PID_DEBUG,
    HAL_PID_TIMER,
    HAL_PID_RADIO,

    HAL_PID_COUNT
};

void nvic_init (void);

void dbg_init (void);

void timer_init (void);
uint64_t timer_ticks (void);
void timer_set (uint64_t target);

void radio_halinit (void);

uint32_t pio_irq_get (void);
void pio_irq_clear (uint32_t mask);
void pio_irq_enable (unsigned int gpio, bool enable);
void pio_irq_config (unsigned int pin, bool rising, bool falling);

// Personalization data
#define HAL_PERSODATA_BASE PERSODATA_BASE

#if defined(SVC_fuota)
// Glue for FUOTA (fountain code) service

#include "peripherals.h"

#define fuota_flash_pagesz FLASH_PAGE_SZ
#define fuota_flash_bitdefault 0

#define fuota_flash_write(dst,src,nwords,erase) \
    flash_write((uint32_t*) (dst), (uint32_t*) (src), nwords, erase)

#define fuota_flash_read(dst,src,nwords) \
    memcpy(dst, src, (nwords) << 2)

#define fuota_flash_rd_u4(addr) \
    (*((uint32_t*) (addr)))

#define fuota_flash_rd_ptr(addr) \
    (*((void**) (addr)))

#endif

// Firmware header -- do not modify (append only)
typedef struct {
    boot_fwhdr boot;

    uint32_t version;
} hal_fwhdr;


#endif
