// Copyright 2025 Dimitrix LLC
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Strip QMK's own dprintf() debug messages in console builds. The deepest one
// was host.c's keyboard_report: line, printed from inside the USB control ISR
// (clear_keyboard on SET_PROTOCOL/SET_CONFIGURATION). That made the worst-case
// stack ~56 B more than the 16U2 has, with the SPI frame ring just below it
// (2026-11-23, PROGRESS.md). Our logs (spi received:, battery:, spi dropped:)
// are uprintf, so they still print. Non-console builds already have NO_DEBUG.
#ifndef NO_DEBUG
#    define NO_DEBUG
#endif
