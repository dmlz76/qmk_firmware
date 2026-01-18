// Copyright 2025 Dimitrix LLC
// SPDX-License-Identifier: GPL-2.0-or-later

extern "C" {
#include "bluetooth.h"
#include "connection.h"
#include "suspend.h"
}
#include "gpio.h"
#include "spi_master.h"
#include "wait.h"
#include "debug.h"
#include "ringbuffer.hpp"
#include "timer.h"
#include "transfer_blob.h"
#include <assert.h>

#define RST_PIN D4
#define SCK_DIVISOR 8 // 2MHz SCK/16MHz CPU

#define LSBFIRST false
#define SPI_MODE 0

#define PostSelectWait 250      /* microseconds */
#define Timeout 150             /* milliseconds */
#define ShortTimeout 10         /* milliseconds */
#define BackOff 100             /* microseconds */

#define BLE_STATE_UNKNOWN 0
#define BLE_STATE_OFF -1
#define BLE_STATE_ON 1

#define ENABLE_POWER_SAVINGS 1

#if ENABLE_POWER_SAVINGS
#    define POWER_SAVE_TIMEOUT_MS 10000 // 10 sec
static uint32_t last_processed_blob_time = 0;
static uint8_t  power_save_level         = 0;
#endif

static int                             ble_state = BLE_STATE_UNKNOWN;
static RingBuffer<transfer_blob_t, 40> send_buf;

static void ble_turn_on() {
    if (ble_state == BLE_STATE_ON) {
        return;
    }

    gpio_write_pin_high(RST_PIN);
    wait_ms(1000); // Give it a second to initialize

    ble_state = BLE_STATE_ON;
}

static void ble_turn_off() {
    if (ble_state == BLE_STATE_OFF) {
        return;
    }

    gpio_write_pin_low(RST_PIN);
    wait_ms(10);

    ble_state = BLE_STATE_OFF;
}

static bool process_blob(const transfer_blob_t &blob, uint16_t timeout) 
{
    bool spi_started = spi_start(SPI_SS_PIN, LSBFIRST, SPI_MODE, SCK_DIVISOR);
    if (!spi_started) {
#ifdef CONSOLE_ENABLE
        dprintf("SPI start failed\n");
#endif
        return false;
    }
    wait_us(PostSelectWait);

    uint16_t timerStart = timer_read();
    bool success = false;
    do {

        spi_status_t type_status = spi_transmit(blob.raw, sizeof(blob.raw));
        success = (type_status == SPI_STATUS_SUCCESS);
        if (success) {
            break;
        }

#ifdef CONSOLE_ENABLE
        dprintf("SPI transmission failed. Retrying.\n");
#endif
        spi_stop();
        wait_us(BackOff);
        spi_start(SPI_SS_PIN, LSBFIRST, SPI_MODE, SCK_DIVISOR);
        wait_us(PostSelectWait);
    } while (timer_elapsed(timerStart) < timeout);

    spi_stop();
    return success;
}

static bool send_buf_send_one(uint16_t timeout = Timeout) 
{
    transfer_blob_t blob;
    if (!send_buf.peek(blob)) {
#if ENABLE_POWER_SAVINGS
        // Power saving
        uint32_t time_diff = timer_elapsed32(last_processed_blob_time);
        if (time_diff > POWER_SAVE_TIMEOUT_MS) {
            power_save_level = 1;
            suspend_power_down();
        }
#endif
        return false;
    }

#if ENABLE_POWER_SAVINGS
    if (power_save_level) {
        power_save_level = 0;
        suspend_wakeup_init();
    }
#endif

    if (process_blob(blob, timeout)) {
        // commit that peek
        send_buf.get(blob);
#if ENABLE_POWER_SAVINGS
        last_processed_blob_time = timer_read32();
#endif
        return true;
    } 

    wait_ms(timeout);
    return false;
}

extern "C" void bluetooth_init(void) {
    spi_init();

    gpio_set_pin_output(RST_PIN);
}

extern "C" void bluetooth_task(void) {
    connection_host_t connection = connection_get_host();
    if (connection == CONNECTION_HOST_BLUETOOTH) {
        ble_turn_on();
        send_buf_send_one(ShortTimeout);
    } else {
        ble_turn_off();
        // clear out any remaining blobs
        transfer_blob_t blob;
        while (send_buf.get(blob)) {
        }
    }
}

extern "C" void bluetooth_send_keyboard(report_keyboard_t *report)
{
    static_assert(KEYBOARD_REPORT_KEYS == 6, "Expected 6 keys in keyboard report");
#ifdef CONSOLE_ENABLE
    if (debug_mouse) {
        dprintf("bluetooth_send_keyboard: mods %d, keys [%d,%d,%d,%d,%d,%d]\n", report->mods, report->keys[0], report->keys[1], report->keys[2], report->keys[3], report->keys[4], report->keys[5]);
    }
#endif
}

extern "C" void bluetooth_send_mouse(report_mouse_t *report)
{
#ifdef CONSOLE_ENABLE
    if (debug_mouse) {
        dprintf("bluetooth_send_mouse: buttons %02X, x %d, y %d, v %d, h %d\n", report->buttons, report->x, report->y, report->v, report->h);
    }
#endif

    transfer_blob_t blob;
    blob.buttons = report->buttons;
    blob.x = report->x;
    blob.y = report->y;
    blob.v = report->v;

    while (!send_buf.enqueue(blob)) {
        send_buf_send_one();
    }
}
