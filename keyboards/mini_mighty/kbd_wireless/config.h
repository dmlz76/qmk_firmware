#define USB_SUSPEND_WAKEUP_DELAY 200
#define NO_SUSPEND_POWER_DOWN 1

// Debug only: force HID report routing over the wireless (nRF/SPI) path even
// while USB is plugged in, so the wireless link can be exercised while the USB
// console captures logs. Uncomment (or pass -DFORCE_OUTPUT_BLUETOOTH via
// rules.mk OPT_DEFS) to enable; keymap applies it in keyboard_post_init_user().
// Leave off for normal builds (output auto-selects USB when enumerated).
// #define FORCE_OUTPUT_BLUETOOTH
