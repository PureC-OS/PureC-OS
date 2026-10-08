ROOT_DIR ?= $(abspath ../../../..)
include $(ROOT_DIR)/mk/toolchain.mk

E1000_GC_DIR := $(ROOT_DIR)/src/drivers/net/82543gc
OBJ_DIR := $(BIN_DIR)/obj/drivers/net/82543gc
MODULE_DIR := $(BIN_DIR)/modules
TARGET_ELF := $(MODULE_DIR)/e1000_82543gc.elf
TARGET_KO := $(MODULE_DIR)/e1000_82543gc.ko
TARGET_SO := $(MODULE_DIR)/e1000_82543gc.so

SOURCES := e1000_82543gc.c
OBJECTS := $(SOURCES:%.c=$(OBJ_DIR)/%.o)
SO_OBJ_DIR := $(OBJ_DIR)/pic
SO_OBJECTS := $(SOURCES:%.c=$(SO_OBJ_DIR)/%.o)
DEPS := $(OBJECTS:.o=.d)
SO_DEPS := $(SO_OBJECTS:.o=.d)

CPPFLAGS += -I$(E1000_GC_DIR)/include
MOD_CFLAGS := $(KERNEL_CFLAGS) -ffreestanding -fno-pic

.PHONY: all module clean
all: module

module: $(TARGET_ELF) $(TARGET_KO) $(TARGET_SO)

$(OBJ_DIR)/%.o: $(E1000_GC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(MOD_CFLAGS) -c $< -o $@

$(SO_OBJ_DIR)/%.o: $(E1000_GC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(KMOD_SO_CFLAGS) -c $< -o $@

$(TARGET_ELF): $(OBJECTS)
	@mkdir -p $(@D)
	$(LD) -r -o $@ $(OBJECTS)
	@echo "e1000_82543gc module -> $@"

$(TARGET_KO): $(TARGET_ELF)
	@cp $< $@
	@echo "e1000_82543gc module -> $@"

$(TARGET_SO): $(SO_OBJECTS) $(KMOD_SO_LDSCRIPT)
	@mkdir -p $(@D)
	$(LD) $(KMOD_SO_LINK) -soname e1000_82543gc.so -o $@ $(SO_OBJECTS)
	@echo "e1000_82543gc module -> $@"

-include $(DEPS) $(SO_DEPS)
