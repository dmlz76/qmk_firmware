#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "matrix.h"
#include "gpio.h"
#include "wait.h"
#include "debug.h"
#ifdef MOUSE_ENABLE
#include "report.h"
#include "host.h"
#endif

#define USE_SPI_IRQ (defined(__AVR__) || defined(__INTELLISENSE__))
#if USE_SPI_IRQ
#include <avr/interrupt.h>
#endif

#include "transfer_blob.h"

#define RST_PIN D4

#define CS_PIN B0
#define SCK_PIN B1
#define MOSI_PIN B2
#define MISO_PIN B3

#ifdef MOUSE_ENABLE
static report_mouse_t mouse_report = {};
#endif

#define SPI_READ_ONLY 1

#if USE_SPI_IRQ
#define DATA_BUFFER_COUNT 64
#define DATA_BUFFER_MASK (DATA_BUFFER_COUNT-1)
#define DATA_BUFFER_NEXT(x) ((x + 1) & DATA_BUFFER_MASK)

static volatile uint8_t g_spi_read = 0;
static volatile uint8_t g_spi_write = 0;
static volatile uint8_t g_spi_data[DATA_BUFFER_COUNT];
#endif

void spi_init_slave(void)
{
    // https://www.electronicwings.com/avr-atmega/atmega1632-spi

    gpio_set_pin_input(CS_PIN);
    gpio_set_pin_input(SCK_PIN);
    gpio_set_pin_input(MOSI_PIN);
#if SPI_READ_ONLY
    gpio_set_pin_input(MISO_PIN);
#else
    gpio_set_pin_output(MISO_PIN);
#endif

    // enable SPI
#if USE_SPI_IRQ
    SPCR = _BV(SPE) | _BV(SPIE);
    sei();
#else
    SPCR = _BV(SPE);
#endif
}

#if !USE_SPI_IRQ
static inline bool spi_selected(void) {
    return !gpio_read_pin(CS_PIN);
}
#endif

#if USE_SPI_IRQ
ISR (SPI_STC_vect)
{
#if !SPI_READ_ONLY
    SPDR = 0x0;
#endif
    g_spi_data[g_spi_write] = SPDR;
    g_spi_write = DATA_BUFFER_NEXT(g_spi_write);
}
bool spi_receive(uint8_t *data, uint8_t length) {
    uint8_t read = g_spi_read;
    for (uint8_t i = 0; i < length; i++) {
        if (read == g_spi_write) {
            return false;
        }
        data[i] = g_spi_data[read];
        read = DATA_BUFFER_NEXT(read);
    }
    g_spi_read = read;
    return true;
}
#else
uint8_t spi_read(void) {
#if !SPI_READ_ONLY
    SPDR = 0x0;
#endif
    while (!(SPSR & _BV(SPIF))) {
    }
    return SPDR;
}
void spi_receive(uint8_t *data, uint8_t length) {
    for (uint8_t i = 0; i < length; i++) {
        data[i] = spi_read();
    }
}
#endif

matrix_row_t matrix_get_row(uint8_t row) {
    return 0;
}

void matrix_print(void) {
}

void matrix_init(void) {
    spi_init_slave();

    // Perform a hardware reset
    gpio_set_pin_output(RST_PIN);
    gpio_write_pin_high(RST_PIN);
    gpio_write_pin_low(RST_PIN);
    wait_ms(10);
    gpio_write_pin_high(RST_PIN);

    wait_ms(1000); // Give it a second to initialize

    // This *must* be called for correct keyboard behavior
    matrix_init_kb();
}

uint8_t matrix_scan(void) {
    bool changed = false;

    transfer_blob_t blob;
#if USE_SPI_IRQ
    if (spi_receive(blob.raw, sizeof(blob.raw))) {
        dprintf("spi irq received: buttons %02X, x %d, y %d, v %d\n", blob.buttons, blob.x, blob.y, blob.v);  
        changed = true;
    }
#else
    if (spi_selected())
    {
        spi_receive(blob.raw, sizeof(blob.raw));
        dprintf("spi received: buttons %02X, x %d, y %d, v %d\n", blob.buttons, blob.x, blob.y, blob.v);  
        changed = true;
    }
#endif

#ifdef MOUSE_ENABLE
    if (changed) {
        mouse_report.buttons = blob.buttons;
        mouse_report.x = blob.x;
        mouse_report.y = blob.y;
        mouse_report.v = blob.v;
        host_mouse_send(&mouse_report);
    }
#endif

    // This *must* be called for correct keyboard behavior
    matrix_scan_kb();

    return changed;
}

__attribute__((weak)) void matrix_init_kb(void) { matrix_init_user(); }

__attribute__((weak)) void matrix_scan_kb(void) { matrix_scan_user(); }

__attribute__((weak)) void matrix_init_user(void) {}

__attribute__((weak)) void matrix_scan_user(void) {}
