// Copyright (C) 2020-2021 Michael Kuyper. All rights reserved.
// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _hw_h_
#define _hw_h_

#include <stdint.h>

#define PERIPH_EEPROM

#define EEPROM_BASE     0x30000000
#define EEPROM_SZ       (8 * 1024)
#define EEPROM_END      (EEPROM_BASE + EEPROM_SZ)

// 0x0000-0x003f   64 B : reserved for bootloader
// 0x0040-0x005f   32 B : reserved for persistent stack data
// 0x0060-0x00ff  160 B : reserved for personalization data
// 0x0100-......        : reserved for application

#define STACKDATA_BASE          (EEPROM_BASE + 0x0040)
#define PERSODATA_BASE          (EEPROM_BASE + 0x0060)
#define APPDATA_BASE            (EEPROM_BASE + 0x0100)

#define STACKDATA_SZ            (PERSODATA_BASE - STACKDATA_BASE)
#define PERSODATA_SZ            (APPDATA_BASE - PERSODATA_BASE)
#define APPDATA_SZ              (EEPROM_END - APPDATA_BASE)

#define PERIPH_FLASH
#define FLASH_BASE              0x20000000
#define FLASH_SZ                (128 * 1024)
#define FLASH_END               (FLASH_BASE + FLASH_SZ)
#define FLASH_PAGE_SZ           128
#define FLASH_PAGE_NW		(FLASH_PAGE_SZ >> 2)

#define PERIPH_USART
#define USART_FUART1            ((void*) 1)

typedef struct {
    void* reg;
    uint32_t mask;
} pio_direct;
#define PERIPH_PIO
#define PERIPH_PIO_DIRECT
#define PIO_IRQ_LINE(gpio) (gpio)

#define PERIPH_CRC
#define PERIPH_SHA256

#endif
