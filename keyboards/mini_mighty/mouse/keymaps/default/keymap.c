// Copyright 2025 Dimitrix LLC
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/power.h>
#include <avr/interrupt.h>


// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
[0] = LAYOUT(
    KC_BTN1, KC_BTN3, KC_BTN2
)
};
// clang-format on

void keyboard_post_init_user(void) {
#ifdef CONSOLE_ENABLE
    debug_enable = true;
    debug_keyboard = false;
    debug_mouse = true;
#else
    debug_enable = false;
    debug_keyboard = false;
    debug_mouse = false;
#endif
}

void pointing_device_init_user(void) {
    pointing_device_set_cpi(1600);
}
