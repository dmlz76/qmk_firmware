// Copyright 2025 Dimitrix LLC
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// RAM-backed HID console.
//
// QMK's sendchar() drops a character to the void whenever the USB console isn't
// configured or no listener is attached (e.g. across a re-enumeration, or while
// qmk console is detached). That loses exactly the lines printed at the moment
// of a USB disturbance — the ones we most want to see.
//
// This queues all print/uprintf output into a RAM ring buffer instead, and
// drains it to the real console only once it's ready. Nothing is lost (short of
// overflowing the ring, which drops oldest), and one-shot prints (e.g. the reset
// reason) survive until a console attaches — so they no longer need re-emitting.

#ifdef __cplusplus
extern "C" {
#endif

// Install the buffering sendchar. Call once at init (e.g. keyboard_post_init_user),
// after QMK's keyboard_setup() has installed the default sendchar.
void console_buffer_init(void);

// Drain queued bytes to the HID console when it's ready. Call every loop from
// housekeeping_task_user(). No-op without CONSOLE_ENABLE.
void console_buffer_flush_task(void);

#ifdef __cplusplus
}
#endif
