// Copyright 2026 Dimitrix LLC
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Feature gates derived from the PCB revision.
//
// This has to be post_config.h rather than config.h: QMK includes the shared
// mouse/config.h *before* the leaf rev*/config.h that defines MMM_VER, and only
// the post_config.h pass runs late enough to compare it.

// v1.0.0 replaced the SPDT battery switch with a DPDT whose second pole grounds
// the nRF's ~RESET line through 1 kOhm in the OFF position -- a wireless kill
// switch that needs no MCU pin, since the line the AVR already owns carries both
// the enforcement and the sense. Implementation — and why a runtime flip has to
// be sampled rather than waited for — is kill_switch_task() in
// common/ble_send_buf.cpp.
#if MMM_VER >= MM_PCB_VERSION(1, 0, 0)
#    define BLE_KILL_SWITCH_ON_RESET_PIN
#endif
