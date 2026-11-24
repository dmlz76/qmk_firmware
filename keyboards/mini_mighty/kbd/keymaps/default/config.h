// Copyright 2025 Dimitrix LLC
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// No tap-hold or one-shot keys in this keymap (MO() works without either), so
// drop both. That frees ~68 B of the 16U2's 512 B RAM (the tapping waiting
// buffer and tap state) and shortens the deepest key-press call chain, which
// overflowed the stack. Remove these if you add LT(), MT(), TT(), OSM() etc.
#define NO_ACTION_TAPPING
#define NO_ACTION_ONESHOT
