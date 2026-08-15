#pragma once

#define ENCODER_RESOLUTION 2

#define MK_KINETIC_SPEED

#ifdef POINTING_DEVICE_ENABLE
#define POINTING_DEVICE_SCLK_PIN C4
#define POINTING_DEVICE_SDIO_PIN C5
#define POINTING_DEVICE_MOTION_PIN C6
#define POINTING_DEVICE_CS_PIN C7
#define POINTING_DEVICE_INVERT_Y
#define POINTING_DEVICE_TASK_THROTTLE_MS 3
#endif

#define NO_ACTION_TAPPING
#define NO_ACTION_ONESHOT
#define NO_ACTION_LAYER

#define USB_SUSPEND_WAKEUP_DELAY 200
#define NO_SUSPEND_POWER_DOWN 1

// Debug only: force HID report routing over the wireless (nRF/SPI) path even
// while USB is plugged in, so the wireless link can be exercised while the USB
// console captures logs. Uncomment (or pass -DFORCE_OUTPUT_BLUETOOTH via
// rules.mk OPT_DEFS) to enable; keymap applies it in keyboard_post_init_user().
// Leave off for normal builds (output auto-selects USB when enumerated).
// #define FORCE_OUTPUT_BLUETOOTH

// Watchdog period used for MCU power-down sleeps. Async wake (PCINT0/PCINT1)
// handles button/encoder/motion events, so this is not the latency for normal
// input -- but it IS what a *missed* wake costs: the MCU stays down for the full
// period and the event is folded away unreported. matrix_sleep_arm()'s
// check-after-arm closes the known holes; keep this modest anyway so an unknown
// one degrades to a hiccup rather than a lost click. Under evaluation at 8s
// (2026-08-13). See ble_send_buf.cpp.
//
// NB (2026-08-14): this is a real 8 s only because ble_send_buf.cpp enables the U2
// Enhanced WDT's early-warning interrupt. Without WDEWIE the atmega16u2 does not fire
// WDT_vect until *two* time-out periods, so this would be a 16 s nap credited as 8 s.
// Don't reason about naps on this board straight from the datasheet's WDP table --
// read wdt_early_warning_enable() first.
#define MCU_POWER_DOWN_WDTO WDTO_8S
