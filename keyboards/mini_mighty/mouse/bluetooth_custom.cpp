extern "C" {
#include "bluetooth.h"
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

static RingBuffer<transfer_blob_t, 40> send_buf;

extern "C" void bluetooth_init(void) 
{
    spi_init();

    // Perform a hardware reset
    gpio_set_pin_output(RST_PIN);
    gpio_write_pin_high(RST_PIN);
    gpio_write_pin_low(RST_PIN);
    wait_ms(10);
    gpio_write_pin_high(RST_PIN);

    wait_ms(1000); // Give it a second to initialize
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
        return false;
    }
    if (process_blob(blob, timeout)) {
        // commit that peek
        send_buf.get(blob);
        return true;
    } 

    wait_ms(timeout);
    return false;
}

extern "C" void bluetooth_task(void) 
{  
    send_buf_send_one(ShortTimeout);
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
