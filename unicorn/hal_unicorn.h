// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _hal_unicorn_h_
#define _hal_unicorn_h_

#include "hw.h"
#include "boottab.h"

extern void* HAL_svc;

// peripherals
enum {
    HAL_PID_NVIC,
    HAL_PID_DEBUG,
    HAL_PID_TIMER,
    HAL_PID_GPIO,
    HAL_PID_FUART,
    HAL_PID_RADIO,

    HAL_PID_COUNT
};

void nvic_init (void);
void nvic_sethandler (uint32_t pid, void* handler);

void dbg_init (void);

void timer_init (void);
uint64_t timer_ticks (void);
uint64_t timer_extend (uint32_t ticks);
void timer_set (uint64_t target);

void radio_halinit (void);

void gpio_init (void);

void fuart_init (void);
void fuart_tx (unsigned char* buf, int n);
void fuart_rx_start (void);
void fuart_rx_cb (unsigned char* buf, int n);
void fuart_rx_stop (void);


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
