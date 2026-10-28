// Copyright 2026 Dimitrix LLC
// SPDX-License-Identifier: GPL-2.0-or-later

// SWD bridge: lets a host tool (keyboards/mini_mighty/tools/mm_swd.py) flash the dongle's
// nRF52805 and read its RTT log over the dongle's own USB, through QMK raw HID. Dongle PCB
// v7 wires SWD to PD1/PD2 (swd.c). On v6 the pins are unconnected: INFO says so, and
// nothing else gets past that.
//
// The AVR is only a memory bridge with a gate in front of it. Everything that knows the
// nRF's flash (NVMC, which pages to erase, verifying, finding RTT) lives in the host tool.
//
// Why the gate: SWD reaches the running nRF's pairing keys (in flash and RAM) and can
// replace the radio firmware. Rev 1 nRF52805s (the 2026 prototypes) never lock with
// UICR.APPROTECT erased, and on rev 2 the firmware releases APPROTECT at every boot. Before this bridge
// nothing on the host could reach either. So memory access takes a press of the dongle's
// button, S1, which the AVR reads over SWD with the nRF halted. Neither host software nor a
// broken nRF image can fake that.
//   CLOSED      INFO, CONNECT, UNLOCK and RELEASE only.
//   UNLOCK      If the AHB-AP answers: halt the core, make P0.00 (S1) an input with
//               pull-up, and wait up to 30 s for S1 to read released, pressed, released.
//               Then OPEN. Halted, the nRF can't act on the press itself (its 60 s pairing
//               window, or the 10 s erase), and it's released before the nRF runs again.
//               If the AHB-AP doesn't answer: pin reset, give the firmware 100 ms to
//               release APPROTECT, and look again. Still locked (a protected or fresh
//               module, a broken image, UICR.APPROTECT Enabled, or on rev 2 not
//               HwDisabled) gets ERASE_ONLY.
//   ERASE_ONLY  ERASEALL, which returns once the erase is done, and then the part is
//               blank and the gate OPEN. A working dongle can't be made to look locked
//               from the host.
//   OPEN        Everything, until RELEASE or 10 s without a command.
// A session that halted the nRF or wrote outside its RAM ends in a pin reset, which also
// restores the firmware's own P0.00 config. -e EXTRAFLAGS=-DMM_SWD_BRIDGE_NO_BUTTON skips
// the press: bench builds only.
//
// Reports are 32 bytes each way. Request: [0] command, [1] sequence, [2..] arguments.
// Reply: [0] command, [1] sequence, [2] status (ST_*), [3..] results; bytes past the
// results are undefined. Multi-byte values are little-endian. The commands start at 0xB0,
// clear of VIA's 0x01..0x15, and anything else gets [0] = 0xFF back, as VIA answers what it
// doesn't handle. So a tool probing for VIA can't stumble into the bridge.
//   INFO       -> [3] protocol version, [4] gate (GATE_*), [5] flags (INFO_*), [6] status
//                 of the last UNLOCK, or the reason for ERASE_ONLY
//   CONNECT    -> [3..6] DPIDR, [7] 1 if the AHB-AP answers
//   UNLOCK     -> PENDING, then poll INFO for the gate. OK if already OPEN.
//   RELEASE    Ends the session: debugger detached, pins off, pin reset if needed.
//   MEM_READ   [2..5] address, [6] words (1..7) -> [3] words, [4..] data
//   MEM_WRITE  [2..5] address, [6] words (1..6), [7..] data
//   MEM_CRC32  [2..5] address, [6..7] words (1..256), [8..11] CRC so far (0 to start)
//              -> [3..6] zlib.crc32() of the bytes, chained the way zlib chains it
//   ERASEALL   CTRL-AP ERASEALL, returning when it's done (~200 ms). CONNECT again after.
//   RESET      Debugger detached, pins off, pin reset. CONNECT again after ~100 ms.
//   AP_READ    [2] AP, [3] register -> [3..6] value
//   AP_WRITE   [2] AP, [3] register, [4..7] value
// MEM_CRC32, ERASEALL and UNLOCK on a locked part hold the main loop for a while (up to
// ~70 ms, ~200 ms, 100 ms). They're rare, and the nRF's radio is down for all of them.

#include <string.h>
#include "quantum.h"
#include "raw_hid.h"
#include "swd.h"

#define SWD_PROTOCOL_VERSION 1

enum {
    CMD_INFO = 0xB0,
    CMD_CONNECT,
    CMD_UNLOCK,
    CMD_RELEASE,
    CMD_MEM_READ,
    CMD_MEM_WRITE,
    CMD_MEM_CRC32,
    CMD_ERASEALL,
    CMD_RESET,
    CMD_AP_READ,
    CMD_AP_WRITE,
};

