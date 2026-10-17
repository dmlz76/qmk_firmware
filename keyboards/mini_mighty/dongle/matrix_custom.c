#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "matrix.h"
#include "gpio.h"
#include "wait.h"
#include "debug.h"
#include "print.h"
#include "report.h"
#include "os_detection.h"
#include "host.h"
#include <assert.h>

#define USE_SPI_IRQ (defined(__AVR__) || defined(__INTELLISENSE__))
#if USE_SPI_IRQ
#include <avr/interrupt.h>
#include "atomic_util.h"
#endif

#include "transfer_blob.h"
#include "peer_battery.h"

#define RST_PIN D4

#define CS_PIN B0
#define SCK_PIN B1
#define MOSI_PIN B2
#define MISO_PIN B3

static report_keyboard_t keyboard_report = {};
#ifdef MOUSE_ENABLE
static report_mouse_t mouse_report = {};
#endif

#define MANUAL_POWER_ON_RESET 0
#define SPI_READ_ONLY 1

// 1: the nRF follows every blob with transfer_blob_crc8() of it, so a frame is
// TRANSFER_DONGLE_FRAME_LEN bytes and one that fails the check is dropped. 0: bare
// 8-byte blobs, for running against nRF firmware from before the CRC (CS framing still
// applies). Both ends have to agree, or every frame is dropped for its length.
#ifndef SPI_FRAME_CRC
#    define SPI_FRAME_CRC 1
#endif
#if SPI_FRAME_CRC
#    define SPI_FRAME_LEN TRANSFER_DONGLE_FRAME_LEN
#else
#    define SPI_FRAME_LEN sizeof(transfer_blob_t)
#endif

#if USE_SPI_IRQ
// A ring of whole frames, not bytes. A frame is whatever was clocked in during one CS
// assertion, and only an exact-length one is committed, so a dropped byte, an extra one
// or a stray SCK edge costs that one frame. With a byte ring, which this replaces, it
// shifted every frame after it until reset. Power of two; holds FRAME_COUNT - 1 frames,
// the same 7 blobs the 64-byte ring held.
//
// Main only reads the ring inside ATOMIC_BLOCK, so the ISRs may rearrange it, read
// index included, when it overflows (spi_frame_overflow()).
#define FRAME_COUNT 8
#define FRAME_NEXT(x) (((x) + 1) & (FRAME_COUNT - 1))
#define FRAME_PREV(x) (((x) - 1) & (FRAME_COUNT - 1))

static volatile uint8_t g_frames[FRAME_COUNT * SPI_FRAME_LEN]; // one flat array: see spi_frame()
static volatile uint8_t g_frame_read = 0;  // oldest frame; main advances it, and so does an overflow
static volatile uint8_t g_frame_write = 0; // written by the ISRs only; the slot being filled
static volatile uint8_t g_frame_len = 0;   // bytes clocked in this CS assertion, saturating
static volatile uint8_t g_frame_hwm = 0;   // most frames ever queued at once; FRAME_COUNT - 1 is full

// Frames dropped, mod 256: wrong length (a lost or extra byte), or discarded to make
// room in a full ring. Main loop reports changes; see spi_report_drops().
static volatile uint8_t g_drops_len = 0;
static volatile uint8_t g_drops_full = 0;

// Slot s of g_frames. One flat array, indexed in 8 bits with a shift and an add,
// because the 16U2 has no hardware multiplier: as a [FRAME_COUNT][SPI_FRAME_LEN] array,
// GCC called __mulhi3 for the 9-byte stride on every byte SPI_STC_vect took. That
// cost ~50 cycles of the 512 a byte allows at 8 MHz, plus the registers a call makes
// an ISR save.
static inline volatile uint8_t *spi_frame(uint8_t s) {
    uint8_t at = (uint8_t)(s << 3);
    // Hides `at` from the optimizer. Otherwise it folds the shift and the add back
    // into s * 9 and, at -Os, calls __mulqi3 for that.
    __asm__("" : "+r"(at));
    if (SPI_FRAME_LEN == 9) {
        at += s;
    }
    return &g_frames[at];
}
static_assert(SPI_FRAME_LEN == 8 || SPI_FRAME_LEN == 9, "spi_frame() only knows strides of 8 and 9");
static_assert(FRAME_COUNT * SPI_FRAME_LEN <= 256, "spi_frame() indexes in 8 bits");
#endif
#if SPI_FRAME_CRC
static uint8_t s_drops_crc = 0; // main loop only
#endif

