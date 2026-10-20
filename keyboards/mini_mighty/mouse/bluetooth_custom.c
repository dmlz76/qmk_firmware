// Copyright 2025 Dimitrix LLC
// SPDX-License-Identifier: GPL-2.0-or-later

#include "bluetooth.h"
#include "ble_send_buf.h"
#include "reset_reason.h"
#include "gpio.h"
#include "debug.h"
#include <string.h>
#include <assert.h>

#define RST_PIN D4
#define SLEEP_PIN D5

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
    // Mouse never emits keyboard reports; this driver hook is an unused stub.
    static_assert(KEYBOARD_REPORT_KEYS == 6, "Expected 6 keys in keyboard report");
}

#ifdef POINTING_DEVICE_HIRES_SCROLL_ENABLE
// The encoder's notch in 1/120 notch, set just before it sends the same notch's whole
// notches (keymap.c encoder_update_kb). Taken by the next report.
static int16_t s_wheel_hires;

void bluetooth_set_wheel_hires(int16_t v_hr) {
    s_wheel_hires = v_hr;
}
#endif

void bluetooth_send_mouse(report_mouse_t *report) {
#ifdef CONSOLE_ENABLE
    if (debug_mouse) {
        dprintf("bt_mouse: b%02X x%d y%d v%d h%d\n", report->buttons, report->x, report->y, report->v, report->h);
    }
#endif

    transfer_blob_t blob;
    memset( blob.raw, 0, sizeof(blob.raw) );
    blob.type = 'M';
    blob.m.buttons = report->buttons;
    blob.m.x = report->x;
    blob.m.y = report->y;
    blob.m.v = report->v; // whole notches: the encoder goes hi-res only while USB is the host
#ifdef POINTING_DEVICE_HIRES_SCROLL_ENABLE
    // Both units of the same notch; the nRF or the dongle picks one for its host. Only
    // next to a wheel report, so a stale value can't ride along with motion. Without one
    // the flag stays clear, and the receiver reads m.v x 120 (transfer_mouse_v_hr()).
    if (report->v && s_wheel_hires) {
        transfer_mouse_set_v_hr(&blob, s_wheel_hires);
    }
    s_wheel_hires = 0;
#endif

    // Bounded backpressure drain. This used to spin until the blob enqueued —
    // if the nRF stops draining over SPI (its reset/init window, or it stops
    // ACKing) the ring stays full and this spins forever, stalling the
    // main loop (matrix scan, USB, housekeeping). That stall is itself a reset
    // trigger. Cap the attempts; if still full, force the newest blob in so the
    // loop always makes forward progress and the current button/motion state is
    // preserved.
    uint8_t attempts = 0;
    while (!send_buf_enqueue(&blob)) {
        if (attempts++ >= SEND_BUF_MAX_DRAIN) {
            send_buf_force_enqueue(&blob);
            break;
        }
        send_buf_send_one(Timeout);
    }
}
