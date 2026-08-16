#pragma once

#include <inttypes.h>

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
        };
    };
    uint8_t raw[8];
} transfer_blob_t;

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
#ifdef __cplusplus
static_assert(sizeof(transfer_status_t) == sizeof(transfer_blob_t), "status frame must match the blob frame");
#else
_Static_assert(sizeof(transfer_status_t) == sizeof(transfer_blob_t), "status frame must match the blob frame");
#endif
