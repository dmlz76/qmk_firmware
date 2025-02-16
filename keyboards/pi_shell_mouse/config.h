#pragma once

#ifdef POINTING_DEVICE_ENABLE
#    define POINTING_DEVICE_SCLK_PIN C4
#    define POINTING_DEVICE_SDIO_PIN C5
#    define POINTING_DEVICE_MOTION_PIN C6
#    define POINTING_DEVICE_CS_PIN C7
#    define POINTING_DEVICE_INVERT_Y
//#    define POINTING_DEVICE_DEBUG
#endif

#ifdef BLUETOOTH_ENABLE
#   define BLUEFRUIT_LE_RST_PIN D4
#   define BLUEFRUIT_LE_CS_PIN B0
#   define BLUEFRUIT_LE_IRQ_PIN D6
#   define BLUEFRUIT_LE_SCK_DIVISOR 8 // 2MHz SCK/16MHz CPU
#   define BLUEFRUIT_LE_SAMPLE_BATTERY 0
#endif

//#define NO_ACTION_TAPPING
//#define NO_ACTION_ONESHOT
