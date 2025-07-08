#pragma once

typedef union
{
    struct
    {
        uint8_t buttons;
        int8_t x; // Horizontal movement
        int8_t y; // Vertical movement
        int8_t v; // Vertical scroll
    };    
    uint8_t raw[4]; 
} transfer_blob_t;
