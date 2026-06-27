// Copyright 2025 Dimitrix LLC
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H
#include "reset_reason.h"
#include "connection.h"

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

#ifdef FORCE_OUTPUT_BLUETOOTH
    // Debug aid: pin HID report routing to the wireless (nRF/SPI) path even
    // while USB is plugged in, so the report traffic exercises the BLE link
    // while the USB console stays available for logs. The connection
    // framework would otherwise auto-select USB whenever it's enumerated.
    // noeeprom = not persisted; clears on reflash without this define.
    // Note: while forced, the host's USB keyboard/mouse HID interfaces stay
    // enumerated but receive no reports (cursor won't move over USB) — that's
    // expected; the console endpoint is unaffected.
    connection_set_host_noeeprom(CONNECTION_HOST_BLUETOOTH);
#endif
}

void pointing_device_init_user(void) {
    pointing_device_set_cpi(1600);
}

void housekeeping_task_user(void) {
    // Re-emit the reset reason for a few seconds so qmk console catches it.
    reset_reason_task();
}

// Encoder → mouse wheel, written straight into the pointing-device report.
// We override encoder_update_kb (not _user) on purpose: the weak default only
// emits QK_MOUSE_WHEEL_* under MOUSEKEY_ENABLE (else KC_PGUP/PGDN keyboard taps),
// and MOUSEKEY is off here to fit CONSOLE_ENABLE on the atmega16u2. Replacing the
// default also drops its tap_code(KC_PGUP/PGDN) machinery from the build, which
// matters on this nearly-full 16u2. Populate report->v directly — no mousekey.
bool encoder_update_kb(uint8_t index, bool clockwise) {
    report_mouse_t report = pointing_device_get_report();
    report.v = clockwise ? 1 : -1;
    pointing_device_set_report(report);
    pointing_device_send();
    return true;
}
