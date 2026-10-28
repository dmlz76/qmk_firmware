# SWD bridge to the nRF over raw HID (swd_bridge.c), for dongle PCB v7. Raw HID and the
# console don't both fit in the 16U2's four endpoints (QMK stops with "not enough available
# endpoints"), so console builds (-e CONSOLE_ENABLE=yes) go without the bridge. That's why
# this is in post_rules.mk: it's read after the keymap's rules.mk and the -e variables are
# all in. -e MM_SWD_BRIDGE=no leaves it out of any build.
MM_SWD_BRIDGE ?= yes
ifeq ($(strip $(MM_SWD_BRIDGE)), yes)
    ifneq ($(strip $(CONSOLE_ENABLE)), yes)
        RAW_ENABLE = yes
        SRC += swd.c swd_bridge.c
    endif
endif
