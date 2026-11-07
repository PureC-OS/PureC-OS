ROOT_DIR ?= $(abspath ../../..)
include $(ROOT_DIR)/mk/toolchain.mk

OBJ_DIR := $(BIN_DIR)/obj/fs/fat32
MODULE_DIR := $(BIN_DIR)/modules
TARGET_SO := $(MODULE_DIR)/fat32.so

OBJECTS := $(OBJ_DIR)/fat32.o

CPPFLAGS += -I$(ROOT_DIR)/src
MOD_CFLAGS := $(KMOD_SO_CFLAGS)

.PHONY: all module clean
all: module

module: $(TARGET_SO)

$(OBJ_DIR)/%.o: $(ROOT_DIR)/src/fs/fat32/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(MOD_CFLAGS) -c $< -o $@

$(TARGET_SO): $(OBJECTS) $(KMOD_SO_LDSCRIPT)
	@mkdir -p $(@D)
	$(LD) $(KMOD_SO_LINK) -soname fat32.so -o $@ $(OBJECTS)
	@echo "fat32 module -> $@"

clean:
	rm -rf $(OBJ_DIR) $(TARGET_SO)

-include $(OBJECTS:.o=.d)
