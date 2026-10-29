ROOT_DIR ?= $(abspath ../..)
include $(ROOT_DIR)/mk/toolchain.mk

SDL_DIR ?= $(ROOT_DIR)/SDL
OBJ_DIR := $(BIN_DIR)/obj/libsdl2
LIB_DIR := $(BIN_DIR)/lib
PROGRAM_DIR := $(BIN_DIR)/programs
TARGET := $(LIB_DIR)/libSDL2.a
LINKER_SCRIPT := $(ROOT_DIR)/linker-userspace.ld

SDL_SOURCES := \
	$(wildcard $(SDL_DIR)/src/*.c) \
	$(wildcard $(SDL_DIR)/src/atomic/*.c) \
	$(wildcard $(SDL_DIR)/src/audio/*.c) \
	$(wildcard $(SDL_DIR)/src/audio/dummy/*.c) \
	$(wildcard $(SDL_DIR)/src/cpuinfo/*.c) \
	$(wildcard $(SDL_DIR)/src/events/*.c) \
	$(wildcard $(SDL_DIR)/src/file/*.c) \
	$(wildcard $(SDL_DIR)/src/haptic/*.c) \
	$(wildcard $(SDL_DIR)/src/haptic/dummy/*.c) \
	$(wildcard $(SDL_DIR)/src/hidapi/*.c) \
	$(wildcard $(SDL_DIR)/src/joystick/*.c) \
	$(wildcard $(SDL_DIR)/src/joystick/dummy/*.c) \
	$(wildcard $(SDL_DIR)/src/loadso/dummy/*.c) \
	$(wildcard $(SDL_DIR)/src/power/*.c) \
	$(wildcard $(SDL_DIR)/src/filesystem/dummy/*.c) \
	$(wildcard $(SDL_DIR)/src/locale/*.c) \
	$(wildcard $(SDL_DIR)/src/locale/dummy/*.c) \
	$(wildcard $(SDL_DIR)/src/misc/*.c) \
	$(wildcard $(SDL_DIR)/src/misc/dummy/*.c) \
	$(wildcard $(SDL_DIR)/src/render/*.c) \
	$(wildcard $(SDL_DIR)/src/render/software/*.c) \
	$(wildcard $(SDL_DIR)/src/sensor/*.c) \
	$(wildcard $(SDL_DIR)/src/sensor/dummy/*.c) \
	$(wildcard $(SDL_DIR)/src/stdlib/*.c) \
	$(wildcard $(SDL_DIR)/src/libm/*.c) \
	$(wildcard $(SDL_DIR)/src/thread/*.c) \
	$(wildcard $(SDL_DIR)/src/thread/generic/*.c) \
	$(wildcard $(SDL_DIR)/src/timer/*.c) \
	$(wildcard $(SDL_DIR)/src/timer/purec/*.c) \
	$(wildcard $(SDL_DIR)/src/video/*.c) \
	$(wildcard $(SDL_DIR)/src/video/yuv2rgb/*.c) \
	$(wildcard $(SDL_DIR)/src/video/purec/*.c)
SDL_SOURCES := $(filter-out %/SDL_steam_virtual_gamepad.c,$(SDL_SOURCES))
SDL_OBJECTS := $(SDL_SOURCES:$(SDL_DIR)/%.c=$(OBJ_DIR)/%.o)

SDL_CFLAGS := -D__PUREC__ -fno-strict-aliasing \
	-I$(SDL_DIR)/include -I$(ROOT_DIR)/src \
	-I$(ROOT_DIR)/src/libc/include/hosted

SDLTEST_OBJECT := $(OBJ_DIR)/sdltest/main.o
SDLTEST := $(PROGRAM_DIR)/sdltest

.PHONY: all
all: $(TARGET) $(SDLTEST)

-include $(SDL_OBJECTS:.o=.d) $(SDLTEST_OBJECT:.o=.d)

$(TARGET): $(SDL_OBJECTS)
	@mkdir -p $(@D)
	rm -f $@
	$(AR) rcs $@ $^

$(OBJ_DIR)/%.o: $(SDL_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(DEPFLAGS) $(USER_CFLAGS) $(SDL_CFLAGS) -c $< -o $@

$(SDLTEST_OBJECT): $(ROOT_DIR)/src/libsdl2/sdltest/main.c
	@mkdir -p $(@D)
	$(CC) $(DEPFLAGS) $(USER_CFLAGS) $(SDL_CFLAGS) -c $< -o $@

$(SDLTEST): $(SDLTEST_OBJECT) $(LIB_DIR)/crt0.o $(TARGET) $(LIB_DIR)/libpuregui.a $(LIB_DIR)/libpurec.a $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(LIB_DIR)/crt0.o $(SDLTEST_OBJECT) $(TARGET) $(LIB_DIR)/libpuregui.a $(LIB_DIR)/libpurec.a

.PHONY: clean
clean:
	rm -rf $(OBJ_DIR) $(TARGET) $(SDLTEST)
