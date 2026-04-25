SPI_DRIVER_REQUIRED = yes
SRC += bluetooth_custom.c
SRC += ble_send_buf.cpp
SRC += matrix_sleep.c
VPATH += keyboards/mini_mighty/common

LUFA_OPTS_USB_REG_DISABLED = yes
