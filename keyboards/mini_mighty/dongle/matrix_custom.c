#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "matrix.h"
#include "gpio.h"
#include "wait.h"
#include "debug.h"
#include "report.h"
#include "os_detection.h"
#include "host.h"
#include <assert.h>

#define USE_SPI_IRQ (defined(__AVR__) || defined(__INTELLISENSE__))
#if USE_SPI_IRQ
#include <avr/interrupt.h>
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
#define FRAME_COUNT 8
#define FRAME_NEXT(x) (((x) + 1) & (FRAME_COUNT - 1))

static volatile uint8_t g_frames[FRAME_COUNT][SPI_FRAME_LEN];
static volatile uint8_t g_frame_read = 0;  // written by main only
static volatile uint8_t g_frame_write = 0; // written by the ISRs only; the slot being filled
static volatile uint8_t g_frame_len = 0;   // bytes clocked in this CS assertion, saturating

// Frames dropped, mod 256: wrong length (a lost or extra byte) or ring full. Main loop
// reports changes; see spi_report_drops().
static volatile uint8_t g_drops_len = 0;
static volatile uint8_t g_drops_full = 0;
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
static inline void spi_frame_take(uint8_t byte) {
    uint8_t len = g_frame_len;
    if (len < SPI_FRAME_LEN) {
        g_frames[g_frame_write][len] = byte;
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
        uint8_t next = FRAME_NEXT(g_frame_write);
        if (next != g_frame_read) {
            g_frame_write = next;
        } else {
            g_drops_full++;
        }
    } else if (len != 0) {
        g_drops_len++;
    }
    g_frame_len = 0;
}

bool spi_receive(transfer_blob_t *blob) {
    uint8_t read = g_frame_read;
    if (read == g_frame_write) {
        return false;
    }
    for (uint8_t i = 0; i < sizeof(blob->raw); i++) {
        blob->raw[i] = g_frames[read][i];
    }
    uint8_t crc = g_frames[read][SPI_FRAME_LEN - 1];
    // The slot is copied out, so hand it back before checking: a bad frame is consumed
    // like a good one.
    g_frame_read = FRAME_NEXT(read);
    return spi_frame_check(blob, crc);
}

// Logs whenever a drop counter has moved since the last call. Console builds only, in
// effect: dprintf is empty otherwise.
static void spi_report_drops(void) {
    static uint8_t s_len, s_full;
    uint8_t len = g_drops_len, full = g_drops_full;
    if (len != s_len || full != s_full) {
        dprintf("spi dropped: length %u, ring full %u (mod 256)\n", len, full);
        s_len = len;
        s_full = full;
    }
#if SPI_FRAME_CRC
    static uint8_t s_crc;
    if (s_drops_crc != s_crc) {
        dprintf("spi dropped: crc %u (mod 256)\n", s_drops_crc);
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

    // One parse for both transports. The IRQ path pulls a whole frame out of
    // the ring the ISRs fill; the polled path shifts it in while CS is
    // asserted. Only the acquisition differs, so only spi_receive() is #if'd.
    // Either way a blob that fails its CRC never reaches the switch.
    transfer_blob_t blob;
    bool            have_blob = spi_receive(&blob);
    spi_report_drops();

    if (have_blob) {
        dprintf("spi received: type %d\n", blob.type);
        switch (blob.type) {
            case 'M':
                dprintf("spi received: buttons %02X, x %d, y %d, v %d\n", blob.m.buttons, blob.m.x, blob.m.y, blob.m.v);
                m_changed = true;
                break;
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
