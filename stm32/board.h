// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _board_h_
#define _board_h_

// GPIO definitions
// 33222222 22221111 11111100 00000000
// 10987654 32109876 54321098 76543210
// ________ _____fff ccccaaaa PPPPpppp

#define BRD_GPIO(port,pin)              (((port) << 4) | (pin))
#define BRD_GPIO_EX(port,pin,ex)        (((port) << 4) | (pin) | (ex))
#define BRD_GPIO_AF(port,pin,af)        (((af) << 8) | ((port) << 4) | (pin))
#define BRD_GPIO_AF_EX(port,pin,af,ex)  (((af) << 8) | ((port) << 4) | (pin) | (ex))
#define BRD_PIN(gpio)                   ((gpio) & 0x0f)
#define BRD_PORT(gpio)                  (((gpio) >> 4) & 0x0f)
#define BRD_AF(gpio)                    (((gpio) >> 8) & 0x0f)

// alternate function configuratons (c)
#define BRD_GPIO_CHAN(ch)               ((ch) << 12)
#define BRD_GPIO_GET_CHAN(gpio)         (((gpio) >> 12) & 0x07)

// flags (f)
#define BRD_GPIO_EXT_PULLUP             (1 << 16)
#define BRD_GPIO_EXT_PULLDN             (1 << 17)
#define BRD_GPIO_ACTIVE_LOW             (1 << 18)

// GPIO ports
#define PORT_A  0
#define PORT_B  1
#define PORT_C  2

// macros to define DMA channels
#define BRD_DMA_CHAN(a)                 (a)
#define BRD_DMA_CHANS(a,b)              (((b) << 4) | (a))
#define BRD_DMA_CHAN_A(x)               (((x) & 0xf) - 1)
#define BRD_DMA_CHAN_B(x)               ((((x) >> 4) & 0xf) - 1)

// UART instances
#define BRD_USART1                      (1 << 0)
#define BRD_USART2                      (1 << 1)
#define BRD_LPUART1                     (1 << 2)

// UART ports
#define BRD_USART1_PORT                 usart_port_u1
#define BRD_USART2_PORT                 usart_port_u2
#define BRD_LPUART1_PORT                usart_port_lpu1

#define BRD_USART_EN(m)                 (((BRD_USART) & (m)) != 0)

// Timer instances
#define BRD_TIM2                        (1 << 0)
#define BRD_TIM3                        (1 << 1)

// Timer peripherals
#define BRD_TIM2_PERIPH                 tmr_t2
#define BRD_TIM3_PERIPH                 tmr_t3

#define BRD_TMR_EN(m)                   (((BRD_TMR) & (m)) != 0)


#ifdef BRD_IMPL_INC
#include BRD_IMPL_INC
#else
#error "Missing board implementation include file"
#endif

#endif
