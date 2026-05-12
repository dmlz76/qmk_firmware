#include "matrix_sleep.h"
#include "gpio.h"
#include "quantum.h"
#include "timer.h"
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/io.h>

volatile bool matrix_wake_flag = false;

#if MMM_VER >= 12
#define USE_INT7 0
#else
#define USE_INT7 1
#endif
#define USE_PCINT0 1
#define USE_PCINT1 1

#define TIMER_INCR_ON_INTR 1

#if USE_INT7
ISR(INT7_vect)   { 
    EIMSK &= ~_BV(INT7);   
    matrix_wake_flag = true; 
}
#endif

#if USE_PCINT0
ISR(PCINT0_vect) { 
    matrix_wake_flag = true; 
    timer_count += TIMER_INCR_ON_INTR;
}
#endif

#if USE_PCINT1
ISR(PCINT1_vect) { 
    matrix_wake_flag = true; 
    timer_count += TIMER_INCR_ON_INTR;
}
#endif

void matrix_sleep_arm(void) {
    matrix_wake_flag = false;
 
    // turn off the internal pull-ups on the encoder pins (leaving just the external pull-ups)
    gpio_set_pin_input(B4);
    gpio_set_pin_input(B5);

#if USE_INT7
    // INT7 low-level trigger (ISC71:ISC70 = 00).
    EICRB &= ~(_BV(ISC71) | _BV(ISC70));
    EIFR   = _BV(INTF7);
    EIMSK |= _BV(INT7);
#endif

#if USE_PCINT0
    PCIFR |= _BV(PCIF0);
    PCICR |= _BV(PCIE0);
    // PCINT4 (B4 enc-B), PCINT5 (B5 enc-A), PCINT6 (B6 btn), PCINT7 (B7 btn).
    PCMSK0 = _BV(PCINT4) | _BV(PCINT5) | _BV(PCINT6) | _BV(PCINT7);
#endif

#if USE_PCINT1
    PCIFR |= _BV(PCIF1);
    PCICR |= _BV(PCIE1);
#if MMM_VER >= 12
    // PCINT8 (motion pin C6), PCINT11 (C2 btn)
    PCMSK1 = _BV(PCINT8) | _BV(PCINT11);
#else
    PCMSK1 = _BV(PCINT8);
#endif
#endif
}

void matrix_sleep_disarm(void) {
#if USE_INT7
    EIMSK  &= ~_BV(INT7);
#endif

#if USE_PCINT0
    PCMSK0  = 0;
    PCICR  &= ~_BV(PCIE0);
#endif

#if USE_PCINT1
    PCMSK1  = 0;
    PCICR  &= ~_BV(PCIE1);
#endif

    // turn back on the internal pull-ups on the encoder pins
    gpio_set_pin_input_high(B4);
    gpio_set_pin_input_high(B5);
}
