# GCC turns QMK's KEYCODE2CONSUMER() switch (tmk_core/protocol/report.h) into a 54 B
# lookup table, and on AVR a const table lands in RAM. With two copies and three small
# tables, 123 of the 16u2's 512 B went to switch tables, and the stack overflowed into
# tapping_key/waiting_buffer as phantom key presses (2026-11-23, PROGRESS.md). This
# compiles switches to compare chains instead, for some flash. Same fix as dongle/.
CFLAGS += -fno-tree-switch-conversion
