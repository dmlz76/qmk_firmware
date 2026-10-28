// Copyright 2026 Dimitrix LLC
// SPDX-License-Identifier: GPL-2.0-or-later

#include "swd.h"
#include <avr/io.h>
#include "wait.h"

// Dongle v7 wiring (~/repos/mini_mighty_mouse, "WIP dongle v7"):
//   PD1 (U2.7) -> R9 1k -> SWCLK, with R10 2k2 to GND
//   PD2 (U2.8) -> R12 330R -> SWDIO, with R11 2k2 up to the nRF's 3.3 V
// The 16U2 runs from /VBUS (~4.8 V) and the nRF from 3.3 V, so:
//   - SWCLK is push-pull, but only while a session is open. R9 against R10 and the nRF's
//     ~13k pull-down puts the 4.8 V high at ~3.1 V on the pin.
//   - SWDIO is open drain: driven low, or released for R11 to pull up. PORTD2 is never
//     set, so a released pin is a plain input and the nRF never sees 4.8 V. Nothing here
//     writes PIND either, which would toggle PORTD.
// The rest of the time both pins are inputs with the pull-up off: the AVR's reset state,
// and what every dongle image has left them in, v6 boards (where they're unconnected)
// included.
//
// Every pin change is a single-bit read-modify-write of a low I/O register, which GCC
// compiles to one sbi/cbi, so the bit-bang can't race the SPI ISRs and runs with
// interrupts on. SWD has no minimum clock rate, so an ISR stretching a bit costs nothing.
#define SWCLK_BIT PD1
#define SWDIO_BIT PD2

// Half a clock. A released SWDIO rises through R11 in ~50 ns; at 16 MHz this, with the
// instructions around it, gives it ~250 ns before the edge that samples it.
#define SWD_DELAY() __builtin_avr_delay_cycles(2)

// Request bits, as swd_xfer() takes them: APnDP, RnW, then A[3:2] in place.
#define REQ_AP 0x01
#define REQ_R 0x02

#define DP_IDR_R (REQ_R | 0x00)
#define DP_ABORT_W 0x00
#define DP_CTRL_W 0x04
#define DP_CTRL_R (REQ_R | 0x04)
#define DP_SELECT_W 0x08
#define DP_RDBUFF_R (REQ_R | 0x0C)

#define ABORT_CLEAR_ALL 0x1EUL      // STKCMPCLR | STKERRCLR | WDERRCLR | ORUNERRCLR
#define CTRL_PWRUP_REQ 0x50000000UL // CSYSPWRUPREQ | CDBGPWRUPREQ
#define CTRL_PWRUP_ACK 0xA0000000UL // CSYSPWRUPACK | CDBGPWRUPACK
#define CTRL_STICKYERR 0x00000020UL

#define MEM_AP 0
#define MEM_CSW 0x00
#define MEM_TAR 0x04
#define MEM_DRW 0x0C
// 32-bit accesses, no auto-increment (TAR is written for every word), debug master, and the
// HPROT bits the SoC expects (CMSIS-DAP's value, less SADDRINC).
#define MEM_CSW_VALUE 0x23000042UL

#define ACK_OK 1
#define ACK_WAIT 2
#define ACK_FAULT 4
#define WAIT_RETRIES 255

uint32_t swd_data;

// SELECT as last written: (APSEL << 4) | APBANKSEL, or 0xFF when unknown.
static uint8_t s_select = 0xFF;

bool swd_wired(void) {
    PORTD |= _BV(SWCLK_BIT); // pull-up on
    wait_us(20);
    bool high = PIND & _BV(SWCLK_BIT);
    PORTD &= ~_BV(SWCLK_BIT);
    return !high;
}

void swd_pins_off(void) {
    DDRD &= ~_BV(SWDIO_BIT);
    DDRD &= ~_BV(SWCLK_BIT);
    PORTD &= ~_BV(SWCLK_BIT);
    s_select = 0xFF;
}

static void dio_out(bool bit) {
    if (bit) {
        DDRD &= ~_BV(SWDIO_BIT);
    } else {
        DDRD |= _BV(SWDIO_BIT);
    }
}

// One clock: the target samples SWDIO on the rising edge, and changes its own output then.
static inline void clock_cycle(void) {
    PORTD &= ~_BV(SWCLK_BIT);
    SWD_DELAY();
    PORTD |= _BV(SWCLK_BIT);
    SWD_DELAY();
}

static void swd_write_bits(uint32_t v, uint8_t n) {
    do {
        dio_out(v & 1);
        clock_cycle();
        v >>= 1;
    } while (--n);
}

static uint32_t swd_read_bits(uint8_t n) {
    uint32_t v = 0;
    for (uint8_t i = n; i; i--) {
        PORTD &= ~_BV(SWCLK_BIT);
        SWD_DELAY();
        v >>= 1;
        if (PIND & _BV(SWDIO_BIT)) {
            v |= 0x80000000UL;
        }
        PORTD |= _BV(SWCLK_BIT);
        SWD_DELAY();
    }
    return v >> (32 - n);
}

// Idle cycles, SWDIO low, so a posted write completes before the clock stops: between
// transactions it can stop for as long as the main loop likes.
static void swd_idle(void) {
    swd_write_bits(0, 8);
    dio_out(1);
}

