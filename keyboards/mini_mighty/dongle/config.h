#pragma once

#define NO_ACTION_TAPPING
#define NO_ACTION_ONESHOT

#define NO_ACTION_LAYER

#define USB_SUSPEND_WAKEUP_DELAY 200
#define NO_SUSPEND_POWER_DOWN 1

// Fork-local: report the paired peripherals' battery levels to the host as HID Battery Strength
// (see peer_battery.c, and KeyboardReport/SharedReport in tmk_core/protocol/usb_descriptor.c).
#define HID_BATTERY_ENABLE
