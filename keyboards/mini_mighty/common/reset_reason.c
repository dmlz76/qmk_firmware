#include "reset_reason.h"

#include <avr/io.h>
#include <avr/wdt.h>

#ifdef CONSOLE_ENABLE
#    include "print.h"
#    include "timer.h"
#endif

// Snapshot MCUSR before anything clears it, store it in a .noinit var (which
// survives .data/.bss init), clear the flags so the *next* reset reads clean,
// and disable the watchdog to break any WDT-reset boot loop.
//
// IMPORTANT — must run in .init5, NOT .init3: QMK enters the DFU bootloader via
// a watchdog reset (bootloader_jump() in platforms/avr/bootloaders/dfu.c), and
// the post-reset check bootloader_jump_after_watchdog_reset() runs in .init3
// and reads (MCUSR & (1<<WDRF)) to decide whether to jump into DFU. Clearing
// MCUSR in .init3 races that check (intra-section order is just link order); if
// we win, WDRF is wiped and bootmagic / QK_BOOT silently stop entering DFU.
// .init5 runs strictly after the .init3 check (and after .init4 data/bss init),
// so the bootloader check sees WDRF intact, and MCUSR is still untouched here
// for our capture. The watchdog stays enabled an extra ~microseconds (.init3 ->
// .init5), far under even WDTO_15MS, so the boot-loop protection is unaffected.
//
// The function is naked because .initN entries fall through to the next init
// section instead of returning, so no prologue/epilogue (and no ret) is wanted.
uint8_t g_reset_mcusr __attribute__((section(".noinit")));

void grab_reset_mcusr(void) __attribute__((naked, used, section(".init5")));
void grab_reset_mcusr(void) {
    g_reset_mcusr = MCUSR;
    MCUSR         = 0;
    wdt_disable();
}

void reset_reason_task(void) {
#ifdef CONSOLE_ENABLE
    // Print once. The RAM console buffer (console_buffer.c) holds it until a
    // listener attaches and then drains it, so the old windowed/continuous
    // re-emit is no longer needed. A fresh line appearing therefore means a real
    // (re)boot occurred — handy for telling a reboot from a non-reboot event.
    // Raw MCUSR byte + bit legend. Bits: POR=0x01 EXT=0x02 BOD=0x04 WDT=0x08;
    // 0x00 => software jmp 0 / crash.
    static bool printed = false;
    if (printed) return;
    printed = true;
    uprintf("MCUSR=0x%02X (POR1 EXT2 BOD4 WDT8)\n", g_reset_mcusr);
#endif
}
