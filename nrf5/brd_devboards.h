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

#define GPIO_DIO1       1
#define GPIO_BUSY       2
#define GPIO_NSS        3
#define GPIO_TXRX_EN    4

#elif defined(CFG_sx1276mb1mas) || defined(CFG_sx1276mb1las)

#define GPIO_SX_RESET   16
#define GPIO_SX_DIO0    18
#define GPIO_SX_DIO1    19
#define GPIO_SX_DIO2    20
#define GPIO_SX_DIO3    22

#define GPIO_ANT_TX     17

#define GPIO_SX_NSS     11
#define GPIO_SX_SCK     14
#define GPIO_SX_MISO    13
#define GPIO_SX_MOSI    12

#define BRD_sx1276_radio
#if defined(CFG_sx1276mb1las)
#define BRD_PABOOSTSEL(f,p) true
#else
#define BRD_PABOOSTSEL(f,p) false
#endif

#else
#error "Missing radio configuration"
#endif

// Enabled USART peripherals
#define BRD_USART       (BRD_UARTE0)

#define GPIO_UARTE0_RX  26
#define GPIO_UARTE0_TX  27

// Debug LED / USART
#define GPIO_DBG_LED    (7 | BRD_GPIO_ACTIVE_LOW)
#define BRD_DBG_UART    BRD_UARTE0_PORT

#define GPIO_DBG_LED    (7 | BRD_GPIO_ACTIVE_LOW)

#endif
