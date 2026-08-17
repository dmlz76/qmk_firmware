// Copyright 2025 Dimitrix LLC
// SPDX-License-Identifier: GPL-2.0-or-later

#include "paw3220.h"
#include "wait.h"
#include "debug.h"
#include "gpio.h"
#include "progmem.h"
#include "pointing_device_internal.h"

#define MANUAL_POWER_ON_RESET 0

#define REG_PID1 0x00
#define REG_PID2 0x01

#define REG_MSTAT 0x02
#define REG_DX 0x03
#define REG_DY 0x04

#define REG_CONFIG 0x06

#define REG_WRITE_PROTECT 0x09

#define REG_CPI_X 0x0D
#define REG_CPI_Y 0x0E

#define REG_MOUSE_OPTION 0x19
#define REG_SPI_MODE 0x26
#define REG_LED_OPTION 0x5C

// Datasheet §8.1.1.1 — the initialization sequence for 3-wire SPI / High Voltage
// Segment (VDD = 2.1 to 3.6 V), which the datasheet calls "necessary ... to ensure
// the correct operations and the best tracking performance". Everything here lives
// above address 0x10, so the whole block runs between the two Write_Protect writes.
//
// Two entries matter for power specifically:
//   0x4B = 0x00 selects the High Voltage Segment. §5.1.2 warns that if this register
//          "is not set properly, the chip would consume extra power due to the
//          current leakage of the internal regulator" — it powers up correct for
//          this segment, but the vendor sequence writes it anyway, so we do too.
//   0x5C = 0xD4 is current-source mode at 4 mA. Current-source is not optional on
//          this hardware: D3's anode ties straight to VCC with no series resistor
//          (mini_mighty_mouse.kicad_sch), so current-switch mode — the power-on
//          default — has nothing limiting ILED. Note 0xD4, not 0x14: bits [7:6] are
//          Reserved and power up as 1, and only bits [5:4] (mode) and [3:0]
//          (ILED = n × 1 mA) are ours to set.
// 0x42-0x4A / 0x64 / 0x79 are undocumented tuning registers in bank 1, hence the
// 0x7F bank select around them. Values are transcribed from the datasheet verbatim.
static const uint8_t PROGMEM paw3220_init_seq[] = {
    REG_WRITE_PROTECT, 0x5A, // disable write protect
    0x4B,              0x00, // High Voltage Segment (VDD = 2.1 to 3.6 V)
    REG_LED_OPTION,    0xD4, // current source mode, 4 mA
    REG_CPI_X,         0x1A,
    REG_CPI_Y,         0x1C,
    0x7F,              0x01, // select bank 1
    0x42,              0x4F,
    0x43,              0x93,
    0x44,              0x48,
    0x45,              0xF2,
    0x47,              0x4F,
    0x48,              0x93,
    0x49,              0x48,
    0x4A,              0xF3,
    0x64,              0x66,
    0x79,              0x08,
    0x7F,              0x00, // back to bank 0
    REG_WRITE_PROTECT, 0x00, // enable write protect
};

const pointing_device_driver_t paw3220_pointing_device_driver = {
    .init       = paw3220_init,
    .get_report = paw3220_get_report,
    .set_cpi    = paw3220_set_cpi,
    .get_cpi    = paw3220_get_cpi,
};

static void paw3220_select(void) {
    gpio_write_pin_low(PAW3220_CS_PIN);
}

static void paw3220_deselect(void) {
    gpio_write_pin_high(PAW3220_CS_PIN);
}

static uint8_t paw3220_serial_read(void) {
    gpio_set_pin_input(PAW3220_SDIO_PIN);
    uint8_t byte = 0;

    for (uint8_t i = 0; i < 8; ++i) {
        gpio_write_pin_low(PAW3220_SCLK_PIN);
        wait_us(1);

        byte = (byte << 1) | gpio_read_pin(PAW3220_SDIO_PIN);

        gpio_write_pin_high(PAW3220_SCLK_PIN);
        wait_us(1);
    }

    return byte;
}

static void paw3220_serial_write(uint8_t data) {
    gpio_write_pin_low(PAW3220_SDIO_PIN);
    gpio_set_pin_output(PAW3220_SDIO_PIN);

    for (int8_t b = 7; b >= 0; b--) {
        gpio_write_pin_low(PAW3220_SCLK_PIN);
        if (data & (1 << b)) {
            gpio_write_pin_high(PAW3220_SDIO_PIN);
        } else {
            gpio_write_pin_low(PAW3220_SDIO_PIN);
        }
        gpio_write_pin_high(PAW3220_SCLK_PIN);
    }

    wait_us(4);
}


