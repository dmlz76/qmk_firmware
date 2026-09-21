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
                int8_t  v; // Vertical scroll
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
        };
    };
    uint8_t raw[8];
} transfer_blob_t;

TRANSFER_STATIC_ASSERT(sizeof(transfer_blob_t) == 8, "blob frame must stay 8 bytes");
TRANSFER_STATIC_ASSERT(offsetof(transfer_blob_t, k.mods) == 1, "payload must start at offset 1; a padded union would break the wire format");

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
