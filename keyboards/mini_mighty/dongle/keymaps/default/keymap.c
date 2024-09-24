// Copyright 2023 QMK
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
[0] = LAYOUT(
)
};
// clang-format on

void keyboard_post_init_user(void) {
    // Customise these values to desired behaviour
#ifdef CONSOLE_ENABLE
    debug_enable = true;
#else
    debug_enable = false;
#endif
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    return true;
}
