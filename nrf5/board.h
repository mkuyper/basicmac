// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _board_h_
#define _board_h_

// ------------------------------------------------
// GPIO definition
//  3          2          1          0
// 10987654 32109876 54321098 76543210
// ________ ________ _______f ___ppppp

#define BRD_GPIO_PIN(gpio)      ((gpio) & 0x1f)

// flags (f)
#define BRD_GPIO_ACTIVE_LOW     (1 << 8)


#ifdef BRD_IMPL_INC
#include BRD_IMPL_INC
#else
#error "Missing board implementation include file"
#endif

#endif