enum {
    ST_OK = 0,
    ST_PENDING,
    ST_DENIED,        // the gate doesn't allow it now
    ST_BUSY,          // waiting for S1
    ST_BAD_ARGS,
    ST_NO_WIRING,     // no SWD on PD1/PD2: a v6 dongle
    ST_NOT_CONNECTED, // CONNECT first
    ST_TIMEOUT,       // no S1 press in time, or ERASEALL never finished
    ST_LOCKED,        // the AHB-AP doesn't answer
    ST_SWD = 0x10,    // + SWD_ERR_*
};

enum {
    GATE_CLOSED,
    GATE_WAIT_PRESS,
    GATE_WAIT_RELEASE,
    GATE_ERASE_ONLY,
    GATE_OPEN,
};

#define INFO_WIRED 0x01
#define INFO_CONNECTED 0x02
#define INFO_TOUCHED 0x04   // halted or written outside RAM; ending the session resets it
#define INFO_NO_BUTTON 0x08 // a bench build: UNLOCK doesn't wait for S1

#define NRF_RESET_PIN D4 // /BLE_RESET, open drain: see matrix_custom.c

#define CTRL_AP 1
#define CTRL_AP_ERASEALL 0x04
#define CTRL_AP_ERASEALLSTATUS 0x08

#define NRF_RAM_TOP_BYTE 0x20 // RAM is 0x20000000..0x20005FFF
#define FICR_INFO_PART 0x10000100UL
#define P0_IN 0x50000510UL
#define P0_PIN_CNF0 0x50000700UL
#define PIN_CNF_INPUT_PULLUP 0x0CUL
#define DHCSR 0xE000EDF0UL
#define DHCSR_DBGKEY 0xA05F0000UL
#define DHCSR_HALT (DHCSR_DBGKEY | 0x3) // C_DEBUGEN | C_HALT
#define DHCSR_S_HALT (1UL << 17)
#define DEMCR 0xE000EDFCUL

#define POLL_MS 10
#define PROBE_WAIT_MS 100
#define PRESS_TIMEOUT_MS 30000
#define ERASE_TIMEOUT_MS 3000
#define ERASE_POLL_MS 20
#define IDLE_TIMEOUT_MS 10000
#define DEBOUNCE_POLLS 3

static struct {
    uint8_t  gate;
    uint8_t  result;    // INFO [6]
    uint8_t  debounce;  // polls in a row that S1 has read the wanted way
    bool     armed;     // S1 has read released since UNLOCK
    bool     on;        // pins driven
    bool     connected; // debug powered and the line in sync
    bool     touched;   // see INFO_TOUCHED
    uint16_t t_gate;    // when the wait for S1 started
    uint16_t t_poll;
    uint16_t t_cmd; // last command, for the idle timeout
} s;

static uint8_t swd_status(uint8_t r) {
    if (r == SWD_ERR_PROTOCOL) {
        s.connected = false;
    }
    return r ? ST_SWD + r : ST_OK;
}

// Leaves DPIDR in swd_data.
static uint8_t bridge_connect(void) {
    if (!s.on && !swd_wired()) {
        return ST_NO_WIRING;
    }
    s.on        = true;
    uint8_t r   = swd_connect();
    s.connected = !r;
    return swd_status(r);
}

// FICR.INFO.PART is 0x52805 here, 0x5xxxx on any nRF52 (only the top half is compared:
// it's cheaper on the AVR). With APPROTECT engaged the AHB-AP doesn't return it.
static bool nrf_ahb_open(void) {
    return !swd_status(swd_mem(FICR_INFO_PART, true)) && (uint16_t)(swd_data >> 16) == 0x0005;
}

static void nrf_pin_reset(void) {
    gpio_set_pin_output(NRF_RESET_PIN); // PORTD4 is 0, so this drives it low
    wait_ms(1);
    gpio_set_pin_input(NRF_RESET_PIN);
    s.touched = false;
}

// Lets the core run free of the debugger and powers debug down, so a pin reset after it
// boots the firmware as it would without one. The nRF repo's J-Link scripts found that
// resetting with the debugger attached could leave the nRF in lockup.
static void swd_detach(void) {
    if (s.connected) {
        if (s.touched) {
            swd_data = 0;
            swd_mem(DEMCR, false);
            swd_data = DHCSR_DBGKEY;
            swd_mem(DHCSR, false);
            swd_mem_end();
        }
        swd_power_down();
    }
    swd_pins_off();
    s.on = s.connected = false;
}

static void bridge_end(void) {
    bool touched = s.touched;
    swd_detach();
    if (touched) {
        nrf_pin_reset();
    }
    s.gate = GATE_CLOSED;
}

