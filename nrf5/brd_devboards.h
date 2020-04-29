// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

// This board implementation file is included from board.h


// -------------------------------------------
#if defined(CFG_wrl13990_board)

#if defined(CFG_sx1261mbed) || defined(CFG_sx1262mbed)

#if defined(CFG_sx1261mbed)
#define BRD_sx1261_radio
#elif defined(CFG_sx1262mbed)
#define BRD_sx1262_radio
#endif

#define GPIO_DIO1       BRD_GPIO_AF_EX(PORT_B, 4, 4, BRD_GPIO_CHAN(1))
#define GPIO_BUSY       BRD_GPIO(PORT_B, 3)
#define GPIO_NSS        BRD_GPIO(PORT_A, 8)
#define GPIO_TXRX_EN    BRD_GPIO(PORT_A, 9)

#else
#error "Missing radio configuration"
#endif

#endif
