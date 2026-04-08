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
