// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _nrfx_glue_h_
#define _nrfx_glue_h_

#include "lmic.h"

// -----------------------------------------------------------------------------

#define NRFX_ASSERT(expression) \
    ASSERT(expression)

#define NRFX_STATIC_ASSERT(expression) \
    _Static_assert(expression, "")

// -----------------------------------------------------------------------------

#define NRFX_IRQ_PRIORITY_SET(irq_number, priority) do { \
    NVIC_SetPriority(irq_number, priority); \
} while( 0 )

#define NRFX_IRQ_ENABLE(irq_number) do { \
    NVIC_EnableIRQ(irq_number); \
} while( 0 )

#define NRFX_IRQ_IS_ENABLED(irq_number) \
    _nrfx_irq_is_enabled(irq_number)
static inline bool _nrfx_irq_is_enabled (unsigned int irq_number) {
    return (NVIC->ISER[irq_number >> 5] & (1 << (irq_number & 0x1f))) != 0;
}

#define NRFX_IRQ_DISABLE(irq_number) do { \
    NVIC_DisableIRQ(irq_number); \
} while( 0 )

#define NRFX_IRQ_PENDING_SET(irq_number) do { \
    NVIC_SetPendingIRQ(irq_number); \
} while( 0 )

#define NRFX_IRQ_PENDING_CLEAR(irq_number) do { \
    NVIC_ClearPendingIRQ(irq_number); \
} while( 0 )

#define NRFX_IRQ_IS_PENDING(irq_number) \
    ( NVIC_GetPendingIRQ(irq_number) != 0 )

#define NRFX_CRITICAL_SECTION_ENTER() do { \
    hal_disableIRQs(); \
}

#define NRFX_CRITICAL_SECTION_EXIT() do { \
    hal_enableIRQs(); \
}

#endif

