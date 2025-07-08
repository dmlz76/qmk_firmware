MOUSE_ENABLE = yes

CUSTOM_MATRIX = yes
SRC += matrix_custom.c
VPATH += keyboards/mini_mighty/common

CONSOLE_ENABLE = no

EXTRAFLAGS += -flto
