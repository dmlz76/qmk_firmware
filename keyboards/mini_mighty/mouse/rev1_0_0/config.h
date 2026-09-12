// Copyright 2026 Dimitrix LLC
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#define MMM_VER MM_PCB_VERSION(1, 0, 0)

// D7 is the atmega16u2's HWB pin, wired to test point TP1 (short it to GND while
// plugging in USB to force DFU on a board with dead firmware). The via floats
// otherwise, so it still belongs here -- the pull-up only exists while the app
// runs, and a reset hands the pin back to the bootloader undriven.
#define UNCONNECTED_PINS { D0, D1, D2, D3, D6, D7 }
