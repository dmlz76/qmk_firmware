extern "C" {
#include "ble_send_buf.h"
#include "matrix_sleep.h"
#include "spi_master.h"
#include "gpio.h"
#include "wait.h"
#include "debug.h"
#include "timer.h"
#include "suspend.h"
#include <avr/wdt.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
#include "lufa.h"
#ifdef POINTING_DEVICE_ENABLE
#    include "pointing_device.h"
#endif
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
#    define POWER_SAVE_TIMEOUT_MS 5000 // 5 sec
#    define BLE_OFF_TIMEOUT_MS 300000  // 5 min
#    ifndef MCU_POWER_DOWN_WDTO
#        define MCU_POWER_DOWN_WDTO WDTO_15MS
#    endif

#    define POWER_SAVE_STATE_MCU 1
#    define POWER_SAVE_STATE_BLE 2

static uint32_t s_last_processed_blob_time = 0;
static uint8_t  s_power_save_state         = 0;
#endif

static RingBuffer<transfer_blob_t, 20> s_send_buf;

#define BLE_STATE_UNKNOWN 0
#define BLE_STATE_OFF -1
#define BLE_STATE_ON 1

static uint8_t s_resetPin = 0;
static uint8_t s_sleepPin = 0;
static int s_ble_state = BLE_STATE_UNKNOWN;

static void ble_turn_on() {
    if (s_ble_state == BLE_STATE_ON) {
        return;
    }

    gpio_write_pin_high(s_sleepPin);

    gpio_write_pin_high(s_resetPin);
    gpio_write_pin_low(s_resetPin);
    wait_ms(100);
    gpio_write_pin_high(s_resetPin);
    wait_ms(1000); // Give it a second to initialize

    s_ble_state = BLE_STATE_ON;

#ifdef CONSOLE_ENABLE
    uprintf("ble_turn_on\n");
#endif
}

static void ble_turn_off() {
    if (s_ble_state == BLE_STATE_OFF) {
        return;
    }

    gpio_write_pin_low(s_sleepPin);

    s_ble_state = BLE_STATE_OFF;

#ifdef CONSOLE_ENABLE
    uprintf("ble_turn_off\n");
#endif
}

#if defined(WDT_vect)

// clang-format off
#define wdt_intr_enable(value) \
__asm__ __volatile__ ( \
    "in __tmp_reg__,__SREG__" "\n\t" \
    "cli" "\n\t" \
    "wdr" "\n\t" \
    "sts %0,%1" "\n\t" \
    "out __SREG__,__tmp_reg__" "\n\t" \
    "sts %0,%2" "\n\t" \
    : /* no outputs */ \
    : "M" (_SFR_MEM_ADDR(_WD_CONTROL_REG)), \
    "r" (_BV(_WD_CHANGE_BIT) | _BV(WDE)), \
    "r" ((uint8_t) ((value & 0x08 ? _WD_PS3_MASK : 0x00) | _BV(WDIE) | (value & 0x07))) \
    : "r0" \
)
// clang-format on

/** \brief Power down MCU with watchdog timer
 *
 * wdto: watchdog timer timeout defined in <avr/wdt.h>
 *          WDTO_15MS
 *          WDTO_30MS
 *          WDTO_60MS
 *          WDTO_120MS
 *          WDTO_250MS
 *          WDTO_500MS
 *          WDTO_1S
 *          WDTO_2S
 *          WDTO_4S
 *          WDTO_8S
 */
static uint8_t wdt_timeout = 0;

/** \brief Power down
 *
 * FIXME: needs doc
 */
static void power_down(uint8_t wdto) {
    wdt_timeout = wdto;

    cli();

    // Watchdog Interrupt Mode
    wdt_intr_enable(wdto);

    // Drive cols low / pull rows up and arm async-wake IRQs so any keypress
    // wakes the MCU regardless of WDT period.
    matrix_sleep_arm();

    // TODO: more power saving
    // See PicoPower application note
    // - I/O port input with pullup
    // - prescale clock
    // - BOD disable
    // - Power Reduction Register PRR
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    //    cli();
    sleep_enable();
    //    sleep_bod_disable();
    // turn off brown-out enable in software
    //    MCUCR = bit (BODS) | bit (BODSE);
    //    MCUCR = bit (BODS);
    sei();
    sleep_cpu();
    sleep_disable();

    matrix_sleep_disarm();

    // Disable watchdog after sleep
    wdt_disable();
}

uint16_t timer_count_from_wdt_timeout(uint8_t wdto) {
    uint16_t tc = 0;
    switch (wdto) {
        case WDTO_15MS:
            tc = 15 + 2; // WDTO_15MS + 2(from observation)
            break;
        case WDTO_30MS:
            tc = 30 + 2;
            break;
        case WDTO_60MS:
            tc = 60 + 2;
            break;
        case WDTO_120MS:
            tc = 120 + 2;
            break;
        case WDTO_1S:
            tc = 1000 + 2;
            break;
        default:;
    }
    return tc;
}

/* watchdog timeout */
ISR(WDT_vect) {
    // compensate timer for sleep
    timer_count += timer_count_from_wdt_timeout(wdt_timeout);
}
#endif // #if defined(WDT_vect)

#if ENABLE_POWER_SAVINGS
static void mcu_power_down() {
    if (USB_DeviceState == DEVICE_STATE_Configured) {
        return;
    }

#ifdef CONSOLE_ENABLE
    uprintf("mcu_power_down\n");
#endif

    suspend_power_down_quantum();

#if defined(POINTING_DEVICE_ENABLE)
    // run to ensure scanning occurs while suspended
    pointing_device_task();
#endif

    // Enter sleep state if possible (ie, the MCU has a watchdog timeout interrupt)
#if defined(WDT_vect)
    power_down(MCU_POWER_DOWN_WDTO);
#    endif

    if (matrix_wake_flag) {
        //  prevent the MCU from sleeping right away
        s_power_save_state &= ~POWER_SAVE_STATE_MCU;
        s_last_processed_blob_time = timer_read32();
    }
}
#endif

#if ENABLE_POWER_SAVINGS
static void mcu_wake_up() {
    suspend_wakeup_init();

    if (matrix_wake_flag) {
#    ifdef CONSOLE_ENABLE
        uprintf("mcu_wake_up matrix\n");
#    endif
    } else {
#    ifdef CONSOLE_ENABLE
        uprintf("mcu_wake_up watchdog\n");
#    endif
    }
}
#endif

static bool process_blob(const transfer_blob_t &blob, uint16_t timeout) {
    bool spi_started = spi_start(SPI_SS_PIN, LSBFIRST, SPI_MODE, SCK_DIVISOR);
    if (!spi_started) {
#ifdef CONSOLE_ENABLE
        uprintf("SPI start failed\n");
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
        uprintf("SPI transmission failed. Retrying.\n");
#endif
        spi_stop();
        wait_us(BackOff);
        spi_start(SPI_SS_PIN, LSBFIRST, SPI_MODE, SCK_DIVISOR);
        wait_us(PostSelectWait);
    } while (timer_elapsed(timerStart) < timeout);

    spi_stop();
    return success;
}

void setup_power_savings() {
    // Disable analog comparator (not used, saves ~70 µA).
    ACSR &= ~_BV(ACIE);
    ACSR |= _BV(ACD);

    // Power Reduction Register — clock-gate unused peripherals.
    // Timer0 (system tick) and SPI (BLE) must stay enabled.
#if defined(PRR0)
    PRR0 |= _BV(PRUSART1) | _BV(PRTIM1);
#elif defined(PRR)
    PRR |= _BV(PRUSART1) | _BV(PRTIM1);
#endif

#ifdef UNCONNECTED_PINS
    // Pull up unused pins to prevent floating inputs drawing current.
    const pin_t unconnected_pins[] = UNCONNECTED_PINS;
    for (uint8_t i = 0; i < (sizeof(unconnected_pins) / sizeof(pin_t)); i++) {
        gpio_set_pin_input_high(unconnected_pins[i]);
    }
#endif
}

void send_buf_init(uint8_t resetPin, uint8_t sleepPin) {
    s_resetPin = resetPin;
    s_sleepPin = sleepPin;

    gpio_set_pin_output(s_resetPin);
    gpio_set_pin_output(s_sleepPin);

    setup_power_savings();

    spi_init();

    ble_turn_on();
}

void power_savings_on() {
#if ENABLE_POWER_SAVINGS
    uint32_t time_diff = timer_elapsed32(s_last_processed_blob_time);
    if (time_diff > POWER_SAVE_TIMEOUT_MS) {
        s_power_save_state |= POWER_SAVE_STATE_MCU;
    }
    if (time_diff > BLE_OFF_TIMEOUT_MS) {
        s_power_save_state |= POWER_SAVE_STATE_BLE;
    }

    if (s_power_save_state & POWER_SAVE_STATE_BLE) {
        ble_turn_off();
    }
    if (s_power_save_state & POWER_SAVE_STATE_MCU) {
        mcu_power_down();
    }
#endif
}

void power_savings_off() {
#if ENABLE_POWER_SAVINGS
    if (s_power_save_state & POWER_SAVE_STATE_MCU) {
        mcu_wake_up();
    }
    if (s_power_save_state & POWER_SAVE_STATE_BLE) {
        ble_turn_on();
    }
    s_power_save_state = 0;
#endif
}

bool send_buf_send_one(uint16_t timeout) {
    transfer_blob_t blob;
    if (!s_send_buf.peek(blob)) {
        power_savings_on();
        return false;
    }

    power_savings_off();

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
