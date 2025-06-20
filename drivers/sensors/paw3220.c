/* Copyright 2025 Dimitar Lazarov (dimitrix.llc)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "paw3220.h"
#include "wait.h"
#include "debug.h"
#include "gpio.h"
#include "pointing_device_internal.h"

#define USE_LED_CURRENT_SOURCE_MODE 1

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
    wait_us(1);
    paw3220_deselect();
    wait_us(1);

    paw3220_write_reg(REG_CONFIG, 0x80); // full reset
    wait_us(5);

#if USE_LED_CURRENT_SOURCE_MODE
    paw3220_write_reg(REG_WRITE_PROTECT, 0x5A);
    paw3220_write_reg(REG_LED_OPTION, 0x014); // 4 mA
    paw3220_write_reg(REG_WRITE_PROTECT, 0x0);
#endif

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