static void paw3220_write_reg(uint8_t reg_addr, uint8_t data) {
    paw3220_select();
    paw3220_serial_write(0b10000000 | reg_addr);
    paw3220_serial_write(data);
    paw3220_deselect();
}

static uint8_t paw3220_read_reg(uint8_t reg_addr) {
    paw3220_select();
    paw3220_serial_write(reg_addr);
    wait_us(5);
    uint8_t byte = paw3220_serial_read();
    wait_us(1);
    paw3220_deselect();

    // Park SDIO at a defined level instead of leaving it floating. paw3220_serial_read()
    // ends with gpio_set_pin_input(), which on AVR clears PORTxn as well as DDRxn — so
    // between transactions the net floats at both ends, and on a battery board that
    // gap is the entire MCU nap (seconds, not microseconds). Our own input buffer is
    // clamped while asleep (ATmega8U2/16U2/32U2 doc7799 §12.2.5), but the sensor's is
    // not, and a CMOS input sitting near VCC/2 draws through-current the whole time.
    // Safe to drive: the sensor puts SDIO in high-Z whenever NCS is high (§6), which
    // paw3220_deselect() has just done. Write the level before switching direction so
    // no stale PORTxn value can glitch the line high on the way out.
    gpio_write_pin_low(PAW3220_SDIO_PIN);
    gpio_set_pin_output(PAW3220_SDIO_PIN);

    return byte;
}


report_paw3220_t paw3220_read(void) {
    report_paw3220_t data = {0};

    data.isMotion = paw3220_read_reg(REG_MSTAT) & (1 << 7); // check for motion only (bit 7 in field)
    data.x        = (int8_t)paw3220_read_reg(REG_DX);
    data.y        = (int8_t)paw3220_read_reg(REG_DY);

    return data;
}

void paw3220_init(void) {
    gpio_set_pin_output(PAW3220_SCLK_PIN);
    gpio_set_pin_output(PAW3220_SDIO_PIN);
    gpio_set_pin_output(PAW3220_CS_PIN);

    // Hold CS low for 1ms during boot
    paw3220_select();
    wait_ms(1);
    paw3220_deselect();
    wait_us(1);

#if MANUAL_POWER_ON_RESET
    paw3220_write_reg(REG_CONFIG, 0x80); // full reset
    wait_us(100);
#endif

    for (uint8_t i = 0; i < sizeof(paw3220_init_seq); i += 2) {
        paw3220_write_reg(pgm_read_byte(&paw3220_init_seq[i]), pgm_read_byte(&paw3220_init_seq[i + 1]));
    }

    // Enable Sleep3, the deepest of the three automatic power-saving modes (8 µA vs
    // 30 µA in Sleep1), which is the one mode disabled by default. 0x31, not 0x20:
    // Configuration powers up at 0x11 and bits 4 and 0 are Reserved — a bare 0x20
    // sets Slp3_Enh but also writes zeros over both of them. Address 0x06 is below
    // 0x10, so Write_Protect does not apply and this can sit outside the sequence.
    paw3220_write_reg(REG_CONFIG, 0x11 | 0x20);

#ifdef POINTING_DEVICE_DEBUG
    uint8_t pid1 = paw3220_read_reg(REG_PID1);
    uint8_t pid2 = paw3220_read_reg(REG_PID2);
    pd_dprintf("PID1: 0x%02X, PID2: 0x%02X\n", pid1, pid2);

    uint8_t led_option = paw3220_read_reg(REG_LED_OPTION);
    pd_dprintf("LED OPTION: 0x%02X\n", led_option);
#endif
}


report_mouse_t paw3220_get_report(report_mouse_t mouse_report) {
    report_paw3220_t data = paw3220_read();
    if (data.isMotion) {
        pd_dprintf("Raw X: %d, Y: %d\n", data.x, data.y);

        mouse_report.x = data.x;
        mouse_report.y = data.y;
    }

    return mouse_report;
}

void paw3220_set_cpi(uint16_t cpi) {
    uint8_t cpival_x = cpi / 40;
    uint8_t cpival_y = cpi / 37;
    paw3220_write_reg(REG_CPI_X, cpival_x);
    paw3220_write_reg(REG_CPI_Y, cpival_y);
}

uint16_t paw3220_get_cpi(void) {
    uint16_t cpival_x = paw3220_read_reg(REG_CPI_X);
    return cpival_x * 40;
}
