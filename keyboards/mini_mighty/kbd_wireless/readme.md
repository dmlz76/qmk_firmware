# mini_mighty_kbd_wireless

![mini_mighty_kbd_wireless]()

![pcb]()

A low profile, small form factor, wireless mechanical keyboard with hot-swappable switches, featuring a fully 3D printable enclosure and keycaps with an open source design, offering endless possibilities for customization and creativity.
Measuring just 256x?x17 mm (10×?×0.675 inches), mini·mighty·kbd-wireless is one of the thinnest and smallest keyboards on the market. It has the functionality of a 75% keyboard, packed in the size of a 60% keyboard.

* Keyboard Maintainer: [dimitrix-llc](https://github.com/dimitrix-llc)
* Hardware Supported: mini·mighty·kbd-wireless v?.? or later PCB
* Hardware Availability: https://dimitrix.llc/keyboards

Build (after setting up your build environment):

    qmk compile -kb mini_mighty/kbd_wireless -km default

Flash (enter the bootloader first — see below):

    qmk flash -kb mini_mighty/kbd_wireless -km default

> Build with the `qmk` CLI, not a bare `make`. `qmk` uses QMK's managed toolchain
> (avr-gcc 15.x); a bare `make` uses whatever `avr-gcc` is first on your `PATH`, and
> an older one (e.g. Homebrew avr-gcc 8.x) emits larger code that can overflow flash.
> Flashing these atmega boards needs `dfu-programmer` installed.

See the [build environment setup](https://docs.qmk.fm/#/getting_started_build_tools) and the [make instructions](https://docs.qmk.fm/#/getting_started_make_guide) for more information. Brand new to QMK? Start with our [Complete Newbs Guide](https://docs.qmk.fm/#/newbs).

## Bootloader

Enter the bootloader:

* **Bootmagic reset**: Hold down the ESC key and plug in the keyboard
* **Hardware (for dead firmware)**: Hold the column-side pin of any Col_7 switch (F7, 7, U, J or N) to GND, briefly short the "RESET" pad, then release the switch pin. Pressing those keys does nothing: the matrix diodes block the path to ground. HWB (PE2) doubles as Col_7 and has no pull-up, so shorting RESET alone usually restarts the firmware but can occasionally land in DFU; power-cycle to leave it.
