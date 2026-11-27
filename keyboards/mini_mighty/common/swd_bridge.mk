# SWD bridge to the nRF over raw HID (common/swd_bridge.c), for the boards that wire SWD to
# the AVR: dongle PCB v7 / v1.0.0, mouse PCB v1.3.0. Included from their post_rules.mk,
# which QMK reads after the keymap's rules.mk with the -e variables all in.
#
# Raw HID and the console don't both fit in the U2's four endpoints (QMK stops with "not
# enough available endpoints"): on LUFA AVR raw HID takes two, and the dongle's keyboard +
# shared, or the mouse's keyboard + mouse, the other two. So console builds
# (-e CONSOLE_ENABLE=yes) go without the bridge. -e MM_SWD_BRIDGE=no leaves it out of any
# build.
MM_SWD_BRIDGE ?= yes
ifeq ($(strip $(MM_SWD_BRIDGE)), yes)
    ifneq ($(strip $(CONSOLE_ENABLE)), yes)
        RAW_ENABLE = yes
        SRC += swd.c swd_bridge.c
        OPT_DEFS += -DMM_SWD_BRIDGE_ENABLE
    endif
endif
