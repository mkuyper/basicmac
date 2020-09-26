// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "lmic.h"
#include "peripherals.h"
#include "svcdefs.h" // for type-checking hook functions

// Check prerequisistes and generate nice warnings
#ifndef GPIO_PERSO_DET
#error "Personalization module requires detect I/O line, please define GPIO_PERSO_DET"
#endif
//#ifndef BRD_PERSO_UART
//#error "Personalization module requires UART, please define BRD_PERSO_UART"
//#endif


bool _perso_main (osjob_t* job) {
    // TODO - check fuse

    // sample detect line, enter perso mode if externally driven high
    pio_set(GPIO_PERSO_DET, PIO_INP_PDN);
    hal_waitUntil(os_getTime() + us2osticks(100));
    bool enter_perso = pio_get(GPIO_PERSO_DET);
    pio_default(GPIO_PERSO_DET);

    if( enter_perso ) {
        debug_printf("perso: Entering perso...\r\n");
    }

    return enter_perso;
}