static void gate_open(void) {
    s.gate   = GATE_OPEN;
    s.result = ST_OK;
}

static void gate_fail(uint8_t status) {
    bridge_end();
    s.result = status;
}

static void gate_poll_button(void) {
    uint8_t r = swd_mem(P0_IN, true);
    if (r) {
        gate_fail(swd_status(r));
        return;
    }
    if (timer_elapsed(s.t_gate) > PRESS_TIMEOUT_MS) {
        gate_fail(ST_TIMEOUT);
        return;
    }
    bool pressed = !(swd_data & 1);
    if (s.gate == GATE_WAIT_PRESS) {
        // Only a press that starts after UNLOCK counts: not a button already held, nor one
        // stuck down.
        if (!pressed) {
            s.armed    = true;
            s.debounce = 0;
        } else if (s.armed && ++s.debounce >= DEBOUNCE_POLLS) {
            s.gate     = GATE_WAIT_RELEASE;
            s.debounce = 0;
        }
    } else if (pressed) {
        s.debounce = 0;
    } else if (++s.debounce >= DEBOUNCE_POLLS) {
        gate_open();
        s.t_cmd = timer_read();
    }
}

static void swd_bridge_task(void) {
    if ((s.gate == GATE_CLOSED && !s.on) || timer_elapsed(s.t_poll) < POLL_MS) {
        return;
    }
    s.t_poll = timer_read();
    if (s.gate == GATE_WAIT_PRESS || s.gate == GATE_WAIT_RELEASE) {
        gate_poll_button();
    } else if (timer_elapsed(s.t_cmd) > IDLE_TIMEOUT_MS) {
        bridge_end();
    }
}

void housekeeping_task_kb(void) {
    swd_bridge_task();
    housekeeping_task_user();
}

static uint8_t cmd_unlock(void) {
    switch (s.gate) {
        case GATE_CLOSED:
            break;
        case GATE_OPEN:
            return ST_OK;
        case GATE_ERASE_ONLY:
            return ST_LOCKED;
        default:
            return ST_PENDING;
    }
    uint8_t st = bridge_connect();
    if (!st && !nrf_ahb_open()) {
        swd_detach();
        nrf_pin_reset();
        wait_ms(PROBE_WAIT_MS);
        st = bridge_connect();
        if (!st && !nrf_ahb_open()) {
            s.gate   = GATE_ERASE_ONLY;
            s.result = ST_LOCKED;
            return ST_LOCKED;
        }
    }
    if (st) {
        return st;
    }
#ifdef MM_SWD_BRIDGE_NO_BUTTON
#    pragma message "MM_SWD_BRIDGE_NO_BUTTON: the SWD bridge opens without a press of S1. Bench builds only."
    gate_open();
    return ST_OK;
#else
    // Halt, so neither the nRF nor its firmware sees the press, and S1 reads as the AVR
    // sets it up.
    s.touched = true;
    swd_data  = DHCSR_HALT;
    uint8_t r = swd_mem(DHCSR, false);
    if (!r) r = swd_mem(DHCSR, true);
    if (!r && !(swd_data & DHCSR_S_HALT)) r = SWD_ERR_FAULT;
    swd_data = PIN_CNF_INPUT_PULLUP;
    if (!r) r = swd_mem(P0_PIN_CNF0, false);
    if (!r) r = swd_mem_end();
    if (r) {
        gate_fail(swd_status(r));
        return s.result;
    }
    s.gate     = GATE_WAIT_PRESS;
    s.armed    = false;
    s.debounce = 0;
    s.t_gate   = timer_read();
    return ST_PENDING;
#endif
}

static uint8_t cmd_eraseall(void) {
    swd_data  = 1;
    uint8_t r = swd_ap(CTRL_AP, CTRL_AP_ERASEALL, false);
    s.touched = true;
    for (uint8_t i = ERASE_TIMEOUT_MS / ERASE_POLL_MS; !r; i--) {
        r = swd_ap(CTRL_AP, CTRL_AP_ERASEALLSTATUS, true);
        if (!r && !(swd_data & 1)) {
            // Blank now, keys included, so nothing is left to protect.
            gate_open();
            return ST_OK;
        }
        if (!i) {
            return ST_TIMEOUT;
        }
        wait_ms(ERASE_POLL_MS);
    }
    return swd_status(r);
}

// Whether the gate lets cmd through now; ST_OK if so.
static uint8_t gate_allows(uint8_t cmd) {
    switch (cmd) {
        case CMD_INFO:
        case CMD_UNLOCK: // sorts the gate out itself
        case CMD_RELEASE:
            return ST_OK;
        case CMD_CONNECT:
            return s.gate == GATE_WAIT_PRESS || s.gate == GATE_WAIT_RELEASE ? ST_BUSY : ST_OK;
        case CMD_ERASEALL:
            return s.gate == GATE_ERASE_ONLY || s.gate == GATE_OPEN ? ST_OK : ST_DENIED;
        default:
            return s.gate == GATE_OPEN ? ST_OK : ST_DENIED;
    }
}

