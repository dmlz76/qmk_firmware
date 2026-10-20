#pragma once

#define NO_ACTION_TAPPING
#define NO_ACTION_ONESHOT

#define NO_ACTION_LAYER

#define USB_SUSPEND_WAKEUP_DELAY 200
#define NO_SUSPEND_POWER_DOWN 1

// Uncomment on a dongle fitted with the 8 MHz CSTNE8M00GH5C000R0 (see
// common/mcu_clock.h). Untested on the dongle so far. Halving the clock halves the
// cycles the SPI slave ISR has per byte from the nRF: 512 at 125 kHz.
// #define MCU_RESONATOR_8MHZ
#include "mcu_clock.h"

// Fork-local: report the paired peripherals' battery levels to the host as HID Battery Strength
// (see peer_battery.c, and KeyboardReport/SharedReport in tmk_core/protocol/usb_descriptor.c).
#define HID_BATTERY_ENABLE

// Hi-res wheel over GZLL, under test (2026-10-19): the dongle's half of the mouse's
// POINTING_DEVICE_HIRES_SCROLL_ENABLE. The dongle then declares the HID Resolution
// Multiplier. While its host has it on (Linux and Windows do, at enumeration), the dongle
// sends each 'M' blob's 1/120-notch wheel. Otherwise (macOS) it sends whole notches, as
// without it. Safe with any mouse firmware: one that doesn't fill in the hi-res wheel reads
// as whole notches x 120 (transfer_mouse_v_hr()). Build with
// -e EXTRAFLAGS="-DPOINTING_DEVICE_HIRES_SCROLL_ENABLE", or uncomment below.
#define POINTING_DEVICE_HIRES_SCROLL_ENABLE
#ifdef POINTING_DEVICE_HIRES_SCROLL_ENABLE
#    define WHEEL_EXTENDED_REPORT // the hi-res wheel is int16
#endif
