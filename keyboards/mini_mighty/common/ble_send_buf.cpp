extern "C" {
#include "ble_send_buf.h"
#include "spi_master.h"
#include "gpio.h"
#include "wait.h"
#include "debug.h"
#include "timer.h"
#include "suspend.h"
}
#include "ringbuffer.hpp"
#include <assert.h>

#define LSBFIRST false
#define SPI_MODE 0
#define SCK_DIVISOR 8 // 2MHz SCK/16MHz CPU

#define PostSelectWait 250      /* microseconds */
#define BackOff 100             /* microseconds */

#define ENABLE_POWER_SAVINGS 1

#if ENABLE_POWER_SAVINGS
#    define POWER_SAVE_TIMEOUT_MS 10000 // 10 sec
#    define BLE_OFF_TIMEOUT_MS 600000   // 10 min
static uint32_t s_last_processed_blob_time = 0;
static uint8_t  s_power_save_level         = 0;
#endif

static RingBuffer<transfer_blob_t, 40> s_send_buf;

#define BLE_STATE_UNKNOWN 0
#define BLE_STATE_OFF -1
#define BLE_STATE_ON 1

static uint8_t s_resetPin = 0;
static uint8_t s_sleepPin = 0;
static int s_ble_state = BLE_STATE_UNKNOWN;

void ble_turn_on() {
    if (s_ble_state == BLE_STATE_ON) {
        return;
    }

    gpio_write_pin_high(s_sleepPin);

    gpio_write_pin_high(s_resetPin);
    gpio_write_pin_low(s_resetPin);
    wait_ms(10);
    gpio_write_pin_high(s_resetPin);
    wait_ms(1000); // Give it a second to initialize

    s_ble_state = BLE_STATE_ON;
}

void ble_turn_off() {
    if (s_ble_state == BLE_STATE_OFF) {
        return;
    }

    gpio_write_pin_low(s_sleepPin);

    s_ble_state = BLE_STATE_OFF;
}

static bool process_blob(const transfer_blob_t &blob, uint16_t timeout) {
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

void send_buf_init(uint8_t resetPin, uint8_t sleepPin) {
    s_resetPin = resetPin;
    s_sleepPin = sleepPin;

    gpio_set_pin_output(s_resetPin);
    gpio_set_pin_output(s_sleepPin);

    spi_init();

    ble_turn_on();
}

bool send_buf_send_one(uint16_t timeout) {
    transfer_blob_t blob;
    if (!s_send_buf.peek(blob)) {
#if ENABLE_POWER_SAVINGS
        // Power saving
        uint32_t time_diff = timer_elapsed32(s_last_processed_blob_time);
        if (time_diff > POWER_SAVE_TIMEOUT_MS) {
            if (s_power_save_level == 0) {
                s_power_save_level = 1;
            }
            if (time_diff > BLE_OFF_TIMEOUT_MS) {
                if (s_power_save_level == 1) {
                    s_power_save_level = 2;
                    ble_turn_off();
                }
            }
            suspend_power_down();
        }
#endif
        return false;
    }

#if ENABLE_POWER_SAVINGS
    if (s_power_save_level > 0) {
        suspend_wakeup_init();
        if (s_power_save_level == 2) {
            ble_turn_on();
        }
        s_power_save_level = 0;
    }
#endif

    if (process_blob(blob, timeout)) {
        // commit that peek
        s_send_buf.get(blob);
#if ENABLE_POWER_SAVINGS
        s_last_processed_blob_time = timer_read32();
#endif
        return true;
    } 

    wait_ms(timeout);
    return false;
}

bool send_buf_enqueue(const transfer_blob_t *blob) {
    assert(blob);
    return s_send_buf.enqueue(*blob);
}
