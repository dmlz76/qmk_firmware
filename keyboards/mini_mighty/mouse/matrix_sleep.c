#include "matrix_sleep.h"
#include "gpio.h"
#include "quantum.h"
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/io.h>

// Direct-pin matrix on atmega16u2: D7, B6, B7 (per keyboard.json).
// All three are async-wake capable: D7 via INT7, B6/B7 via PCINT0 group.
// Rotary encoder A/B on B5/B4 also sit in the PCINT0 group, so quadrature
// edges wake the MCU through the same PCINT0_vect.
//

volatile bool matrix_wake_flag = false;

// TODO: Disable INT7 for next PCB revision
#define USE_INT7 1
#define USE_PCINT0 1
#define USE_PCINT1 1

#if USE_INT7
ISR(INT7_vect)   { 
    EIMSK &= ~_BV(INT7);   
    matrix_wake_flag = true; 
}
#endif

#if USE_PCINT0
ISR(PCINT0_vect) { 
    matrix_wake_flag = true; 
}
#endif

#if USE_PCINT1
ISR(PCINT1_vect) { 
    matrix_wake_flag = true; 
}
#endif

void matrix_sleep_arm(void) {
    matrix_wake_flag = false;
 
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
    // PCINT8 (motion pin C6)
    // TODO: Add PCINT11 (C2 btn) for next PCB revision 
    PCMSK1 = _BV(PCINT8);
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
}
