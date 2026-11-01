// Copyright 2026 Dimitrix LLC
// SPDX-License-Identifier: GPL-2.0-or-later

#include <avr/power.h>
#include "keyboard.h"
// Already in via config.h on a board that sets MCU_RESONATOR_8MHZ. Included again
// for the #error that MCU_CPU_DIV on a 16 MHz board (e.g. mouse rev12) must hit.
#include "mcu_clock.h"

#if defined(MCU_CPU_DIV) && MCU_CPU_DIV > 1
// Divide the CPU clock (common/mcu_clock.h) before timer_init() and USB init run.
// LUFA's setup_mcu() has just set clock_div_1, since F_CPU != F_USB matches none
// of its cases. Earlier boot steps (platform_setup, matrix_setup) time nothing
// off F_CPU.
void keyboard_pre_init_kb(void) {
    clock_prescale_set(MCU_CPU_DIV == 4 ? clock_div_4 : clock_div_2);
    keyboard_pre_init_user();
}
#endif
