SRC += matrix_custom.c
SRC += peer_battery.c
SRC += reset_reason.c
VPATH += keyboards/mini_mighty/common

# GCC turns QMK's KEYCODE2CONSUMER() switch (tmk_core/protocol/report.h) into a 54 B
# lookup table, and on AVR a const table lands in RAM. There were two copies: 108 of the
# 16u2's 512 B, while the stack had ~9 B to spare in the worst case (2026-10-19,
# PROGRESS.md). This compiles switches to compare chains instead, for some flash.
CFLAGS += -fno-tree-switch-conversion
