ROOT_DIR ?= $(abspath ../..)
include $(ROOT_DIR)/mk/toolchain.mk

OBJ_DIR := $(BIN_DIR)/obj/libsdl2_mixer
LIB_DIR := $(BIN_DIR)/lib
SDL_DIR ?= $(ROOT_DIR)/SDL
TARGET := $(LIB_DIR)/libSDL2_mixer.a

.PHONY: all
all: $(TARGET)

-include $(OBJ_DIR)/mixer.d

$(TARGET): $(OBJ_DIR)/mixer.o
	@mkdir -p $(@D)
	rm -f $@
	$(AR) rcs $@ $^

$(OBJ_DIR)/mixer.o: $(ROOT_DIR)/src/libsdl2_mixer/mixer.c
	@mkdir -p $(@D)
	$(CC) $(DEPFLAGS) $(USER_CFLAGS) -D__PUREC__ -I$(SDL_DIR)/include -I$(ROOT_DIR)/src \
		-I$(ROOT_DIR)/src/libc/include/hosted -I$(ROOT_DIR)/src/libsdl2_mixer -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(OBJ_DIR) $(TARGET)
