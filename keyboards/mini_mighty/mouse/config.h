#pragma once

#define ENCODER_RESOLUTION 2

#define MK_KINETIC_SPEED

#ifdef POINTING_DEVICE_ENABLE
#define POINTING_DEVICE_SCLK_PIN C4
#define POINTING_DEVICE_SDIO_PIN C5
#define POINTING_DEVICE_MOTION_PIN C6
#define POINTING_DEVICE_CS_PIN C7
#define POINTING_DEVICE_INVERT_Y
#define POINTING_DEVICE_TASK_THROTTLE_MS 3
#endif

#define NO_ACTION_TAPPING
#define NO_ACTION_ONESHOT
#define NO_ACTION_LAYER

#define USB_SUSPEND_WAKEUP_DELAY 200
#define NO_SUSPEND_POWER_DOWN 1

// Watchdog period used for MCU power-down sleeps. Async wake
// handles button/encoder events regardless of this value, so it only bounds
// the timer drift correction window in WDT_vect. See ble_send_buf.cpp.
#define MCU_POWER_DOWN_WDTO WDTO_1S
