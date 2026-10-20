#pragma once

// MM_PCB_VERSION(), used by rev*/config.h to define MMM_VER. Included here
// because QMK pulls in this shared config before the leaf revision one.
#include "pcb_version.h"

#define ENCODER_RESOLUTION 1

// Wheel acceleration (encoder_update_kb in the keymap). A notch that comes at least
// WHEEL_ACCEL_SLOW_MS after the previous one scrolls one wheel unit, as it always
// has; quicker notches scale up as SLOW_MS / interval, to at most WHEEL_ACCEL_MAX
// units per notch. One unit is one host notch and macOS has no fractional wheel,
// so this can only add speed: the fine end comes from turning the host's scroll
// speed down, and this gives the long pages back. WHEEL_ACCEL_MAX 1 turns it off.
#define WHEEL_ACCEL_SLOW_MS 100
#define WHEEL_ACCEL_MAX 3

// Hi-res wheel, under test (2026-10-19). Ubuntu has no scroll-speed setting to turn down,
// so the curve above is too fast there. With this on, the mouse declares the HID Resolution
// Multiplier. A host that turns it on (Linux and Windows do, at enumeration) gets notches in
// 1/120ths, and the slow end of the curve keeps going below one notch: a notch twice
// WHEEL_ACCEL_SLOW_MS after the last scrolls half a notch, down to WHEEL_HIRES_MIN. A host
// that leaves it off (macOS is expected to) gets whole notches exactly as without it.
// Build with -e EXTRAFLAGS="-DPOINTING_DEVICE_HIRES_SCROLL_ENABLE" or uncomment below.
#define POINTING_DEVICE_HIRES_SCROLL_ENABLE
#ifdef POINTING_DEVICE_HIRES_SCROLL_ENABLE
#    define POINTING_DEVICE_HIRES_SCROLL_MULTIPLIER 120 // units per notch
#    define WHEEL_EXTENDED_REPORT                       // 120 x WHEEL_ACCEL_MAX overflows int8
#    define WHEEL_HIRES_MIN 60                          // finest step, in 1/120 notch
#endif

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

// Debug only: take the nRF out of the picture for a power measurement. Parks
// ble_send_buf.cpp in BLE_STATE_DISABLED at boot and never leaves it -- no SPI
// transactions, no blobs queued, no reset/init window -- so what the meter reads
// is the AVR, the sensor and the rails, with the radio subtracted. The MCU sleep
// path still runs, so idle current stays comparable to a normal build. Wireless
// input is dead in this build, by design; uncomment, measure, then comment back.
//
// Pairs with BLE_FORCE_DISABLED_HOLD_RESET (default 1 in ble_send_buf.cpp):
//   1 - nRF held in reset. Costs ~250 uA at the rail, because the AVR sinks the
//       module's own ~13 kOhm reset pull-up the whole time -- expect that offset
//       and don't go hunting it.
//   0 - reset released, so the nRF boots and idles on its own; isolates the AVR
//       side of the link (SPI, queueing) without the pull-up current.
// #define BLE_FORCE_DISABLED 1

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

// BLE send queue depth (ble_send_buf.cpp, default 20 = 162 B). The atmega16u2 has
// 512 B of RAM, and at 20 the stack had ~150 B. That wasn't enough: the USB control
// interrupt runs with interrupts re-enabled and stacks ~70 B, plus a nested ISR, on
// top of wherever the main loop is, and the sensor read path is ~90 B deep. So
// enumeration could overflow into timer_count and USB_DeviceState. The hi-res wheel
// build sits 27 B lower and failed about half its macOS plug-ins (2026-10-19,
// PROGRESS.md). USB never queues. Over BLE the queue only rides out an nRF reboot or
// a full nRF ring, and at the 3 ms sensor throttle 20 slots overflowed during motion
// anyway; drop-oldest keeps the newest state. Each slot is 8 B of stack. At 12 the
// static worst case (main loop + USB control ISR + a nested ISR) has ~49 B to spare
// in the plain build and ~22 B with the hi-res wheel, so recheck the headroom before
// adding RAM or raising this (PROGRESS.md 2026-10-19 has the method).
#ifndef SEND_BUF_DEPTH // -e EXTRAFLAGS="-DSEND_BUF_DEPTH=n" to try another
#    define SEND_BUF_DEPTH 12
#endif
