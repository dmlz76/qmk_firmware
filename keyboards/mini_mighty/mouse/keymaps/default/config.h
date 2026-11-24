// Copyright 2025 Dimitrix LLC
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Strip QMK's own dprintf() debug messages to save ~700 B of the 16U2's 12 KB in
// console builds. Our logs (bt_mouse:, wheel:, MCUSR=, ble_send_buf's) are
// uprintf, the per-report ones behind debug_enable, so they still print.
// Non-console builds already have NO_DEBUG.
#ifndef NO_DEBUG
#    define NO_DEBUG
#endif
