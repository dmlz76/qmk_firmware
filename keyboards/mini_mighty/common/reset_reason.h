#pragma once
// Captures the AVR reset cause (MCUSR) very early at boot and prints a decoded
// reason once the console is up — the AVR analog of the nRF's RESETREAS log.
// Used to classify the intermittent mini_mighty resets (brown-out vs watchdog
// vs software jmp 0). See MOUSE_NRF_REBOOT_HANDOFF.md.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Reset flags snapshotted from MCUSR early at boot (.init5 — after QMK's .init3
// DFU watchdog-reset check, so it doesn't clobber WDRF), before anything clears it.
extern uint8_t g_reset_mcusr;

// Call every loop from housekeeping_task_user(). Prints the captured MCUSR a
// handful of times over the first several seconds so `qmk console` catches it
// even when attached after boot (and re-prints on every crash/reboot). A
// one-shot print from keyboard_post_init_user() fires before the host attaches
// the console reader, so it would be lost. No-op without CONSOLE_ENABLE.
void reset_reason_task(void);

#ifdef __cplusplus
}
#endif
