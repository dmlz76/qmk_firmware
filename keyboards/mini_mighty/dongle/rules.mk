MOUSE_ENABLE = yes

CUSTOM_MATRIX = yes
SRC += matrix_custom.c
VPATH += keyboards/mini_mighty/common

CONSOLE_ENABLE = no

SPACE_CADET_ENABLE = no
GRAVE_ESC_ENABLE = no 
MAGIC_ENABLE = no

EXTRAFLAGS += -flto
