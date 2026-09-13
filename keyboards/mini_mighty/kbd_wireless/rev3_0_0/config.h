#pragma once

#define MMKW_VER MM_PCB_VERSION(3, 0, 0)

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

// v3.0.0 replaced the SPDT battery switch with a DPDT whose second pole grounds
// the nRF's ~RESET line through 1 kOhm in the OFF position -- a wireless kill
// switch that needs no MCU pin, since the line the AVR already owns carries both
// the enforcement and the sense. Implementation — and why a runtime flip has to
// be sampled rather than waited for — is kill_switch_task() in
// common/ble_send_buf.cpp.
#define BLE_KILL_SWITCH_ON_RESET_PIN 1
