#pragma once

#include <stdbool.h>

// Wake-on-keypress helpers for BLE peripherals.
//
// Called from power_down() in ble_send_buf.cpp around sleep_cpu():
//   matrix_sleep_arm()    - drive cols low, pull rows up, enable async IRQs
//   matrix_sleep_disarm() - disable async IRQs (next matrix_scan reconfigures pins)
//
// Each board supplies its own implementation matching its MCU's wake sources.
// Wake sets matrix_wake_flag so power_savings_on() can distinguish key wakes
// from WDT ticks if it wants to.

#ifdef __cplusplus
extern "C" {
#endif

extern volatile bool matrix_wake_flag;

void matrix_sleep_arm(void);
void matrix_sleep_disarm(void);

#ifdef __cplusplus
}
#endif