void spi_init_slave(void)
{
    // https://www.electronicwings.com/avr-atmega/atmega1632-spi

    gpio_set_pin_input(CS_PIN);
    gpio_set_pin_input(SCK_PIN);
    gpio_set_pin_input(MOSI_PIN);
#if SPI_READ_ONLY
    gpio_set_pin_input(MISO_PIN);
#else
    gpio_set_pin_output(MISO_PIN);
#endif

    // enable SPI
#if USE_SPI_IRQ
    // CS edges delimit frames (PCINT0_vect). CS_PIN is PB0, which is PCINT0. Clear any
    // edge latched before now, so the first frame isn't judged on a stale one; a frame
    // already in flight when this runs is dropped for its length, as it should be.
    PCMSK0 |= _BV(PCINT0);
    PCIFR = _BV(PCIF0);
    PCICR |= _BV(PCIE0);

    SPCR = _BV(SPE) | _BV(SPIE);
    sei();
#else
    SPCR = _BV(SPE);
#endif
}

#if !USE_SPI_IRQ
static inline bool spi_selected(void) {
    return !gpio_read_pin(CS_PIN);
}
#endif

// Rejects a blob whose CRC byte doesn't match, counting it; true when the blob is good.
static inline bool spi_frame_check(const transfer_blob_t *blob, uint8_t crc) {
#if SPI_FRAME_CRC
    if (crc != transfer_blob_crc8(blob)) {
        s_drops_crc++;
        return false;
    }
#else
    (void)blob;
    (void)crc;
#endif
    return true;
}

#if USE_SPI_IRQ
static inline __attribute__((always_inline)) void spi_frame_take(uint8_t byte) {
    uint8_t len = g_frame_len;
    if (len < SPI_FRAME_LEN) {
        spi_frame(g_frame_write)[len] = byte;
    }
    if (len != UINT8_MAX) {
        g_frame_len = len + 1;
    }
}

ISR (SPI_STC_vect)
{
#if !SPI_READ_ONLY
    SPDR = 0x0;
#endif
    spi_frame_take(SPDR);
}

// Which input stream a frame belongs to, as one bit for spi_frame_overflow(). 0 for a
// frame it may always drop: a 'B' (each peripheral resends its level every 2 min), or
// a type byte that isn't one. Not a switch: GCC turns that into a lookup table, and on
// the AVR .rodata takes RAM.
#define FRAME_STREAMS 4
static inline uint8_t spi_frame_stream(uint8_t type) {
    return type == 'K' ? 1 : type == 'M' ? 2 : type == 'C' ? 4 : type == 'S' ? 8 : 0;
}
static_assert(FRAME_COUNT > FRAME_STREAMS, "a full ring plus the arrival must hold two frames of one stream");

static inline __attribute__((always_inline)) void spi_frame_move(uint8_t to, uint8_t from) {
    volatile uint8_t *d = spi_frame(to);
    volatile uint8_t *s = spi_frame(from);
    // Unrolled: as a loop, GCC rebuilds both 16-bit addresses for every byte, ~16 cycles
    // a byte against ~4 here.
#pragma GCC unroll 16
    for (uint8_t i = 0; i < SPI_FRAME_LEN; i++) {
        d[i] = s[i];
    }
}

