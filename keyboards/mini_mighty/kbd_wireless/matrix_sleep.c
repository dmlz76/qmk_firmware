#include "matrix_sleep.h"
#include "gpio.h"
#include "quantum.h"
#include "timer.h"
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/io.h>

volatile bool matrix_wake_flag = false;

#if MMKW_VER >= 2
// Wake-on-keypress for the rev2 kbd_wireless (atmega32u4).
//
// SLEEP_MODE_PWR_DOWN stops the I/O clock, so only asynchronous sources wake the
// MCU: INTx configured for LOW LEVEL, and PCINT edges. We sense keypresses on the
// six ROW lines, which are the only wake-capable pins left after SPI to the nRF
// claims PB0..PB3 (PCINT0..3):
//
//   Row_0 = B6 = PCINT6      Row_3 = D0 = INT0
//   Row_1 = B5 = PCINT5      Row_4 = D1 = INT1
//   Row_2 = B4 = PCINT4      Row_5 = B7 = PCINT7
//
// arm() drives every column LOW and pulls every row UP, so a pressed key pulls its
// row low -> PCINT edge / INT low level -> wake. This REQUIRES the matrix diodes to
// be ROW2COL (anode on the row line): current must flow row->col for the pressed row
// to be pulled toward the driven-low column. With COL2ROW diodes the row can never be
// pulled low and no key would wake the MCU.
//
// INT0/INT1 are level triggered, so their ISRs self-disable to stop the interrupt
// re-firing while the key is held low; arm() re-enables them before the next sleep.

#define TIMER_INCR_ON_INTR 1

ISR(INT0_vect) {
    EIMSK &= ~_BV(INT0);
    matrix_wake_flag = true;
}

ISR(INT1_vect) {
    EIMSK &= ~_BV(INT1);
    matrix_wake_flag = true;
}

ISR(PCINT0_vect) {
    matrix_wake_flag = true;
    timer_count += TIMER_INCR_ON_INTR;
}
#endif // MMKW_VER >= 2

void matrix_sleep_arm(void) {
    matrix_wake_flag = false;

#if MMKW_VER >= 2
    static const pin_t row_pins[] = MATRIX_ROW_PINS;
    static const pin_t col_pins[] = MATRIX_COL_PINS;

    // Rows: input + pull-up (idle high, pulled low by a keypress).
    for (uint8_t i = 0; i < ARRAY_SIZE(row_pins); i++) {
        gpio_set_pin_input_high(row_pins[i]);
    }
    // Columns: drive all low so any key shorts a row to a low sink.
    for (uint8_t i = 0; i < ARRAY_SIZE(col_pins); i++) {
        gpio_set_pin_output(col_pins[i]);
        gpio_write_pin_low(col_pins[i]);
    }

    // INT0 (D0/Row_3) + INT1 (D1/Row_4): LOW LEVEL (ISCx1:ISCx0 = 00) — the only
    // INTx mode that wakes from power-down.
    EICRA &= ~(_BV(ISC01) | _BV(ISC00) | _BV(ISC11) | _BV(ISC10));
    EIFR   = _BV(INTF0) | _BV(INTF1);   // clear any stale flags
    EIMSK |= _BV(INT0) | _BV(INT1);

    // PCINT4..7 (B4/B5/B6/B7 = Row_2/Row_1/Row_0/Row_5).
    PCMSK0 = _BV(PCINT4) | _BV(PCINT5) | _BV(PCINT6) | _BV(PCINT7);
    PCIFR |= _BV(PCIF0);                // clear any stale flag
    PCICR |= _BV(PCIE0);
#endif
}

void matrix_sleep_disarm(void) {
#if MMKW_VER >= 2
    EIMSK  &= ~(_BV(INT0) | _BV(INT1));
    PCMSK0  = 0;
    PCICR  &= ~_BV(PCIE0);

    // Release the pins to a neutral, self-contained state so nothing is left
    // actively driven if matrix_scan() doesn't immediately run (defensive; today
    // ROW2COL scan's unselect_col() would reset the columns on the first scan).
    // Deliberately NOT reconstructing QMK's exact resting convention (hi-Z vs
    // pull-up vs MATRIX_UNSELECT_DRIVE_HIGH) — that would re-couple us to matrix.c
    // internals. Input+pull-up drives nothing and matrix_scan reconfigures from
    // here regardless.
    static const pin_t row_pins[] = MATRIX_ROW_PINS;
    static const pin_t col_pins[] = MATRIX_COL_PINS;
    for (uint8_t i = 0; i < ARRAY_SIZE(col_pins); i++) {
        gpio_set_pin_input_high(col_pins[i]);
    }
    for (uint8_t i = 0; i < ARRAY_SIZE(row_pins); i++) {
        gpio_set_pin_input_high(row_pins[i]);
    }
#endif
}
