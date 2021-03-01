// Copyright (C) 2020-2021 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

// Universal Flash API

#ifndef _ufl_h_
#define _ufl_h_

#include <stdbool.h>
#include <stdint.h>

#ifdef UFL_IMPL
#include UFL_IMPL
#endif

#if !defined(ufl_bitdefault) || !(ufl_bitdefault == 0 || ufl_bitdefault == 1)
#error "UFL implementation must define ufl_bitdefault to 0 or 1"
#endif

#ifndef ufl_erase
void ufl_erase (void* addr, uint32_t nwords);
#endif

#ifndef ufl_write
void ufl_write (void* dst, void* src, uint32_t nwords, bool erase);
#endif

#ifndef ufl_wr_u4
void ufl_wr_u4 (void* addr, uint32_t value);
#endif

#ifndef ufl_read
void ufl_read (void* dst, void* src, uint32_t nwords);
#endif

#ifndef ufl_rd_u4
uint32_t ufl_rd_u4 (void* addr);
#endif

#endif