// The ring is full, and a whole frame has just landed in g_frame_write. One frame has to
// go, and it must never be the newest of its stream. 'K', 'C' and 'S' frames are
// absolute state, and so are an 'M' frame's buttons, so the newest of each is the one
// that releases whatever the host is holding. Dropping the arrival, as this used to,
// loses exactly that at the end of a burst and leaves a key in auto-repeat. Instead,
// scanning back from the arrival, it takes:
//   1. the first frame whose loss costs nothing that matters: a 'B', a frame with no
//      valid type, or an 'M' with the same buttons as the next newer 'M' (only its
//      motion is lost). A burst of 'M' frames, the usual way the ring fills, has one
//      within two frames of the arrival;
//   2. failing that, the oldest frame with a newer one in its stream. That loses an
//      intermediate state, a tap at worst, and nothing is left held.
// 8 frames, the arrival included, over 4 streams, so 2 always finds one. The frames
// left keep their order. Worst case, with SPI_STC_vect held off: a scan of 8 frames and
// 3 frame moves, toward whichever end of the ring is nearer.
static inline void spi_frame_overflow(void) {
    uint8_t read = g_frame_read, write = g_frame_write;

    // Newest to oldest, so `seen` holds the streams of every frame newer than slot s.
    // The arrival is the fallback victim, though rule 2 can't come up empty.
    uint8_t victim = write, seen = 0;
    int16_t newer_buttons = -1; // of the next newer 'M'; -1 for none yet
    for (uint8_t s = write;; s = FRAME_PREV(s)) {
        volatile uint8_t *f      = spi_frame(s);
        uint8_t           type   = f[0];
        uint8_t           stream = spi_frame_stream(type);
        if (stream == 0) {
            victim = s;
            break;
        }
        if (type == 'M') {
            uint8_t buttons = f[offsetof(transfer_blob_t, m.buttons)];
            if (buttons == newer_buttons) {
                victim = s;
                break;
            }
            newer_buttons = buttons;
        }
        if (seen & stream) {
            victim = s; // superseded: rule 2's pick so far, moving back to the oldest
        }
        seen |= stream;
        if (s == read) {
            break;
        }
    }
    g_drops_full++;

    if (victim == write) {
        return; // the arrival itself; the next frame fills its slot again
    }
    if (((victim - read) & (FRAME_COUNT - 1)) <= ((write - victim) & (FRAME_COUNT - 1))) {
        // Nearer the oldest end: move the older frames up over the victim, then commit.
        for (uint8_t s = victim; s != read; s = FRAME_PREV(s)) {
            spi_frame_move(s, FRAME_PREV(s));
        }
        g_frame_read  = FRAME_NEXT(read);
        g_frame_write = FRAME_NEXT(write);
    } else {
        // Nearer the arrival: move the newer frames, the arrival included, down over it.
        // That leaves the arrival just behind g_frame_write, so it's already committed.
        for (uint8_t s = victim; s != write; s = FRAME_NEXT(s)) {
            spi_frame_move(s, FRAME_NEXT(s));
        }
    }
}

// CS changed level. Bytes only clock in while CS is low, so on either edge the
// transaction before it is over: commit it if it was exactly one frame. A falling edge
// normally finds nothing (the rising edge before it committed and reset), but it also
// covers a rising edge this ISR ran too late to tell apart, with CS already low again.
ISR (PCINT0_vect)
{
    // This vector (9) outranks SPI_STC_vect (22). If the last byte completed and CS then
    // rose while interrupts were held off, both are pending and this runs first, so take
    // that byte here. Reading SPSR with SPIF set and then SPDR clears SPIF, so
    // SPI_STC_vect doesn't take it a second time.
    if (SPSR & _BV(SPIF)) {
        spi_frame_take(SPDR);
    }

    uint8_t len = g_frame_len;
    if (len == SPI_FRAME_LEN) {
        uint8_t read = g_frame_read;
        uint8_t next = FRAME_NEXT(g_frame_write);
        if (next != read) {
            g_frame_write = next;
            uint8_t depth = (next - read) & (FRAME_COUNT - 1);
            if (depth > g_frame_hwm) {
                g_frame_hwm = depth;
            }
        } else {
            spi_frame_overflow();
        }
    } else if (len != 0) {
        g_drops_len++;
    }
    g_frame_len = 0;
}

// Copies frame `slot` out: the blob into *blob, and returns the CRC byte after it.
// Unrolled, like spi_frame_move(): callers run it with interrupts off.
static inline uint8_t spi_frame_copy_out(uint8_t slot, transfer_blob_t *blob) {
    volatile uint8_t *f = spi_frame(slot);
#pragma GCC unroll 16
    for (uint8_t i = 0; i < sizeof(blob->raw); i++) {
        blob->raw[i] = f[i];
    }
    return f[SPI_FRAME_LEN - 1];
}

bool spi_receive(transfer_blob_t *blob) {
    bool    have = false;
    uint8_t crc  = 0;
    // Atomic, or an overflow could move frames under the copy.
    ATOMIC_BLOCK_FORCEON {
        uint8_t read = g_frame_read;
        if (read != g_frame_write) {
            crc = spi_frame_copy_out(read, blob);
            // Handed back before checking: a bad frame is consumed like a good one.
            g_frame_read = FRAME_NEXT(read);
            have         = true;
        }
    }
    return have && spi_frame_check(blob, crc);
}

