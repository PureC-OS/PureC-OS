ROOT_DIR ?= $(abspath ../../..)
include $(ROOT_DIR)/mk/toolchain.mk

OBJ_DIR := $(BIN_DIR)/obj/fs/ext2
MODULE_DIR := $(BIN_DIR)/modules
TARGET_ELF := $(MODULE_DIR)/ext2.elf
TARGET_KO := $(MODULE_DIR)/ext2.ko
TARGET_SO := $(MODULE_DIR)/ext2.so

SOURCES := block.c super.c inode.c dir.c file.c format.c ext2.c debug.c
OBJECTS := $(SOURCES:%.c=$(OBJ_DIR)/%.o)
SO_OBJ_DIR := $(OBJ_DIR)/pic
SO_OBJECTS := $(SOURCES:%.c=$(SO_OBJ_DIR)/%.o)
DEPS := $(OBJECTS:.o=.d)
SO_DEPS := $(SO_OBJECTS:.o=.d)

CPPFLAGS += -I$(ROOT_DIR)/src
MOD_CFLAGS := $(KERNEL_CFLAGS) -ffreestanding -fno-pic

.PHONY: all clean
all: $(TARGET_ELF) $(TARGET_KO) $(TARGET_SO)

$(OBJ_DIR)/%.o: $(ROOT_DIR)/src/fs/ext2/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(MOD_CFLAGS) -c $< -o $@

$(SO_OBJ_DIR)/%.o: $(ROOT_DIR)/src/fs/ext2/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(KMOD_SO_CFLAGS) -c $< -o $@

$(TARGET_ELF): $(OBJECTS)
	@mkdir -p $(@D)
	$(LD) -r -o $@ $(OBJECTS)
	@echo "ext2 module -> $@"

$(TARGET_KO): $(TARGET_ELF)
	@cp $< $@
	@echo "ext2 module -> $@"

$(TARGET_SO): $(SO_OBJECTS) $(KMOD_SO_LDSCRIPT)
	@mkdir -p $(@D)
	$(LD) $(KMOD_SO_LINK) -soname ext2.so -o $@ $(SO_OBJECTS)
	@echo "ext2 module -> $@"

-include $(DEPS) $(SO_DEPS)
