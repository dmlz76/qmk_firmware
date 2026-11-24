// Copyright 2025 Dimitrix LLC
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Strip QMK's own dprintf() debug messages in console builds. The deepest one is
// host.c's keyboard_report: line, which runs inside the USB control ISR
// (clear_keyboard on SET_PROTOCOL/SET_CONFIGURATION). Without NO_DEBUG the
// worst-case stack is 279 of the 293 B free; with it, 222 (2026-11-23,
// PROGRESS.md). Before NO_ACTION_TAPPING below, it was also needed to fit 12 KB.
// Our logs (keymap.c) are uprintf, the key log behind debug_enable, so they still
// print. Non-console builds already have NO_DEBUG. DB_TOGG does nothing with this.
#ifndef NO_DEBUG
#    define NO_DEBUG
#endif

// No tap-hold or one-shot keys in this keymap (MO() works without either), so
// drop both. That frees ~68 B of the 16U2's 512 B RAM (the tapping waiting
// buffer and tap state) and shortens the deepest key-press call chain, which
// overflowed the stack. Remove these if you add LT(), MT(), TT(), OSM() etc.
#define NO_ACTION_TAPPING
#define NO_ACTION_ONESHOT
