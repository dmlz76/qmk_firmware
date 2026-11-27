// Copyright 2026 Dimitrix LLC
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// PCB v1.3.0: v1.2.0 (the same pins as v1.1.0) with an ATmega32U2 in place of the 16U2,
// and SWD from the AVR to the nRF for the SWD bridge (common/swd_bridge.c, built by
// post_rules.mk). The 32U2 is the 16U2 with 32 KB of flash and 1 KB of RAM: the bridge's
// ~3 KB and its raw HID buffer on the stack fit neither in the 16U2.
#define MMM_VER MM_PCB_VERSION(1, 3, 0)

// D1 and D2 now carry SWD, and D7 (HWB) is pulled to GND through R1 (10k) as on v1.1.0:
// none of them may get the pull-up setup_power_savings() enables on every entry here. On
// D7 it would fight R1 (~55-110 uA); on SWCLK the nRF's own ~13k pull-down (~70-100 uA,
// and an SWCLK sitting at ~0.9 V). Never drive D7 high either. D7 is only read on a
// RESET-pin reset, so briefly shorting the RESET pad enters DFU.
#define UNCONNECTED_PINS { D0, D3, D6 }

// SWD: PD2 -> R21 100R -> SWCLK, PD1 -> R22 100R -> SWDIO, the other way round from the
// dongle (common/swd.c). The bridge's gate is the erase-bonds button, SW1 on the nRF's
// P0.01 (/BOND_DELETE). Every v1.3.0 is wired, so no probe (SWD_DETECT_WIRING).
#define SWD_SWCLK_BIT PD2
#define SWD_SWDIO_BIT PD1
#define SWD_GATE_NRF_PIN 1

// Same DPDT wireless kill switch as v1.0.0: its second pole grounds the nRF's ~RESET line
// through 1 kOhm (R17) in the OFF position. See kill_switch_task() in
// common/ble_send_buf.cpp. The SWD bridge needs it ON.
#define BLE_KILL_SWITCH_ON_RESET_PIN 1

// Fitted with the 8 MHz CSTNE8M00GH5C000R0 (see common/mcu_clock.h), as since v1.2.0.
#define MCU_RESONATOR_8MHZ
// CPU at 8 MHz / 4 = 2 MHz, as on rev1_1_0. The SWD bit-bang runs off the CPU clock, so it
// is ~8x slower per bit than on the 16 MHz dongle; mm_swd.py asks for smaller CRC chunks on
// the mouse to keep each request short.
#ifndef MCU_CPU_DIV
#    define MCU_CPU_DIV 4
#endif
#include "mcu_clock.h"
