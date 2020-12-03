// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "peripherals.h"

#ifdef BRD_TMR

enum {
    F_ON        = 1 << 0,
};

typedef struct {
    uint32_t flags;             // flags
    tmr_cb cb;                  // callback
} tmr_state;

typedef struct {
    TIM_TypeDef* timer;         // port
    volatile uint32_t* enr;     // peripheral clock enable register
    uint32_t enb;               // peripheral clock enable bit
    uint32_t irqn;              // IRQ number
    tmr_state* state;           // pointer to state (in RAM)
} tmr_p;

static void tmr_irq (const tmr_p* tmr);

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
void tmr_t2_irq (void) {
    tmr_irq(tmr_t2);
}
#endif
#if BRD_TMR_EN(BRD_TIM3)
static tmr_state state_t3;
static const tmr_p p_t3 = {
    .timer   = TIM3,
    .enr     = &RCC->APB1ENR,
    .enb     = RCC_APB1ENR_TIM3EN,
    .irqn    = TIM3_IRQn,
    .state   = &state_t3
};
const void* const tmr_t3 = &p_t3;
void tmr_t3_irq (void) {
    tmr_irq(tmr_t3);
}
#endif

void tmr_start (const void* p, uint32_t psc) {
    const tmr_p* tmr = p;

    if( (tmr->state->flags & F_ON) == 0 ) {
        tmr->state->flags |= F_ON;
        hal_setMaxSleep(HAL_SLEEP_S0);
        *tmr->enr |= tmr->enb;          // enable peripheral clock
    }
    tmr->timer->PSC = psc;              // set prescaler
}

void tmr_stop (const void* p) {
    const tmr_p* tmr = p;

    if( (tmr->state->flags & F_ON) != 0 ) {
        tmr->state->flags &= ~F_ON;
        tmr->timer->CR1 = 0;            // halt timer
        tmr->timer->DIER = 0;           // disable all interrupts
        NVIC_DisableIRQ(tmr->irqn);     // disable interrupt in NVIC
        *tmr->enr &= ~tmr->enb;         // stop peripheral clock
        hal_clearMaxSleep(HAL_SLEEP_S0);
    }
}

void tmr_run (const void* p, uint32_t count, tmr_cb cb, bool once) {
    const tmr_p* tmr = p;

    ASSERT((tmr->state->flags & F_ON) != 0);

    tmr->state->cb = cb;

    tmr->timer->CNT = 0;                      // reset counter
    tmr->timer->ARR = count;                  // set auto-reload register
    tmr->timer->EGR = TIM_EGR_UG;             // refresh registers

    tmr->timer->SR = 0;                       // clear interrupt flags
    tmr->timer->DIER = cb ? TIM_DIER_UIE : 0; // enable update interrupt
    NVIC_EnableIRQ(tmr->irqn);                // enable interrupt in NVIC

    tmr->timer->CR1 = TIM_CR1_CEN
        | (once ? TIM_CR1_OPM : 0 );          // enable timer
}

void tmr_halt (const void* p) {
    const tmr_p* tmr = p;

    ASSERT((tmr->state->flags & F_ON) != 0);

    tmr->timer->CR1 = 0;                      // halt timer
}

uint32_t tmr_get (const void* p) {
    const tmr_p* tmr = p;
    return tmr->timer->CNT;
}

static void tmr_irq (const tmr_p* tmr) {
    if( tmr->state->cb ) {
        tmr->state->cb();
    }
}

#endif
