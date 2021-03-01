// Copyright (C) 2020-2021 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _flashsimul_h_
#define _flashsimul_h_

#ifndef FLASH_SZ
#define FLASH_SZ         (2 * 1024 * 1024) // 2M
#endif

#ifndef FLASH_PAGE_SZ
#define FLASH_PAGE_SZ    4096
#endif

#ifndef FLASH_BITDEFAULT
#define FLASH_BITDEFAULT 1
#endif

#define FLASH_WORD_CT    (FLASH_SZ >> 2)
#define FLASH_PAGE_CT    (FLASH_SZ / FLASH_PAGE_SZ)

#define ufl_bitdefault   FLASH_BITDEFAULT

void* flashsimul_init (void);

void* flashsimul_direct (void* addr);

#endif
