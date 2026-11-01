POINTING_DEVICE_DRIVER = paw3220

SPI_DRIVER_REQUIRED = yes
SRC += bluetooth_custom.c
SRC += ble_send_buf.cpp
SRC += matrix_sleep.c
SRC += reset_reason.c
SRC += mcu_clock.c
VPATH += keyboards/mini_mighty/common

# Overrides the revision's CPU clock divider (common/mcu_clock.h), e.g. a
# full-speed image for comparison:
#   qmk compile -kb mini_mighty/mouse/rev1_1_0 -km default -e MCU_CPU_DIV=1 \
#       -e TARGET=mini_mighty_mouse_rev1_1_0_default_8mhz
ifneq ($(strip $(MCU_CPU_DIV)),)
    OPT_DEFS += -DMCU_CPU_DIV=$(strip $(MCU_CPU_DIV))
endif
