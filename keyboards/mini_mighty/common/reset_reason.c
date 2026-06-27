#include "reset_reason.h"

#include <avr/io.h>
#include <avr/wdt.h>

#ifdef CONSOLE_ENABLE
#    include "print.h"
#    include "timer.h"
#endif

// MCUSR is cleared by the avr-libc/QMK startup before main() runs, so snapshot
// it in .init3 (which executes before .data/.bss init; a .noinit var survives
// that) and clear the flags so the *next* reset reads clean. Disabling the
// watchdog here also breaks any WDT-reset boot loop before it can re-trigger.
//
// This is the canonical avr-libc "save MCUSR" idiom. The function is naked
// because .initN entries fall through to the next init section instead of
// returning, so no prologue/epilogue (and no ret) is wanted.
uint8_t g_reset_mcusr __attribute__((section(".noinit")));

void grab_reset_mcusr(void) __attribute__((naked, used, section(".init3")));
void grab_reset_mcusr(void) {
    g_reset_mcusr = MCUSR;
    MCUSR         = 0;
    wdt_disable();
}

void reset_reason_task(void) {
#ifdef CONSOLE_ENABLE
    // Re-emit a handful of times after boot: post_init runs before the host
    // attaches the console reader, so a single print is lost. Raw MCUSR byte +
    // bit legend (one format string keeps this tiny on the near-full 16u2).
    // Bits: POR=0x01 EXT=0x02 BOD=0x04 WDT=0x08; 0x00 => software jmp 0 / crash.
    static uint8_t  count = 0;
    static uint16_t last  = 0;
    if (count >= 15) return;                            // ~15 prints then quiet
    if (count != 0 && timer_elapsed(last) < 1000) return; // ~1s apart
    last = timer_read();
    count++;
    uprintf("MCUSR=0x%02X (POR1 EXT2 BOD4 WDT8)\n", g_reset_mcusr);
#endif
}
