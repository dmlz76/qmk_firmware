// Copyright 2026 Dimitrix LLC
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// PCB revision numbering for the mini_mighty boards.
//
// Revisions used to be bare integers (MMM_VER 11/12, MMKW_VER 1/2) matching the
// sequential PCB numbering of the prototypes. The mouse PCB restarted at v1.0.0
// on 2026-09-11 for the production run, so a bare integer can no longer order
// the revisions: 1.0.0 has to sort *above* the old 12. Encoding the three parts
// into one comparable number fixes that, and the old boards map on as 0.<n>.0 --
// MMM_VER 12 becomes MM_PCB_VERSION(0, 12, 0) -- which leaves every existing
// `>=` comparison ordering exactly the way it did before.
//
// Each board's rev*/config.h sets its own MMM_VER / MMKW_VER; feature gates that
// depend on it live in the board's post_config.h, because QMK includes the
// shared <board>/config.h *before* the leaf rev*/config.h and only the
// post_config.h pass runs late enough to see the value.
//
// Safe in #if: the preprocessor evaluates arithmetic in intmax_t. The UL suffix
// is there for the C expression case, where a 16-bit int would overflow on any
// major version above 3.
//
// minor and patch are limited to 0..99 by the encoding.
#define MM_PCB_VERSION(major, minor, patch) (((major) * 10000UL) + ((minor) * 100UL) + (patch))
