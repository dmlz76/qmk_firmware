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
