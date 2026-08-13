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
 
#if MMM_VER >= 12
    // turn off the internal pull-ups on the encoder pins (leaving just the external pull-ups)
    gpio_set_pin_input(B4);
    gpio_set_pin_input(B5);
#endif

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

    // Check-after-arm — mirrors kbd_wireless/matrix_sleep.c. PCINT fires on a
    // transition in either direction, but never on a static level: a button
    // ALREADY low when we get here (held across the sleep decision, or pressed in
    // the window between the last scan and the PCICR writes above) produces no
    // transition and no wake. It stays invisible until the watchdog expires, and
    // at MCU_POWER_DOWN_WDTO = WDTO_1S that is long enough for the button to be
    // released first — the release edge does wake us, but sym_defer_g then sees
    // raw back in agreement with cooked and folds the press away unreported.
    //
    // Sample the buttons and the motion pin. No settling delay is needed: unlike
    // the keyboard, arm() does not reconfigure these pins — they are already
    // input+pull-up from the matrix scan / pointing_device_init().
    static const pin_t button_pins[][MATRIX_COLS] = DIRECT_PINS;
    for (uint8_t r = 0; r < ARRAY_SIZE(button_pins); r++) {
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            if (button_pins[r][c] != NO_PIN && !gpio_read_pin(button_pins[r][c])) {
                matrix_wake_flag = true;
            }
        }
    }

#ifdef POINTING_DEVICE_MOTION_PIN
    // The PAW3220 holds MOTION low until the delta registers are read, and
    // pointing_device_task() only reads them when it isn't throttled
    // (POINTING_DEVICE_TASK_THROTTLE_MS). mcu_power_down() calls that task right
    // before sleeping, but the call is usually throttled out because
    // keyboard_task() just ran it — so motion starting in the ~1-3 ms between the
    // last successful sensor read and the PCICR write above leaves MOTION already
    // low with no transition for PCINT8 to catch. The mouse would then be dead
    // until the watchdog fires. Veto the sleep instead; this is self-limiting,
    // since the next unthrottled pointing_device_task() reads the sensor and
    // releases the pin.
    if (!gpio_read_pin(POINTING_DEVICE_MOTION_PIN)) {
        matrix_wake_flag = true;
    }
#endif

    // Deliberately NOT sampled: the encoder pins B4/B5. They legitimately rest
    // with a contact closed in some detent positions (see the 1M external pull-up
    // work, 2026-05-11), so vetoing on those would keep the MCU awake forever.
    // They are safe to leave out — any rotation transitions at least one of the
    // two pins, in either direction, so PCINT always sees an edge.
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

#if MMM_VER >= 12
    // turn back on the internal pull-ups on the encoder pins
    gpio_set_pin_input_high(B4);
    gpio_set_pin_input_high(B5);
#endif
}
