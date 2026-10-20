#pragma once

#include <inttypes.h>
#include <stddef.h>

#ifdef __cplusplus
#    define TRANSFER_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#    define TRANSFER_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

typedef union {
    struct {
        uint8_t type;
        union {
            struct {
                uint8_t buttons;
                int8_t  x; // Horizontal movement
                int8_t  y; // Vertical movement
                int8_t  v; // Vertical scroll, in whole notches
                // The same scroll in 1/120 notch, int16 little-endian, for a host that
                // has turned on the HID Resolution Multiplier. Two bytes rather than an
                // int16_t for the alignment reason given at `e` below. Valid only with
                // TRANSFER_MOUSE_HAS_V_HR in flags: read it with transfer_mouse_v_hr(),
                // never directly.
                uint8_t v_hr_lo;
                uint8_t v_hr_hi;
                uint8_t flags; // TRANSFER_MOUSE_*
            } m;
            struct {
                uint8_t mods;
                uint8_t keys[6];
            } k;
            // Consumer ('C') and system ('S') usages — the HID "extra keys":
            // volume, mute, brightness, media transport, browser nav, power.
            // QMK routes these through a separate host-driver entry point than
            // the keyboard report (send_extra, not send_keyboard), so they need
            // their own blob type rather than a seat in `k.keys`.
            //
            // The 16-bit usage is split into two bytes on purpose rather than
            // declared as a uint16_t: a uint16_t member would raise this
            // union's alignment to 2, the compiler would insert a pad byte
            // after `type`, and `m`/`k` would slide from offset 1 to offset 2
            // — silently breaking the wire format for every mouse and keyboard
            // blob already in the field. The offsetof assert below is what
            // catches that if anyone tries it anyway.
            //
            // Little-endian on the wire, which is both ends' native order.
            struct {
                uint8_t usage_lo;
                uint8_t usage_hi;
            } e;
            // Battery level of one peripheral ('B'). The peripheral's nRF makes these
            // itself from its own SAADC reading, in GZLL mode only (over BLE the host reads
            // BAS directly), so no AVR ever sends one; only the dongle receives them. The
            // dongle reports the lowest level per kind to the host as HID Battery Strength.
            struct {
                uint8_t kind;  // 'M' or 'K': which of the dongle's two batteries this counts toward.
                uint8_t peer;  // Which peripheral: its GZLL pipe, stamped by the dongle's nRF on
                               // receipt. Peripherals send 0.
                uint8_t level; // 0..100 %, or TRANSFER_BATTERY_NONE.
            } b;
        };
    };
    uint8_t raw[8];
} transfer_blob_t;

// 'B' level when there is no cell to measure: the cutoff switch is off or no cell is fitted,
// so the peripheral is running on external power. It drops out of the dongle's minimum
// rather than reporting as empty.
#define TRANSFER_BATTERY_NONE 0xFF

TRANSFER_STATIC_ASSERT(sizeof(transfer_blob_t) == 8, "blob frame must stay 8 bytes");
TRANSFER_STATIC_ASSERT(offsetof(transfer_blob_t, k.mods) == 1, "payload must start at offset 1; a padded union would break the wire format");

// KEEP IN SYNC WITH THE nRF FIRMWARE
// (mini_mighty_common/mini_mighty_spi_transfer.h in ~/repos/mini_mighty_nrf52805)
//
// An 'M' blob's wheel travel comes in two units. The mouse AVR fills both from the same
// notch: m.v from its whole-notch curve, and m.v_hr from its hi-res one, which lets a slow
// notch scroll less than a whole one. Whichever end knows the host's HID Resolution
// Multiplier picks one: the mouse's nRF over BLE, the dongle's AVR over GZLL.
//
// TRANSFER_MOUSE_HAS_V_HR in m.flags says m.v_hr is filled in. An AVR from before hi-res,
// or built without it, zeroes the spare bytes, so the flag is clear and the scroll reads as
// m.v whole notches. With the flag set, a v_hr of 0 really is no scroll, so a merge is a
// plain sum: v + v, and transfer_mouse_v_hr() of both stored with transfer_mouse_set_v_hr().
// That holds whether or not either blob had the flag.
#define TRANSFER_WHEEL_HIRES_PER_NOTCH 120
#define TRANSFER_MOUSE_HAS_V_HR 0x01 // m.flags

TRANSFER_STATIC_ASSERT(offsetof(transfer_blob_t, m.flags) == 7, "the 'M' payload must end at the frame's last byte");

