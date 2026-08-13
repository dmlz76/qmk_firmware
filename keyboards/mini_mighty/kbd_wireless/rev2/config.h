#pragma once

#define MMKW_VER 2

#define RST_PIN C6
#define SLEEP_PIN D7

// Watchdog period used for MCU power-down sleeps. Async wake
// handles button/encoder events regardless of this value, so it only bounds
// the timer drift correction window in WDT_vect. See ble_send_buf.cpp.
#define MCU_POWER_DOWN_WDTO WDTO_1S
