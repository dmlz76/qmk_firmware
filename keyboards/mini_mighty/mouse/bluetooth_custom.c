// Copyright 2025 Dimitrix LLC
// SPDX-License-Identifier: GPL-2.0-or-later

#include "bluetooth.h"
#include "ble_send_buf.h"
#include "gpio.h"
#include <assert.h>

#define RST_PIN D4
#define SLEEP_PIN D5

#define Timeout 150             /* milliseconds */
#define ShortTimeout 10         /* milliseconds */

void bluetooth_init(void) {
    send_buf_init(RST_PIN, SLEEP_PIN);
}

void bluetooth_task(void) {
    send_buf_send_one(ShortTimeout);
}

void bluetooth_send_keyboard(report_keyboard_t *report) {
    static_assert(KEYBOARD_REPORT_KEYS == 6, "Expected 6 keys in keyboard report");
#ifdef CONSOLE_ENABLE
    if (debug_mouse) {
        dprintf("bluetooth_send_keyboard: mods %d, keys [%d,%d,%d,%d,%d,%d]\n", report->mods, report->keys[0], report->keys[1], report->keys[2], report->keys[3], report->keys[4], report->keys[5]);
    }
#endif
}

void bluetooth_send_mouse(report_mouse_t *report) {
#ifdef CONSOLE_ENABLE
    if (debug_mouse) {
        dprintf("bluetooth_send_mouse: buttons %02X, x %d, y %d, v %d, h %d\n", report->buttons, report->x, report->y, report->v, report->h);
    }
#endif

    transfer_blob_t blob;
    blob.buttons = report->buttons;
    blob.x = report->x;
    blob.y = report->y;
    blob.v = report->v;

    while (!send_buf_enqueue(&blob)) {
        send_buf_send_one(Timeout);
    }
}
