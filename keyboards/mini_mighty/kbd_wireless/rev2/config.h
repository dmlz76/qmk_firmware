#pragma once

#define MMKW_VER 2

#define RST_PIN C6
#define SLEEP_PIN D7

// Watchdog period used for MCU power-down sleeps. Async wake (INT0/INT1 +
// PCINT4..7) handles keypresses, so this is not the latency for normal input --
// but it IS what a *missed* wake costs: the MCU stays down for the full period,
// the key is released before the next scan, and sym_defer_g folds the press away
// unreported. matrix_sleep_arm()'s check-after-arm closes the known holes; keep
// this modest anyway so an unknown one degrades to a hiccup rather than a lost
// keystroke. Under evaluation at 8s (2026-08-13). See ble_send_buf.cpp.
#define MCU_POWER_DOWN_WDTO WDTO_8S
