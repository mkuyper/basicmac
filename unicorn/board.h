// Copyright (C) 2020-2021 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _board_h_
#define _board_h_

// GPIO definitions
//  3          2          1          0
// 10987654 32109876 54321098 76543210
// ________ ________ _______f ___ppppp

#define BRD_PIN(pin)            ((pin) & 0x1f)

#define BRD_GPIO_ACTIVE_LOW     (1 << 8)


#ifdef BRD_IMPL_INC
#include BRD_IMPL_INC
#endif


// Personalization
#ifndef GPIO_PERSO_DET
#define GPIO_PERSO_DET          24
#endif

#ifndef BRD_PERSO_UART
#define BRD_PERSO_UART          USART_FUART1
#endif

#endif
