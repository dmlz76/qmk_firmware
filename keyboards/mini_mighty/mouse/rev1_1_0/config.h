// Copyright 2026 Dimitrix LLC
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#define MMM_VER MM_PCB_VERSION(1, 1, 0)

// D7 (the atmega16u2's HWB pin) is deliberately NOT listed. v1.1.0 pulls it to
// GND through R1 (10k), so briefly shorting the RESET pad enters DFU. HWB is
// only read on a RESET-pin reset, so power-on, brown-out, watchdog and USB
// resets still start the app. The pin must stay a plain input: the pull-up that
// setup_power_savings() enables on every UNCONNECTED_PINS entry would fight R1
// and draw ~55-110 uA for as long as the app runs, about double the sleep
// budget. Never drive D7 high either.
#define UNCONNECTED_PINS { D0, D1, D2, D3, D6 }

// Same DPDT wireless kill switch as v1.0.0: its second pole grounds the nRF's
// ~RESET line through 1 kOhm in the OFF position. See kill_switch_task() in
// common/ble_send_buf.cpp.
#define BLE_KILL_SWITCH_ON_RESET_PIN 1
