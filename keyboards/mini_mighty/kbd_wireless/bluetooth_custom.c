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

// Bounded backpressure drain. This used to spin until the blob enqueued —
// if the nRF stops draining over SPI (its reset/init window, or it stops
// ACKing) the 20-deep ring stays full and this spins forever, stalling the
// main loop (matrix scan, USB, housekeeping). That stall is itself a reset
// trigger. Cap the attempts; if still full, force the newest blob in so the
// loop always makes forward progress and the current key state is preserved.
static void enqueue_blob(const transfer_blob_t *blob) {
    uint8_t attempts = 0;
    while (!send_buf_enqueue(blob)) {
        if (attempts++ >= SEND_BUF_MAX_DRAIN) {
            send_buf_force_enqueue(blob);
            return;
        }
        send_buf_send_one(Timeout);
    }
}

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

    enqueue_blob(&blob);
}

// Consumer ('C') and system ('S') usages. QMK hands these to the host driver
// through send_extra rather than send_keyboard, which is why they need their
// own hooks: without them the link resolves bluetooth_send_consumer and
// bluetooth_send_system to the weak no-ops in drivers/bluetooth/bluetooth.c
// and every extra key is silently swallowed before it reaches the send buffer.
//
// Zero is a real value here, not a sentinel to skip: host_consumer_send() and
// host_system_send() emit usage 0 as the release event for whatever was held,
// and dedupe against the previous usage themselves, so each call is already a
// state change worth a blob.
static void send_extra_usage(uint8_t type, uint16_t usage) {
    transfer_blob_t blob;
    memset( blob.raw, 0, sizeof(blob.raw) );
    blob.type = type;
    blob.e.usage_lo = (uint8_t)(usage & 0xFF);
    blob.e.usage_hi = (uint8_t)(usage >> 8);

    enqueue_blob(&blob);
}

void bluetooth_send_consumer(uint16_t usage) {
#ifdef CONSOLE_ENABLE
    if (debug_keyboard) {
        dprintf("bluetooth_send_consumer: usage %04X\n", usage);
    }
#endif
    send_extra_usage('C', usage);
}

void bluetooth_send_system(uint16_t usage) {
#ifdef CONSOLE_ENABLE
    if (debug_keyboard) {
        dprintf("bluetooth_send_system: usage %04X\n", usage);
    }
#endif
    send_extra_usage('S', usage);
}

void bluetooth_send_mouse(report_mouse_t *report) {
#ifdef CONSOLE_ENABLE
    if (debug_mouse) {
        dprintf("bluetooth_send_mouse: buttons %02X, x %d, y %d, v %d, h %d\n", report->buttons, report->x, report->y, report->v, report->h);
    }
#endif
}