// x + d stays inside the 8-bit mouse report's -127..127. The blob's fields are int8
// whatever the report's width.
static inline bool spi_motion_fits(int8_t x, int8_t d) {
    int16_t sum = (int16_t)x + d;
    return sum >= INT8_MIN + 1 && sum <= INT8_MAX;
}

// Pops the oldest frame and adds its motion into *m, but only if it's an 'M' with m's
// buttons and the sums still fit. It's looked at and popped in one atomic section, so
// an overflow can't swap the oldest frame in between. One that then fails its CRC is
// consumed, counted, and ends the merge.
static bool spi_receive_motion(transfer_blob_t *m) {
    transfer_blob_t next;
    bool            popped = false;
    uint8_t         crc    = 0;
    ATOMIC_BLOCK_FORCEON {
        uint8_t read = g_frame_read;
        if (read != g_frame_write) {
            crc = spi_frame_copy_out(read, &next);
            if (next.type == 'M' && next.m.buttons == m->m.buttons && spi_motion_fits(m->m.x, next.m.x) && spi_motion_fits(m->m.y, next.m.y) && spi_motion_fits(m->m.v, next.m.v)) {
                g_frame_read = FRAME_NEXT(read);
                popped       = true;
            }
        }
    }
    if (!popped || !spi_frame_check(&next, crc)) {
        return false;
    }
    m->m.x += next.m.x;
    m->m.y += next.m.y;
    m->m.v += next.m.v;
    return true;
}

// Logs whenever a drop counter or the high-water mark has moved since the last call.
// uprintf, not dprintf, so these still print with debug_enable off. Console builds only,
// in effect: uprintf is empty otherwise.
static void spi_report_drops(void) {
    static uint8_t s_len, s_full, s_hwm;
    uint8_t len = g_drops_len, full = g_drops_full, hwm = g_frame_hwm;
    if (len != s_len || full != s_full) {
        uprintf("spi dropped: length %u, ring full %u (mod 256)\n", len, full);
        s_len = len;
        s_full = full;
    }
    if (hwm != s_hwm) {
        uprintf("spi ring: high water %u of %u\n", hwm, FRAME_COUNT - 1);
        s_hwm = hwm;
    }
#if SPI_FRAME_CRC
    static uint8_t s_crc;
    if (s_drops_crc != s_crc) {
        uprintf("spi dropped: crc %u (mod 256)\n", s_drops_crc);
        s_crc = s_drops_crc;
    }
#endif
}
#else
uint8_t spi_read(void) {
#if !SPI_READ_ONLY
    SPDR = 0x0;
#endif
    while (!(SPSR & _BV(SPIF))) {
    }
    return SPDR;
}
// Shifts one frame in while CS is asserted. No length check beyond SPI_FRAME_LEN: a
// polled master has no ISR to miss bytes.
bool spi_receive(transfer_blob_t *blob) {
    if (!spi_selected()) {
        return false;
    }
    for (uint8_t i = 0; i < sizeof(blob->raw); i++) {
        blob->raw[i] = spi_read();
    }
    uint8_t crc = SPI_FRAME_CRC ? spi_read() : 0;
    return spi_frame_check(blob, crc);
}

// No ring to look ahead in, so nothing to merge.
static bool spi_receive_motion(transfer_blob_t *m) {
    (void)m;
    return false;
}

static void spi_report_drops(void) {}
#endif

matrix_row_t matrix_get_row(uint8_t row) {
    return 0;
}

void matrix_print(void) {
}

void matrix_init(void) {
    spi_init_slave();

    gpio_set_pin_output(RST_PIN);
    gpio_write_pin_high(RST_PIN);

#if MANUAL_POWER_ON_RESET
    // Perform a hardware reset
    gpio_write_pin_low(RST_PIN);
    wait_ms(100);
    gpio_write_pin_high(RST_PIN);

    wait_ms(1000); // Give it a second to initialize
#endif

    // This *must* be called for correct keyboard behavior
    matrix_init_kb();
}

