// Copyright 2025 Dimitrix LLC
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H
#include "reset_reason.h"
#include "connection.h"

#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/power.h>
#include <avr/interrupt.h>


// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
[0] = LAYOUT(
    KC_BTN1, KC_BTN3, KC_BTN2
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

void housekeeping_task_user(void) {
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
    }
#endif
}

// Encoder → mouse wheel, written straight into the pointing-device report.
// We override encoder_update_kb (not _user) on purpose: the weak default only
// emits QK_MOUSE_WHEEL_* under MOUSEKEY_ENABLE (else KC_PGUP/PGDN keyboard taps),
// and MOUSEKEY is off here to fit CONSOLE_ENABLE on the atmega16u2. Replacing the
// default also drops its tap_code(KC_PGUP/PGDN) machinery from the build, which
// matters on this nearly-full 16u2. Populate report->v directly — no mousekey.
bool encoder_update_kb(uint8_t index, bool clockwise) {
    report_mouse_t report = pointing_device_get_report();
    report.v = clockwise ? 1 : -1;
    pointing_device_set_report(report);
    pointing_device_send();
    return true;
}
