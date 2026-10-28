// Copyright 2026 Dimitrix LLC
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>
#include <stdbool.h>

// Bit-banged SWD from the 16U2 to the nRF52805, dongle PCB v7 and later. See swd.c for the
// wiring and why the pins are driven the way they are; swd_bridge.c is the only user.

// What an SWD operation came to. 0 is success; the bridge passes the others on to the host
// as ST_SWD + the value.
enum {
    SWD_OK = 0,
    SWD_ERR_WAIT,     // the target answered WAIT to every retry
    SWD_ERR_FAULT,    // the target answered FAULT, or a write left STICKYERR set
    SWD_ERR_PROTOCOL, // no valid ACK: nothing there, or the line lost sync. Reconnect.
    SWD_ERR_PARITY,   // read data failed its parity bit
};

// The word every access below reads into or writes from. One static word rather than a
// value or pointer argument at each level, because those cost stack frames, and the bridge
// runs on top of raw HID's 32-byte buffer with the USB ISR able to nest above it (QMK
// PROGRESS 2026-10-27).
extern uint32_t swd_data;

// True when PD1 has v7's pull-down on it (R9 + R10, and the nRF's own on SWCLK). Only call
// it with the pins off.
bool swd_wired(void);

void swd_pins_off(void);

// Pins on, line reset, DPIDR (into swd_data), debug power-up, MEM-AP CSW. Leaves the pins
// on whatever the result. CONNECT again after anything that may have reset the AHB-AP: a
// pin reset, an ERASEALL.
uint8_t swd_connect(void);
// Debug power-down. Leaves the pins on.
uint8_t swd_power_down(void);

// An AP register, read into or written from swd_data.
uint8_t swd_ap(uint8_t ap, uint8_t reg, bool read);

// One word at addr through AP 0, the AHB-AP, read into or written from swd_data. Writes
// are posted: swd_mem_end() waits for the last one and reports whether any of them failed.
uint8_t swd_mem(uint32_t addr, bool read);
uint8_t swd_mem_end(void);
