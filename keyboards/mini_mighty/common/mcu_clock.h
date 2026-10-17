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
#ifdef MCU_RESONATOR_8MHZ
#    undef F_CPU
#    define F_CPU 8000000UL
#    undef F_USB
#    define F_USB 8000000UL
#endif
