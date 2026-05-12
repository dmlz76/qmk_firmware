POINTING_DEVICE_DRIVER = paw3220

SPI_DRIVER_REQUIRED = yes
SRC += bluetooth_custom.c
SRC += ble_send_buf.cpp
SRC += matrix_sleep.c
VPATH += keyboards/mini_mighty/common
