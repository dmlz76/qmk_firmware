// Copyright 2026 Dimitrix LLC
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// MCU clock selection for boards fitted with the 8 MHz CSTNE8M00GH5C000R0
// resonator in place of the 16 MHz CSTNE16M0V530000R0. A rev*/config.h defines
// MCU_RESONATOR_8MHZ and *then* includes this header; included without the define
// it changes nothing, so a revision can carry the include with the define
// commented out as a toggle.
//
// The build has to match the fitted part: the USB PLL prescaler is fixed at
// compile time from F_USB, so a mismatched build never enumerates and runs every
// timer at the wrong rate. 8 MHz is also inside the speed grade of both the 16U2
// and the 32U4 at 3.3 V (~10.7 MHz), which 16 MHz is not.
//
// F_CPU/F_USB arrive from platforms/avr/mcu_selection.mk as -D flags. GCC applies
// every -D before any -include, and QMK -includes the config headers into every
// translation unit (LUFA's too), so redefining them here wins everywhere. No fuse
// change is needed on either MCU: the factory low fuse 0x5E already selects the
// 8-16 MHz range of the low-power oscillator (CKSEL3..1 = 111) with
// ceramic-resonator start-up.
//
// MCU_CPU_DIV (1, 2 or 4) then runs the CPU at 8 MHz / MCU_CPU_DIV through the
// system clock prescaler (CLKPR). While the AVR is awake its core is most of the
// board's current, and that falls with the clock: kbd_wireless v3.0.0 read 16-17,
// 9-10 and 5-6 mA at the cell, awake idle, at 8, 4 and 2 MHz (2026-10-31). A
// rev*/config.h sets it next to MCU_RESONATOR_8MHZ, under #ifndef so that
// -e MCU_CPU_DIV=1 (see the board's rules.mk) still builds a full-speed image.
//
// The division itself happens in keyboard_pre_init_kb(), in common/mcu_clock.c,
// which a board that sets MCU_CPU_DIV must compile. Without it F_CPU claims 2 MHz
// while the CPU runs at 8, and every delay and timer runs 4x fast. F_USB stays
// 8 MHz: on both parts the USB PLL takes the oscillator ahead of CLKPR (32U4
// doc7766 §6.9.1, 16U2 doc7799 Fig. 8-1), so USB's 48 MHz clock is unchanged.
#ifdef MCU_RESONATOR_8MHZ
#    undef F_CPU
#    if !defined(MCU_CPU_DIV) || MCU_CPU_DIV == 1
#        define F_CPU 8000000UL
#    elif MCU_CPU_DIV == 2
#        define F_CPU 4000000UL
#    elif MCU_CPU_DIV == 4
#        define F_CPU 2000000UL
#    else
#        error "MCU_CPU_DIV must be 1, 2 or 4"
#    endif
#    undef F_USB
#    define F_USB 8000000UL
#elif defined(MCU_CPU_DIV)
#    error "MCU_CPU_DIV needs MCU_RESONATOR_8MHZ"
#endif
