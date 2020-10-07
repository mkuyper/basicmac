// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "peripherals.h"

#ifdef BRD_TMR

typedef struct {
} tmr_state;

typedef struct {
    TIM_TypeDef* timer;         // port
    volatile uint32_t* enr;     // peripheral clock enable register
    uint32_t enb;               // peripheral clock enable bit
    uint32_t irqn;              // IRQ number
    tmr_state* state;           // pointer to state (in RAM)
} tmr_p;

#if BRD_TMR_EN(BRD_TIM2)
static tmr_state state_t2;
static const tmr_p p_t2 = {
    .timer   = TIM2,
    .enr     = &RCC->APB1ENR,
    .enb     = RCC_APB1ENR_TIM2EN,
    .irqn    = TIM2_IRQn,
    .state   = &state_t2
};
const void* const tmr_t2 = &p_t2;
#endif

#endif
