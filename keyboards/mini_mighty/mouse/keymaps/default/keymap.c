// Copyright 2025 Dimitrix LLC
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H
#include "reset_reason.h"
#include "connection.h"
#ifdef POINTING_DEVICE_HIRES_SCROLL_ENABLE
#    include "usb_device_state.h"
#endif

#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/power.h>
#include <avr/interrupt.h>


// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
[0] = LAYOUT(
    MS_BTN1, MS_BTN3, MS_BTN2
)
};
// clang-format on

void keyboard_post_init_user(void) {
#ifdef CONSOLE_ENABLE
    debug_enable = true;
    debug_keyboard = false;
    debug_mouse = true;
#else
    debug_enable = false;
    debug_keyboard = false;
    debug_mouse = false;
#endif

#ifdef FORCE_OUTPUT_BLUETOOTH
    // Debug aid: pin HID report routing to the wireless (nRF/SPI) path even
    // while USB is plugged in, so the report traffic exercises the BLE link
    // while the USB console stays available for logs. The connection
    // framework would otherwise auto-select USB whenever it's enumerated.
    // noeeprom = not persisted; clears on reflash without this define.
    // Note: while forced, the host's USB keyboard/mouse HID interfaces stay
    // enumerated but receive no reports (cursor won't move over USB) — that's
    // expected; the console endpoint is unaffected.
    connection_set_host_noeeprom(CONNECTION_HOST_BLUETOOTH);
#endif
}

void pointing_device_init_user(void) {
    pointing_device_set_cpi(1600);
}

#ifdef CONSOLE_ENABLE
// The previous boot's reset reason and uptime, kept in .noinit so they survive a
// warm reset (brown-out, watchdog, external) but not a power-off. Plugging USB
// into a mouse running on battery browns out the AVR (MCUSR=0x04: the boost is
// switched off before the LDO can carry the rail), so the boot the console sees
// is never the one we wanted to ask about. This lets it report the one before.
#    define PREV_BOOT_MAGIC 0xB007
static uint16_t s_nv_magic __attribute__((section(".noinit")));
static uint8_t  s_nv_mcusr __attribute__((section(".noinit")));
static uint32_t s_nv_uptime __attribute__((section(".noinit")));
static uint8_t  s_prev_mcusr;
static uint32_t s_prev_uptime;
static bool     s_prev_valid;

void keyboard_pre_init_user(void) {
    // Read last boot's record before this boot overwrites it. The magic rejects
    // the random contents .noinit holds after a real power-on.
    s_prev_valid  = (s_nv_magic == PREV_BOOT_MAGIC);
    s_prev_mcusr  = s_nv_mcusr;
    s_prev_uptime = s_nv_uptime;
    s_nv_magic    = PREV_BOOT_MAGIC;
    s_nv_mcusr    = g_reset_mcusr;
    s_nv_uptime   = 0;
}
#endif

#ifdef ENCODER_ENABLE
static void wheel_task(void); // below encoder_update_kb
#endif

void housekeeping_task_user(void) {
#ifdef ENCODER_ENABLE
    wheel_task();
#endif

    // No reset_reason_task() on the mouse: its one-shot line is superseded by the
    // periodic one below, and dropping it frees the flash that line needs. MCUSR
    // bits on the atmega16u2: POR 0x01, EXT 0x02, BOD 0x04, WDT 0x08, USB 0x20.
#ifdef CONSOLE_ENABLE
    // Updated every pass, and the AVR wakes at least every WDT period, so the
    // record a warm reset leaves behind is short of the true run time by at most
    // one nap (~8 s).
    s_nv_uptime = timer_read32();

    // reset_reason_task() prints once and relies on console_buffer.c to hold the
    // line until a listener attaches. The mouse can't afford that buffer: it
    // defaults to 512 B, which is all of the atmega16u2's SRAM, and static RAM is
    // already ~345 B. So re-emit every few seconds instead.
    //
    // "prev" is the boot before this one (ok=0: no valid record, i.e. this boot
    // followed a power-off). Its "ran>=" against wall-clock time since switching
    // on shows whether that boot's firmware started at power-up or late. Sleep is
    // credited to the timer via the WDT, so uptimes are good to ~5%.
    static uint16_t s_last_mcusr_print;
    if (timer_elapsed(s_last_mcusr_print) >= 3000) {
        s_last_mcusr_print = timer_read();
        uprintf("MCUSR=0x%02X up=%lums prev=0x%02X ran>=%lums ok=%u\n", g_reset_mcusr, (unsigned long)s_nv_uptime, s_prev_mcusr, (unsigned long)s_prev_uptime, s_prev_valid);
#    ifdef POINTING_DEVICE_HIRES_SCROLL_ENABLE
        // The host sets the multiplier at enumeration, before any listener can attach,
        // so it is restated here rather than logged when it lands.
        uprintf("hires: multiplier %u, usb %u\n", usb_device_state_get_hires_scroll(), connection_get_host() == CONNECTION_HOST_USB);
#    endif
    }
#endif
}

