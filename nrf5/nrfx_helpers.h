// Copyright (C) 2020-2021 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _nrfx_helpers_h_
#define _nrfx_helpers_h_

#include "nrfx_rtc.h"
#include "nrfx_spim.h"
#include "nrfx_uarte.h"

// Things that should be in nrfx ;-)

static inline bool nrfx_rtc_overflow_pending (nrfx_rtc_t const * p_instance) {
    return nrf_rtc_event_check(p_instance->p_reg, NRF_RTC_EVENT_OVERFLOW);
}

static inline void nrfx_spim_suspend (nrfx_spim_t const * p_instance) {
    nrf_spim_disable(p_instance->p_reg);
}

static inline void nrfx_spim_resume (nrfx_spim_t const * p_instance) {
    nrf_spim_enable(p_instance->p_reg);
}

static inline void nrfx_uarte_suspend (nrfx_uarte_t const * p_instance) {
    nrf_uarte_disable(p_instance->p_reg);
}

static inline void nrfx_uarte_resume (nrfx_uarte_t const * p_instance) {
    nrf_uarte_enable(p_instance->p_reg);
}

#endif
