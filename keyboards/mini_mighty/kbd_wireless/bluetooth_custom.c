// Copyright 2025 Dimitrix LLC
// SPDX-License-Identifier: GPL-2.0-or-later

#include "bluetooth.h"
#include "ble_send_buf.h"
#include "reset_reason.h"
#include "gpio.h"
#include "debug.h"
#include <string.h>
#include <assert.h>


#define Timeout 150             /* milliseconds */
#define ShortTimeout 10         /* milliseconds */

// Cap on backpressure-drain iterations in the send path. Bounds how long a
// stalled/rebooting nRF can stall the QMK main loop before we give up draining
// and force the blob in (drop-oldest). Worst case ≈ SEND_BUF_MAX_DRAIN * one
// failed send_buf_send_one(Timeout); normal traffic enqueues on the first try
// and never enters the loop body.
#define SEND_BUF_MAX_DRAIN 3

void bluetooth_init(void) {
    send_buf_init(RST_PIN, SLEEP_PIN);
}

void bluetooth_task(void) {
    send_buf_send_one(ShortTimeout);
}

void bluetooth_send_keyboard(report_keyboard_t *report) {
    static_assert(KEYBOARD_REPORT_KEYS == 6, "Expected 6 keys in keyboard report");
#ifdef CONSOLE_ENABLE
    if (debug_keyboard) {
        dprintf("bluetooth_send_keyboard: mods %d, keys [%d,%d,%d,%d,%d,%d]\n", report->mods, report->keys[0], report->keys[1], report->keys[2], report->keys[3], report->keys[4], report->keys[5]);
    }
#endif

    transfer_blob_t blob;
    memset( blob.raw, 0, sizeof(blob.raw) );
    blob.type = 'K';
    blob.k.mods = report->mods;
    for (uint8_t i = 0; i < KEYBOARD_REPORT_KEYS; i++ ) {
        blob.k.keys[i] = report->keys[i];
    }

    // Bounded backpressure drain. This used to spin until the blob enqueued —
    // if the nRF stops draining over SPI (its reset/init window, or it stops
    // ACKing) the 20-deep ring stays full and this spins forever, stalling the
    // main loop (matrix scan, USB, housekeeping). That stall is itself a reset
    // trigger. Cap the attempts; if still full, force the newest blob in so the
    // loop always makes forward progress and the current key state is preserved.
    uint8_t attempts = 0;
    while (!send_buf_enqueue(&blob)) {
        if (attempts++ >= SEND_BUF_MAX_DRAIN) {
            send_buf_force_enqueue(&blob);
            break;
        }
        send_buf_send_one(Timeout);
    }
}

void bluetooth_send_mouse(report_mouse_t *report) {
#ifdef CONSOLE_ENABLE
    if (debug_mouse) {
        dprintf("bluetooth_send_mouse: buttons %02X, x %d, y %d, v %d, h %d\n", report->buttons, report->x, report->y, report->v, report->h);
    }
#endif
}