// One transaction, its data in or out through swd_data.
static uint8_t swd_xfer(uint8_t req) {
    uint8_t hdr = 0x81 | (uint8_t)(req << 1) | (uint8_t)(__builtin_parity(req & 0x0F) << 5);
    for (uint8_t tries = WAIT_RETRIES;; tries--) {
        swd_write_bits(hdr, 8);
        dio_out(1); // turnaround: the target drives the ACK
        clock_cycle();
        uint8_t ack = swd_read_bits(3);

        if (ack == ACK_OK) {
            if (req & REQ_R) {
                uint32_t v      = swd_read_bits(32);
                bool     parity = swd_read_bits(1);
                clock_cycle(); // turnaround back to us
                swd_idle();
                if (parity != __builtin_parityl(v)) {
                    return SWD_ERR_PARITY;
                }
                swd_data = v;
            } else {
                clock_cycle(); // turnaround back to us
                swd_write_bits(swd_data, 32);
                swd_write_bits(__builtin_parityl(swd_data), 1);
                swd_idle();
            }
            return SWD_OK;
        }

        if (ack == ACK_WAIT || ack == ACK_FAULT) {
            clock_cycle(); // turnaround; no data phase
            dio_out(1);
            if (ack == ACK_FAULT) {
                return SWD_ERR_FAULT;
            }
            if (!tries) {
                return SWD_ERR_WAIT;
            }
            continue;
        }

        // No valid ACK. Clock out what would have been a data phase, so a target that did
        // see the request is back at idle; the caller has to reconnect either way.
        for (uint8_t i = 34; i; i--) {
            clock_cycle();
        }
        s_select = 0xFF;
        return SWD_ERR_PROTOCOL;
    }
}

static uint8_t dp_write(uint8_t req, uint32_t v) {
    swd_data = v;
    return swd_xfer(req);
}

// Clears the sticky flags after a FAULT, so the next access can go through.
static uint8_t fault_clear(uint8_t r) {
    if (r == SWD_ERR_FAULT) {
        dp_write(DP_ABORT_W, ABORT_CLEAR_ALL);
    }
    return r;
}

static void line_reset(void) {
    swd_write_bits(0xFFFFFFFFUL, 32);
    swd_write_bits(0xFFFFFFFFUL, 24);
}

uint8_t swd_connect(void) {
    PORTD &= ~_BV(SWDIO_BIT); // already 0; the invariant
    DDRD &= ~_BV(SWDIO_BIT);  // released
    PORTD |= _BV(SWCLK_BIT);  // the clock idles high between bits
    DDRD |= _BV(SWCLK_BIT);
    s_select = 0xFF;

    // Line reset, the JTAG-to-SWD switch (the nRF52 is SW-DP only and ignores it), line
    // reset again, then idle.
    line_reset();
    swd_write_bits(0xE79E, 16);
    line_reset();
    swd_idle();

    uint8_t r = swd_xfer(DP_IDR_R);
    if (r) {
        return r;
    }
    uint32_t dpidr = swd_data;
    r              = dp_write(DP_ABORT_W, ABORT_CLEAR_ALL);
    if (!r) r = dp_write(DP_CTRL_W, CTRL_PWRUP_REQ);
    for (uint8_t i = 100; !r; i--) {
        r = swd_xfer(DP_CTRL_R);
        if (!r && (swd_data & CTRL_PWRUP_ACK) == CTRL_PWRUP_ACK) {
            // Best effort: with APPROTECT engaged the AHB-AP may refuse it.
            swd_data = MEM_CSW_VALUE;
            swd_ap(MEM_AP, MEM_CSW, false);
            break;
        }
        if (!i) {
            r = SWD_ERR_WAIT;
        }
    }
    swd_data = dpidr;
    return r;
}

uint8_t swd_power_down(void) {
    return dp_write(DP_CTRL_W, 0);
}

uint8_t swd_ap(uint8_t ap, uint8_t reg, bool read) {
    uint8_t r   = SWD_OK;
    uint8_t sel = (uint8_t)(ap << 4) | (reg >> 4);
    if (sel != s_select) {
        uint32_t v = swd_data;
        r          = dp_write(DP_SELECT_W, ((uint32_t)ap << 24) | (reg & 0xF0));
        swd_data   = v;
        s_select   = r ? 0xFF : sel;
    }
    // AP reads are posted: this one returns the previous read's data, and RDBUFF this one's.
    if (!r) r = swd_xfer(REQ_AP | (read ? REQ_R : 0) | (reg & 0x0C));
    if (!r && read) r = swd_xfer(DP_RDBUFF_R);
    return fault_clear(r);
}

uint8_t swd_mem(uint32_t addr, bool read) {
    uint32_t v = swd_data;
    swd_data   = addr;
    uint8_t r  = swd_ap(MEM_AP, MEM_TAR, false);
    swd_data   = v;
    return r ? r : swd_ap(MEM_AP, MEM_DRW, read);
}

uint8_t swd_mem_end(void) {
    // RDBUFF answers WAIT until the last write is done; STICKYERR says if any failed.
    uint8_t r = swd_xfer(DP_RDBUFF_R);
    if (!r) r = swd_xfer(DP_CTRL_R);
    if (!r && (swd_data & CTRL_STICKYERR)) {
        r = SWD_ERR_FAULT;
    }
    return fault_clear(r);
}
