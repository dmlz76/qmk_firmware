# mini_mighty_mouse

![mini_mighty_mouse](imgur.com image replace me!)

*A short description of the keyboard/project*

* Mouse Maintainer: [dmlz76](https://github.com/dmlz76)
* Hardware Supported: *The PCBs, controllers supported*
* Hardware Availability: dimitrix.llc

Build the PCB revision you have (after setting up your build environment):

    qmk compile -kb mini_mighty/mouse/rev11 -km default
    qmk compile -kb mini_mighty/mouse/rev12 -km default

Flash (enter the bootloader first — see below):

    qmk flash -kb mini_mighty/mouse/rev12 -km default

> Pick a revision — the bare `mini_mighty/mouse` target is a parent and is not
> buildable (it has no matrix). Build with the `qmk` CLI, not a bare `make`: `qmk`
> uses QMK's managed toolchain (avr-gcc 15.x); a bare `make` uses whatever `avr-gcc`
> is first on your `PATH`, and an older one (e.g. Homebrew avr-gcc 8.x) emits larger
> code that overflows this near-full atmega16u2. Flashing needs `dfu-programmer`.

See the [build environment setup](https://docs.qmk.fm/#/getting_started_build_tools) and the [make instructions](https://docs.qmk.fm/#/getting_started_make_guide) for more information. Brand new to QMK? Start with our [Complete Newbs Guide](https://docs.qmk.fm/#/newbs).

## Bootloader

Enter the bootloader:

* **Bootmagic reset**: Hold down the left mouse and plug in the mouse
* **Physical reset pad**: Briefly short the "RESET" pad on the back of the PCB
