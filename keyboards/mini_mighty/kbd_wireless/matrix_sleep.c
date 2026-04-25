#include "matrix_sleep.h"
#include "gpio.h"
#include "quantum.h"
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/io.h>


volatile bool matrix_wake_flag = false;

void matrix_sleep_arm(void) {
    matrix_wake_flag = false;
 }

void matrix_sleep_disarm(void) {
}
