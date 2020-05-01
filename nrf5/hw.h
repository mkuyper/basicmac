// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _hw_h_
#define _hw_h_

#include "board.h"
#include "oslmic.h"

#include "nrf.h"
#include "nrfx.h"
#include "nrf_sdm.h"



// -----------------------------------------------------------------------------
// GPIO

#define PERIPH_PIO


// -----------------------------------------------------------------------------
// Flash

#ifdef NRF52_SERIES

#define FLASH_BASE      0x00000000
#define FLASH_PAGE_SZ   128
#define FLASH_PAGE_NW   (FLASH_PAGE_SZ >> 2)

#define FLASH_SZ        (NRF_FICR->CODESIZE)
#define FLASH_END       (FLASH_BASE + FLASH_SZ)

#endif

#endif
