// Copyright (C) 2020-2021 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "flashsimul.h"
#include "ufl.h"

#ifndef __x86_64__
#error "Simulation requires amd64 platform"
#endif


// ------------------------------------------------
// Flash simulation

static union {
    uint8_t  B[FLASH_SZ];
    uint32_t W[FLASH_SZ / 4];
    uint32_t P[FLASH_PAGE_CT][FLASH_PAGE_SZ / 4];
} FLASH;

// Fake flash addresses are non-canonical, i.e. they are not valid
// in amd64 virtual address space. This ensures that we cannot
// accidentally access them directly without causing a fault.
static uint32_t addr2byte (void* ptr) {
    uintptr_t addr = (uintptr_t) ptr;
    assert((addr >> 32) == 0xdeadbeef);
    return (addr & 0xffffffff);
}

static uint32_t addr2word (void* ptr) {
    uint32_t addr = addr2byte(ptr);
    assert((addr & 3) == 0);
    return addr >> 2;
}

static void* word2addr (uint32_t word) {
    assert(word <= FLASH_WORD_CT);
    return (void*) ((0xdeadbeefULL << 32) | (word << 2));
}

static void assert_aligned (void* ptr) {
    assert((((uintptr_t) ptr) & 3) == 0);
}

static void wr_u4 (uint32_t w, uint32_t value) {
    if( (FLASH_BITDEFAULT) ) {
        FLASH.W[w] &= value;
    } else {
        FLASH.W[w] |= value;
    }
    assert(FLASH.W[w] == value);
}

void ufl_write (void* dst, void* src, uint32_t nwords, bool erase) {
    assert_aligned(src);
    uint32_t w = addr2word(dst);
    assert((w + nwords) <= FLASH_WORD_CT);
    for( int i = 0; i < nwords; i++ ) {
        if( (((w + i) << 2) & (FLASH_PAGE_SZ-1)) == 0 && erase ) {
            memset(FLASH.W + w + i, (FLASH_BITDEFAULT) ? 0xff : 0x00, FLASH_PAGE_SZ);
        }
        wr_u4(w + i, ((uint32_t*) src)[i]);
    }
}

uint32_t ufl_rd_u4 (void* addr) {
    uint32_t w = addr2word(addr);
    assert(w < FLASH_WORD_CT);
    return(FLASH.W[w]);
}

void ufl_read (void* dst, void* src, uint32_t nwords) {
    uint32_t w = addr2word(src);
    assert((w + nwords) <= FLASH_WORD_CT);
    assert_aligned(dst);
    memcpy(dst, FLASH.W + w, nwords << 2);
}

void ufl_wr_u4 (void* addr, uint32_t value) {
    uint32_t w = addr2word(addr);
    assert(w < FLASH_WORD_CT);
    wr_u4(w, value);
}

void ufl_erase (void* addr, uint32_t nwords) {
    assert((((uintptr_t) addr) & (FLASH_PAGE_SZ-1)) == 0);
    assert(((nwords << 2) & (FLASH_PAGE_SZ-1)) == 0);
    uint32_t w = addr2word(addr);
    assert((w + nwords) <= FLASH_WORD_CT);
    memset(FLASH.W + w, (FLASH_BITDEFAULT) ? 0xff : 0x00, nwords << 2);
}

void* flashsimul_direct (void* addr) {
    uint32_t w = addr2word(addr);
    assert(w < FLASH_WORD_CT);
    return FLASH.W + w;
}

void* flashsimul_init (void) {
    memset(&FLASH, 0xa5, sizeof(FLASH));
    return word2addr(0);
}
