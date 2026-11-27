# mini_mighty_mouse

![mini_mighty_mouse](imgur.com image replace me!)

*A short description of the keyboard/project*

* Mouse Maintainer: [dmlz76](https://github.com/dmlz76)
* Hardware Supported: *The PCBs, controllers supported*
* Hardware Availability: dimitrix.llc

Build the PCB revision you have (after setting up your build environment):

    qmk compile -kb mini_mighty/mouse/rev1_1_0 -km default  # PCB v1.1.0 / v1.2.0 (ATmega16U2)
    qmk compile -kb mini_mighty/mouse/rev1_3_0 -km default  # PCB v1.3.0 (ATmega32U2)

Flash (enter the bootloader first — see below):

    qmk flash -kb mini_mighty/mouse/rev1_3_0 -km default

> Pick a revision — the bare `mini_mighty/mouse` target is a parent and is not
> buildable (it has no matrix). Build with the `qmk` CLI, not a bare `make`: `qmk`
> uses QMK's managed toolchain (avr-gcc 15.x); a bare `make` uses whatever `avr-gcc`
> is first on your `PATH`, and an older one (e.g. Homebrew avr-gcc 8.x) emits larger
> code that overflows the near-full atmega16u2 of rev1_1_0. Flashing needs `dfu-programmer`.

## The nRF (PCB v1.3.0)

From PCB v1.3.0 the AVR also has SWD to the wireless module, so its nRF52805 can be
flashed, and its RTT log read, over the mouse's own USB, with no J-Link:

    keyboards/mini_mighty/tools/mm_swd.py info
    keyboards/mini_mighty/tools/mm_swd.py flash mini_mighty_mouse_nrf52805_s112.hex
    keyboards/mini_mighty/tools/mm_swd.py flash s112_nrf52_7.2.0_softdevice.hex mini_mighty_mouse_nrf52805_s112.hex

The tool asks for a press of the mouse's erase-bonds button before it can read or write
the nRF, and the wireless switch must be ON. An app update keeps the BLE bonds and the
pairing with the dongle. A module that has to be erased whole (new, or locked) needs the
SoftDevice's .hex as well, as in the last line. The bridge needs raw HID, which leaves no
endpoint for the console, so `-e CONSOLE_ENABLE=yes` builds go without it.

See the [build environment setup](https://docs.qmk.fm/#/getting_started_build_tools) and the [make instructions](https://docs.qmk.fm/#/getting_started_make_guide) for more information. Brand new to QMK? Start with our [Complete Newbs Guide](https://docs.qmk.fm/#/newbs).

## Bootloader

Enter the bootloader:

* **Bootmagic reset**: Hold down the left mouse and plug in the mouse
* **Physical reset pad (PCB v1.1.0 and later)**: Briefly short the "RESET" pad. HWB is pulled to GND on these boards, so a RESET-pad reset always enters DFU; power-cycle to restart the firmware instead.
