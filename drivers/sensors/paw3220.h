/* Copyright 2025 Dimitar Lazarov (dimitrix llc)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "pointing_device.h"

#ifndef PAW3220_CS_PIN
#    ifdef POINTING_DEVICE_CS_PIN
#        define PAW3220_CS_PIN POINTING_DEVICE_CS_PIN
#    else
#        error "No chip select pin defined -- missing POINTING_DEVICE_CS_PIN or PAW3220_CS_PIN"
#    endif
#endif
#ifndef PAW3220_SCLK_PIN
#    ifdef POINTING_DEVICE_SCLK_PIN
#        define PAW3220_SCLK_PIN POINTING_DEVICE_SCLK_PIN
#    else
#        error "No clock pin defined -- missing POINTING_DEVICE_SCLK_PIN or PAW3220_SCLK_PIN"
#    endif
#endif
#ifndef PAW3220_SDIO_PIN
#    ifdef POINTING_DEVICE_SDIO_PIN
#        define PAW3220_SDIO_PIN POINTING_DEVICE_SDIO_PIN
#    else
#        error "No data pin defined -- missing POINTING_DEVICE_SDIO_PIN or PAW3220_SDIO_PIN"
#    endif
#endif

typedef struct {
    int16_t x;
    int16_t y;
    bool    isMotion;
} report_paw3220_t;

const pointing_device_driver_t paw3220_pointing_device_driver;

void paw3220_init(void);
report_paw3220_t paw3220_read(void);
void paw3220_set_cpi(uint16_t cpi);
uint16_t paw3220_get_cpi(void);
report_mouse_t paw3220_get_report(report_mouse_t mouse_report);
