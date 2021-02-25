// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _nrfx_config_h_
#define _nrfx_config_h_


// CLOCK
#define NRFX_CLOCK_ENABLED 1
#define NRFX_CLOCK_CONFIG_LF_SRC 1 // XTAL
#define NRFX_CLOCK_DEFAULT_CONFIG_IRQ_PRIORITY 5 //XXX

// GPIOTE
#define NRFX_GPIOTE_ENABLED 1
#define NRFX_GPIOTE_CONFIG_NUM_OF_LOW_POWER_EVENTS 4

// RTC
#define NRFX_RTC_ENABLED 1
#define NRFX_RTC1_ENABLED 1

// TIMER
#define NRFX_TIMER_ENABLED 1
#define NRFX_TIMER1_ENABLED 1

// UARTE
#define NRFX_UARTE_ENABLED 1
#define NRFX_UARTE0_ENABLED 1

// SPIM
#define NRFX_SPIM_ENABLED 1
#define NRFX_SPIM0_ENABLED 1

// RNG
#define NRFX_RNG_ENABLED 1
#define NRFX_RNG_DEFAULT_CONFIG_IRQ_PRIORITY 5 //XXX


#endif
