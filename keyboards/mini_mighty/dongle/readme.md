# mini_mighty_dongle

![mini_mighty_dongle](imgur.com image replace me!)

*A short description of the keyboard/project*

* Dongle Maintainer: [dmlz76](https://github.com/dmlz76)
* Hardware Supported: *The PCBs, controllers supported*
* Hardware Availability: dimitrix.llc

Build (after setting up your build environment):

    qmk compile -kb mini_mighty/dongle -km default

Flash (enter the bootloader first — see below):

    qmk flash -kb mini_mighty/dongle -km default

> Build with the `qmk` CLI, not a bare `make`. `qmk` uses QMK's managed toolchain
> (avr-gcc 15.x); a bare `make` uses whatever `avr-gcc` is first on your `PATH`, and
> an older one (e.g. Homebrew avr-gcc 8.x) emits larger code that can overflow flash.
> Flashing these atmega boards needs `dfu-programmer` installed.

See the [build environment setup](https://docs.qmk.fm/#/getting_started_build_tools) and the [make instructions](https://docs.qmk.fm/#/getting_started_make_guide) for more information. Brand new to QMK? Start with our [Complete Newbs Guide](https://docs.qmk.fm/#/newbs).

## Bootloader

Enter the bootloader:

* **Physical reset pad**: Briefly short the "RESET" pad on the back of the PCB