static inline int16_t transfer_mouse_v_hr(const transfer_blob_t *blob) {
    if (blob->m.flags & TRANSFER_MOUSE_HAS_V_HR) {
        return (int16_t)((uint16_t)blob->m.v_hr_lo | ((uint16_t)blob->m.v_hr_hi << 8));
    }
    return (int16_t)(blob->m.v * TRANSFER_WHEEL_HIRES_PER_NOTCH);
}

static inline void transfer_mouse_set_v_hr(transfer_blob_t *blob, int16_t v_hr) {
    blob->m.v_hr_lo = (uint8_t)v_hr;
    blob->m.v_hr_hi = (uint8_t)((uint16_t)v_hr >> 8);
    blob->m.flags |= TRANSFER_MOUSE_HAS_V_HR;
}

// KEEP IN SYNC WITH THE nRF FIRMWARE
// (mini_mighty_common/mini_mighty_spi_transfer.h in ~/repos/mini_mighty_nrf52805)
//
// Status the nRF shifts out on MISO while we shift a blob in on MOSI. SPI is
// full duplex, so this rides inside the same 8 byte-times the blob already
// costs: no extra clocks, no extra transaction, no change to transfer time.
// It exists because the master otherwise has no way to tell whether a blob
// landed — the nRF's SPIS silently ignores anything clocked while it is
// unarmed, and spi_write()'s own status only reports that the *AVR* finished
// shifting bits out.
//
// Byte n of the status shifts out during byte n of the blob, so raw[0] is
// free_slots and raw[1] is accepted.
typedef union {
    struct {
        uint8_t free_slots;  // nRF ring slots free as of its last re-arm, 0..63.
        uint8_t accepted;    // Rolling count (mod 256) of blobs taken into that
                             // ring. Advances on coalesced blobs too — those
                             // were accepted, just merged; free_slots is what
                             // says to back off. Restarts at 0 on every nRF
                             // reset, so it has to be re-baselined after one.
        uint8_t reserved[6]; // Zero. Room to grow without resizing the frame.
    } s;
    uint8_t raw[8];
} transfer_status_t;

// What we read on MISO when the nRF is NOT listening: the SPIS clocks its DEF
// character (NRFX_SPIS_DEFAULT_DEF, 255) out for any transaction that arrives
// while the peripheral is unarmed. free_slots tops out at 63, so 0xFF is
// unambiguous. Before nrfx_spis_init() the nRF does not drive MISO at all, so
// the master pulls the line up to make the boot window read the same way.
#define TRANSFER_STATUS_UNARMED 0xFF

// The status has to cover exactly the byte-times the blob costs — shorter and
// the tail of the frame reads the over-read character, longer and it would cost
// real clocks. The nRF has the matching STATIC_ASSERT.
TRANSFER_STATIC_ASSERT(sizeof(transfer_status_t) == sizeof(transfer_blob_t), "status frame must match the blob frame");

// KEEP IN SYNC WITH THE nRF FIRMWARE (same file as above)
//
// Integrity byte on the dongle's internal hop only. The dongle's nRF (SPIM master) follows
// every blob it forwards with transfer_blob_crc8() of it, inside the same CS assertion, so
// the dongle's AVR (SPI slave) takes TRANSFER_DONGLE_FRAME_LEN bytes per frame and drops a
// frame whose CRC doesn't match. Neither the radio link nor the peripherals' hop (AVR
// master -> nRF SPIS, blob + status) carries it, so nothing changes there.
//
// CRC-8/I-432-1 (the ITU-T I.432.1 / ATM HEC CRC): poly 0x07, init 0x00, MSB first, no
// reflection, xorout 0x55; check("123456789") = 0xA1. Over a 9-byte frame it catches every
// 1-, 2- and 3-bit error, every odd number of flipped bits and every burst up to 8 bits
// (Hamming distance 4 holds up to 119 data bits). The nonzero xorout matters: with it, an
// all-zero frame (MOSI stuck low) fails the check instead of passing as type 0.
#define TRANSFER_DONGLE_FRAME_LEN (sizeof(transfer_blob_t) + 1)

static inline uint8_t transfer_crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
        }
    }
    return crc ^ 0x55;
}

static inline uint8_t transfer_blob_crc8(const transfer_blob_t *blob) {
    return transfer_crc8(blob->raw, sizeof(blob->raw));
}
