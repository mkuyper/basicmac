// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "lmic.h"
#include "peripherals.h"
#include "boottab.h"

#if defined(SVC_eefs)
#include "eefs/eefs.h"
#endif

#if defined(SVC_frag)
#include "fuota/frag.h"
#endif

enum {
    SVC_PERIPH_REG = BOOT_SVC_FWBASE,

    SVC_PERIPH_BASE = 0x1000,
};

static struct {
    boot_boottab* boottab;
    unsigned int irqlevel;
} sim;

static uint64_t svc64 (uint32_t id, uint32_t p1, uint32_t p2, uint32_t p3) {
    return ((uint64_t (*) (uint32_t, uint32_t, uint32_t, uint32_t))
            sim.boottab->svc)(id, p1, p2, p3);
}

static uint32_t svc32 (uint32_t id, uint32_t p1, uint32_t p2, uint32_t p3) {
    return ((uint32_t (*) (uint32_t, uint32_t, uint32_t, uint32_t))
            sim.boottab->svc)(id, p1, p2, p3);
}

static void svc (uint32_t id, uint32_t p1, uint32_t p2, uint32_t p3) {
    ((void (*) (uint32_t, uint32_t, uint32_t, uint32_t))
     sim.boottab->svc)(id, p1, p2, p3);
}

void hal_periph_register (uint32_t id, const unsigned char* uuid) {
    svc(SVC_PERIPH_REG, id, (uint32_t) uuid, 0);
}

void hal_periph_svc (uint32_t id, uint32_t p1, uint32_t p2, uint32_t p3) {
    svc(SVC_PERIPH_BASE + id, p1, p2, p3);
}


void hal_init (void* bootarg) {
    sim.boottab = bootarg;
    ASSERT(sim.boottab->version >= 0x105); // require bootloader v261

    debug_init();
    timer_init();

    debug_printf("time: %d\r\n", (uint32_t) *((uint64_t*) 0x40001000));

    ASSERT(0);

    // TODO: RNG

    hal_pd_init();

#if defined(SVC_frag)
    {
        extern volatile boot_fwhdr fwhdr;
        void* beg[1] = { (void*) (((uintptr_t) &fwhdr + fwhdr.size
                    + (FLASH_PAGE_SZ - 1)) & ~(FLASH_PAGE_SZ - 1)) };
        void* end[1] = { (void*) FLASH_END };
        _frag_init(1, beg, end);
    }
#endif

#if defined(SVC_eefs)
    eefs_init((void*) APPDATA_BASE, APPDATA_SZ);
#endif
}

void hal_watchcount (int cnt) {
}

void hal_disableIRQs (void) {
    if( sim.irqlevel++ == 0 ) {
        asm volatile ("cpsid i" : : : "memory");
    }
}

void hal_enableIRQs (void) {
    ASSERT(sim.irqlevel);
    if( --sim.irqlevel == 0 ) {
        asm volatile ("cpsie i" : : : "memory");
        // TODO - kick simul to get interrupts to run
    }
}

void hal_sleep (u1_t type, u4_t targettime) {
    // TODO - sleep
}

u4_t hal_ticks (void) {
    return hal_xticks();
}

u8_t hal_xticks (void) {
    // TODO - implement
    return 0;
}

void hal_waitUntil (u4_t time) {
    // be very strict about how long we can busy wait
    ASSERT(((s4_t) time - (s4_t) hal_ticks()) < ms2osticks(100));
    while( 1 ) {
        u4_t now = hal_ticks();
        if( ((s4_t) (time - now)) <= 0 ) {
            return;
        }
        // TODO - sleep
    }
}

u1_t hal_getBattLevel (void) {
    return 0;
}

void hal_setBattLevel (u1_t level) {
}

__attribute__((noreturn, naked))
void hal_failed (void) {
    // get return address
    uint32_t addr;
    __asm__("mov %[addr], lr" : [addr]"=r" (addr) : : );
    // in thumb mode the linked address is the address of the calling instruction plus 4 bytes
    addr -= 4;

#ifdef CFG_backtrace
    // log address of assertion
    backtrace_addr(__LINE__, addr);
    // save trace to EEPROM
    backtrace_save();
#endif

    // call panic function
    sim.boottab->panic(0, addr);
    // not reached
}

void radio_init (bool calibrate) {

}

void os_radio (u1_t mode) {
}

#if 0
u1_t radio_rand1 (void) {
    // TODO - implement
    return 0;
}
#endif

#ifdef CFG_powerstats

void hal_stats_get (hal_statistics* stats) {
}
void hal_stats_consume (hal_statistics* stats) {
}

#endif


void hal_fwinfo (hal_fwi* fwi) {
    fwi->blversion = sim.boottab->version;

    extern volatile boot_fwhdr fwhdr;
    fwi->version = 0; // XXX no longer in fwhdr
    fwi->crc = fwhdr.crc;
    fwi->flashsz = 128*1024;
}

u4_t hal_unique (void) {
    return 0xdeadbeef;
}


// ------------------------------------------------
// EEPROM

void eeprom_write (void* dest, unsigned int val) {
    ASSERT(((uintptr_t) dest & 3) == 0
            && (uintptr_t) dest >= EEPROM_BASE
            && (uintptr_t) dest < EEPROM_END);
    *((uint32_t*) dest) = val;
}

void eeprom_copy (void* dest, const void* src, int len) {
    ASSERT(((uintptr_t) src & 3) == 0 && (len & 3) == 0);
    uint32_t* p = dest;
    const uint32_t* s = src;
    len >>= 2;
    while( len-- > 0 ) {
        eeprom_write(p++, *s++);
    }
}


// ------------------------------------------------
// CRC engine (32bit aligned words only)

unsigned int crc32 (void* ptr, int nwords) {
    return sim.boottab->crc32(ptr, nwords);
}


// ------------------------------------------------
// SHA-256 engine

void sha256 (uint32_t* hash, const uint8_t* msg, uint32_t len) {
    sim.boottab->sha256(hash, msg, len);
}

void hal_reboot (void) {
    // TODO - implement
    // not reached
    hal_failed();
}

typedef struct {
    uint32_t    dnonce;      // dev nonce
} pdata;

u4_t hal_dnonce_next (void) {
    pdata* p = (pdata*) STACKDATA_BASE;
    return p->dnonce++;
}

void hal_dnonce_clear (void) {
    pdata* p = (pdata*) STACKDATA_BASE;
    p->dnonce = 0;
}

bool hal_set_update (void* ptr) {
    return sim.boottab->update(ptr, NULL) == BOOT_OK;
}

void flash_write (void* dst, const void* src, unsigned int nwords, bool erase) {
    sim.boottab->wr_flash(dst, src, nwords, erase);
}

void hal_logEv (uint8_t evcat, uint8_t evid, uint32_t evparam) {
    // TODO - implement?
}