static uint32_t crc32_byte(uint32_t crc, uint8_t b) {
    crc ^= b;
    for (uint8_t k = 8; k; k--) {
        crc = (crc >> 1) ^ (crc & 1 ? 0xEDB88320UL : 0);
    }
    return crc;
}

// The reply goes back in the request's own buffer: the 16U2's stack has no room for a
// second one (QMK PROGRESS 2026-10-19).
void raw_hid_receive(uint8_t *data, uint8_t length) {
    uint8_t cmd = data[0];
    if (cmd < CMD_INFO || cmd > CMD_AP_WRITE) {
        data[0] = 0xFF;
        raw_hid_send(data, length);
        return;
    }
    s.t_cmd = timer_read();

    uint32_t addr, arg;
    memcpy(&addr, &data[2], 4);
    memcpy(&arg, &data[8], 4);
    uint8_t  n     = data[6];
    uint16_t words = n | (uint16_t)data[7] << 8;
    uint32_t v     = 0;

    uint8_t st = gate_allows(cmd);
    if (!st && cmd >= CMD_MEM_READ && cmd != CMD_RESET && !s.connected) {
        st = ST_NOT_CONNECTED;
    }
    if (!st) {
        switch (cmd) {
            case CMD_INFO:
                data[3] = SWD_PROTOCOL_VERSION;
                data[4] = s.gate;
                data[5] = (s.on || swd_wired() ? INFO_WIRED : 0) | (s.connected ? INFO_CONNECTED : 0) | (s.touched ? INFO_TOUCHED : 0)
#ifdef MM_SWD_BRIDGE_NO_BUTTON
                          | INFO_NO_BUTTON
#endif
                    ;
                data[6] = s.result;
                break;

            case CMD_CONNECT:
                st      = bridge_connect();
                v       = swd_data;
                data[7] = !st && nrf_ahb_open();
                break;

            case CMD_UNLOCK:
                st = cmd_unlock();
                break;

            case CMD_RELEASE:
                bridge_end();
                break;

            case CMD_MEM_READ:
                if (n < 1 || n > 7) {
                    st = ST_BAD_ARGS;
                    break;
                }
                data[3] = n;
                for (uint8_t *p = &data[4]; n && !st; n--, p += 4, addr += 4) {
                    st = swd_status(swd_mem(addr, true));
                    memcpy(p, &swd_data, 4);
                }
                break;

            case CMD_MEM_WRITE:
                if (n < 1 || n > 6) {
                    st = ST_BAD_ARGS;
                    break;
                }
                // RAM writes (an RTT reader moving RdOff) don't oblige a reset at the end;
                // a halt, a peripheral or the flash does.
                if (data[5] != NRF_RAM_TOP_BYTE) {
                    s.touched = true;
                }
                for (uint8_t *p = &data[7]; n && !st; n--, p += 4, addr += 4) {
                    memcpy(&swd_data, p, 4);
                    st = swd_status(swd_mem(addr, false));
                }
                if (!st) st = swd_status(swd_mem_end());
                break;

            case CMD_MEM_CRC32:
                if (words < 1 || words > 256) {
                    st = ST_BAD_ARGS;
                    break;
                }
                arg = ~arg;
                for (; words && !st; words--, addr += 4) {
                    st = swd_status(swd_mem(addr, true));
                    v  = swd_data;
                    for (uint8_t i = 0; i < 4; i++, v >>= 8) {
                        arg = crc32_byte(arg, (uint8_t)v);
                    }
                }
                v = ~arg;
                break;

            case CMD_ERASEALL:
                st = cmd_eraseall();
                break;

            case CMD_RESET:
                swd_detach();
                nrf_pin_reset();
                break;

            case CMD_AP_READ:
                st = swd_status(swd_ap(data[2], data[3], true));
                v  = swd_data;
                break;

            case CMD_AP_WRITE:
                // Through the AHB-AP this can halt the core or write flash, so as with
                // MEM_WRITE outside RAM, ending the session resets the nRF.
                s.touched = true;
                memcpy(&swd_data, &data[4], 4);
                st = swd_status(swd_ap(data[2], data[3], false));
                break;
        }
    }
    // CONNECT, MEM_CRC32 and AP_READ answer in [3..6].
    if (cmd == CMD_MEM_CRC32 || cmd == CMD_AP_READ || cmd == CMD_CONNECT) {
        memcpy(&data[3], &v, 4);
    }
    data[2] = st;
    raw_hid_send(data, length);
}