uint8_t matrix_scan(void) {
    bool m_changed = false;
    bool k_changed = false;
#ifdef EXTRAKEY_ENABLE
    bool     c_changed = false;
    bool     s_changed = false;
    uint16_t usage     = 0;
#endif

#ifdef SPI_TEST_STALL_MS
    // Bench test of the ring's overflow handling only: stalling every scan makes any
    // burst fill the ring. Build with -e EXTRAFLAGS=-DSPI_TEST_STALL_MS=50. Never ship it.
    wait_ms(SPI_TEST_STALL_MS);
#endif

    // One parse for both transports. The IRQ path pulls a whole frame out of
    // the ring the ISRs fill; the polled path shifts it in while CS is
    // asserted. Only the acquisition differs, so only spi_receive() and
    // spi_receive_motion() are #if'd.
    // Either way a blob that fails its CRC never reaches the switch.
    transfer_blob_t blob;
    bool            have_blob = spi_receive(&blob);
    spi_report_drops();

    if (have_blob) {
        dprintf("spi received: type %d\n", blob.type);
        switch (blob.type) {
            case 'M': {
                // Fold in the 'M' frames queued right behind this one with the same
                // buttons. A drain held up by a slow host or the console would otherwise
                // spend a USB report on each, and that's what fills the ring. Only motion
                // is summed, so nothing is lost, and a button change always gets its own
                // report.
                uint8_t merged = 0;
                while (spi_receive_motion(&blob)) {
                    merged++;
                }
                dprintf("spi received: buttons %02X, x %d, y %d, v %d, merged %u\n", blob.m.buttons, blob.m.x, blob.m.y, blob.m.v, merged);
                m_changed = true;
                break;
            }
            case 'K':
                dprintf("spi received: mods %02X, keys %d,%d,%d,%d,%d,%d\n", blob.k.mods, blob.k.keys[0], blob.k.keys[1], blob.k.keys[2], blob.k.keys[3], blob.k.keys[4], blob.k.keys[5]);
                k_changed = true;
                break;
#ifdef EXTRAKEY_ENABLE
            case 'C':
            case 'S':
                usage = (uint16_t)blob.e.usage_lo | ((uint16_t)blob.e.usage_hi << 8);
                dprintf("spi received: extra usage %04X\n", usage);
                if (blob.type == 'C') {
                    c_changed = true;
                } else {
                    s_changed = true;
                }
                break;
#endif
            case 'B':
                // Not input, so it doesn't count as a change: that return value is what
                // wakes a suspended host.
                dprintf("spi received: battery peer %d kind %c level %d\n", blob.b.peer, blob.b.kind, blob.b.level);
                peer_battery_update(blob.b.peer, blob.b.kind, blob.b.level);
                break;
        }
    }

    if (k_changed) {
        os_variant_t os = detected_host_os();
        uint8_t mods = blob.k.mods;
        if (os == OS_MACOS || os == OS_IOS) {
            // Swap the Command and Option keys for Apple devices
            bool cmd = mods & 4;
            bool opt = mods & 8;
            mods &= ~(4 | 8);
            mods |= cmd ? 8 : 0;
            mods |= opt ? 4 : 0;
        }
        keyboard_report.mods = mods;

        static_assert(KEYBOARD_REPORT_KEYS == 6, "Expected 6 keys in keyboard report");
        for (uint8_t i = 0; i < KEYBOARD_REPORT_KEYS; i++) {
            keyboard_report.keys[i] = blob.k.keys[i];
        }
        host_keyboard_send(&keyboard_report);
    }

#ifdef MOUSE_ENABLE
    if (m_changed) {
        mouse_report.buttons = blob.m.buttons;
        mouse_report.x = blob.m.x;
        mouse_report.y = blob.m.y;
        mouse_report.v = blob.m.v;
        host_mouse_send(&mouse_report);
    }
#endif

#ifdef EXTRAKEY_ENABLE
    // No local state to mirror here the way keyboard_report/mouse_report do:
    // the extra-key protocol is one usage at a time, and usage 0 is the
    // release of whatever was held. host_consumer_send()/host_system_send()
    // dedupe against the previous usage themselves, so forward every blob
    // as-is, zeros included.
    if (c_changed) {
        host_consumer_send(usage);
    }
    if (s_changed) {
        host_system_send(usage);
    }
#endif

    peer_battery_task();

    // This *must* be called for correct keyboard behavior
    matrix_scan_kb();

    uint8_t changed = m_changed | k_changed;
#ifdef EXTRAKEY_ENABLE
    changed |= c_changed | s_changed;
#endif
    return changed;
}

__attribute__((weak)) void matrix_init_kb(void) { matrix_init_user(); }

__attribute__((weak)) void matrix_scan_kb(void) { matrix_scan_user(); }

__attribute__((weak)) void matrix_init_user(void) {}

__attribute__((weak)) void matrix_scan_user(void) {}