// Encoder → mouse wheel, written straight into the pointing-device report.
// We override encoder_update_kb (not _user) on purpose: the weak default only
// emits QK_MOUSE_WHEEL_* under MOUSEKEY_ENABLE (else KC_PGUP/PGDN keyboard taps),
// and MOUSEKEY is off here to fit CONSOLE_ENABLE on the atmega16u2. Replacing the
// default also drops its tap_code(KC_PGUP/PGDN) machinery from the build, which
// matters on this nearly-full 16u2. Populate report->v directly — no mousekey.
//
// Accelerated by the time since the previous notch (see WHEEL_ACCEL_* in
// config.h). The gain is kept in 1/16 units and the remainder carried into the
// next notch, so a steady spin between two whole gains averages out rather than
// snapping to the lower one. A slow notch or a reversal starts clean at 1.
//
// With POINTING_DEVICE_HIRES_SCROLL_ENABLE the same curve also comes out in 1/120
// notch. That is fine enough to need no remainder, and it lets the slow end keep
// falling as MULTIPLIER * SLOW_MS / interval below one notch, to WHEEL_HIRES_MIN.
// Over USB it goes out only while the host has the Resolution Multiplier on. Over BLE
// and GZLL both units ride in the blob, and the nRF or the dongle picks one for its
// own host (transfer_blob.h).
//
// Contact bounce. A detent is one edge on one of the two contacts (ENCODER_RESOLUTION
// 1), and QMK's quadrature driver doesn't debounce, so a contact that chatters as it
// crosses its edge reads as a step, a step back, and the step again. The host scrolls
// down, up and down, which at the end of a page shows as the page bouncing off it
// (2026-10-29). So a step against the last one sent, within WHEEL_REVERSAL_HOLD_MS of
// it, is held rather than sent, and the bounce's own return step cancels it. A held
// step that nothing cancels is a real reversal, and wheel_task() sends it when the
// window closes.
#ifdef POINTING_DEVICE_HIRES_SCROLL_ENABLE
#    if POINTING_DEVICE_HIRES_SCROLL_MULTIPLIER * WHEEL_ACCEL_SLOW_MS > 65535
#        error "MULTIPLIER * WHEEL_ACCEL_SLOW_MS must fit uint16_t"
#    endif
#    define WHEEL_HIRES_K ((uint16_t)POINTING_DEVICE_HIRES_SCROLL_MULTIPLIER * WHEEL_ACCEL_SLOW_MS)
// Past this interval the curve would drop below WHEEL_HIRES_MIN.
#    define WHEEL_HIRES_FLOOR_MS (POINTING_DEVICE_HIRES_SCROLL_MULTIPLIER * WHEEL_ACCEL_SLOW_MS / WHEEL_HIRES_MIN)

// bluetooth_custom.c: the hi-res wheel for the next report's whole notches.
void bluetooth_set_wheel_hires(int16_t v_hr);
#endif

static uint16_t s_wheel_last;      // when the last step was sent
static bool     s_wheel_clockwise; // and which way it went
static uint8_t  s_wheel_held;      // steps against it since then, held as bounce

static void wheel_send(bool clockwise) {
    static uint8_t frac;

    uint16_t dt  = timer_elapsed(s_wheel_last);
    s_wheel_last = timer_read();

    bool reversed     = clockwise != s_wheel_clockwise;
    s_wheel_clockwise = clockwise;
    s_wheel_held      = 0;

    uint8_t gain = 16;
    if (dt >= WHEEL_ACCEL_SLOW_MS || reversed) {
        frac = 0;
    } else if (dt * WHEEL_ACCEL_MAX <= WHEEL_ACCEL_SLOW_MS) {
        gain = WHEEL_ACCEL_MAX * 16;
    } else {
        gain = WHEEL_ACCEL_SLOW_MS * 16 / dt;
    }

    frac += gain;
    mouse_hv_report_t units = frac >> 4;
    frac &= 15;

#ifdef POINTING_DEVICE_HIRES_SCROLL_ENABLE
    mouse_hv_report_t hires;
    if (reversed || dt >= WHEEL_HIRES_FLOOR_MS) {
        hires = WHEEL_HIRES_MIN;
    } else if (dt * WHEEL_ACCEL_MAX <= WHEEL_ACCEL_SLOW_MS) {
        hires = POINTING_DEVICE_HIRES_SCROLL_MULTIPLIER * WHEEL_ACCEL_MAX;
    } else {
        hires = WHEEL_HIRES_K / dt;
    }

    if (connection_get_host() == CONNECTION_HOST_USB) {
        if (usb_device_state_get_hires_scroll()) {
            units = hires;
        }
    } else {
        bluetooth_set_wheel_hires(clockwise ? hires : -hires);
    }
    dprintf("wheel: dt %u units %d hires %d\n", dt, units, hires);
#else
    dprintf("wheel: dt %u units %d\n", dt, units);
#endif

    report_mouse_t report = pointing_device_get_report();
    report.v = clockwise ? units : -units;
    pointing_device_set_report(report);
    pointing_device_send();
}

bool encoder_update_kb(uint8_t index, bool clockwise) {
    if (clockwise == s_wheel_clockwise) {
        if (s_wheel_held) {
            s_wheel_held--; // back over an edge that was already sent
            return true;
        }
    } else if (timer_elapsed(s_wheel_last) < WHEEL_REVERSAL_HOLD_MS) {
        s_wheel_held++;
        return true;
    }
    wheel_send(clockwise);
    return true;
}

// Sends a held step once its window has closed with nothing to cancel it. Only one:
// more than one held means the contact was still chattering, and sent together they
// would read as a fast spin.
static void wheel_task(void) {
    if (s_wheel_held && timer_elapsed(s_wheel_last) >= WHEEL_REVERSAL_HOLD_MS) {
        wheel_send(!s_wheel_clockwise);
    }
}
