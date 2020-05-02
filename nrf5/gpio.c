// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "hw.h"

#include "hal/nrf_gpio.h"

void pio_set (unsigned int pin, int value) {
    if( value < 0 ) { // input
        nrf_gpio_cfg_input(BRD_GPIO_PIN(pin),
                ( (value & 1) == 0 ) ? NRF_GPIO_PIN_PULLUP :
                ( (value & 2) == 0 ) ? NRF_GPIO_PIN_PULLDOWN : NRF_GPIO_PIN_NOPULL);
    } else {
        nrf_gpio_cfg_output(BRD_GPIO_PIN(pin));
        nrf_gpio_pin_write(BRD_GPIO_PIN(pin), value);
    }
}

void pio_activate (unsigned int pin, bool active) {
    pio_set(pin, (pin & BRD_GPIO_ACTIVE_LOW) ? !active : active);
}

int pio_get (unsigned int pin) {
    return nrf_gpio_pin_read(BRD_GPIO_PIN(pin));
}

void pio_default (unsigned int pin) {
    nrf_gpio_cfg_default(BRD_GPIO_PIN(pin));
}
