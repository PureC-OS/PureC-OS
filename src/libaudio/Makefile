ROOT_DIR ?= $(abspath ../..)
include $(ROOT_DIR)/mk/toolchain.mk

OBJ_DIR := $(BIN_DIR)/obj/libaudio
LIB_DIR := $(BIN_DIR)/lib
SOURCES := audio.c
OBJECTS := $(SOURCES:%.c=$(OBJ_DIR)/%.o)
TARGET := $(LIB_DIR)/libpureaudio.a

.PHONY: all
all: $(TARGET)

-include $(OBJECTS:.o=.d)

$(TARGET): $(OBJECTS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

$(OBJ_DIR)/%.o: $(ROOT_DIR)/src/libaudio/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(USER_CFLAGS) -c $< -o $@
