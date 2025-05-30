#include "bluetooth.h"
#include "gpio.h"
#include "spi_master.h"
#include "wait.h"
#include "debug.h"
#include <assert.h>

#define RST_PIN D4
#define IRQ_PIN D6
#define SCK_DIVISOR 8 // 2MHz SCK/16MHz CPU

#define LSBFIRST false
#define SPI_MODE 0

#define PostSelectWait 250      /* microseconds */
#define Timeout 150             /* milliseconds */
#define ShortTimeout 10         /* milliseconds */
#define BackOff 100             /* microseconds */

void bluetooth_init(void) 
{
    gpio_set_pin_input(IRQ_PIN);

    spi_init();

    // Perform a hardware reset
    gpio_set_pin_output(RST_PIN);
    gpio_write_pin_high(RST_PIN);
    gpio_write_pin_low(RST_PIN);
    wait_ms(10);
    gpio_write_pin_high(RST_PIN);

    wait_ms(1000); // Give it a second to initialize
}

void bluetooth_task(void) 
{  
    // This function would typically handle Bluetooth events and communication. 
}

typedef union
{
    struct
    {
        uint8_t buttons;
        int8_t x; // Horizontal movement
        int8_t y; // Vertical movement
        int8_t v; // Vertical scroll
    };    
    uint8_t raw[4]; 
} transfer_blob_t;


void bluetooth_send_keyboard(report_keyboard_t *report)
{
    static_assert(KEYBOARD_REPORT_KEYS == 6, "Expected 6 keys in keyboard report");
    dprintf("Keyboard report: mods %d, keys [%d,%d,%d,%d,%d,%d]\n", report->mods, report->keys[0], report->keys[1], report->keys[2], report->keys[3], report->keys[4], report->keys[5]);
}

void bluetooth_send_mouse(report_mouse_t *report)
{
    dprintf("Mouse report: buttons %02X, x %d, y %d, v %d, h %d\n", report->buttons, report->x, report->y, report->v, report->h);

    bool spi_started = spi_start(SPI_SS_PIN, LSBFIRST, SPI_MODE, SCK_DIVISOR);
    if (!spi_started) {
        dprintf("SPI start failed\n");
        return;
    }
    wait_us(PostSelectWait);

    transfer_blob_t blob;
    blob.buttons = report->buttons;
    blob.x = report->x;
    blob.y = report->y;
    blob.v = report->v;

    spi_status_t type_status = spi_transmit(blob.raw, sizeof(blob.raw));
    if (type_status != SPI_STATUS_SUCCESS) {
        dprintf("SPI transmission failed\n");
    }

    spi_stop();
}
