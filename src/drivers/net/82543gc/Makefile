ROOT_DIR ?= $(abspath ../../../../..)
include $(ROOT_DIR)/mk/toolchain.mk

E1000_GC_DIR := $(ROOT_DIR)/src/drivers/net/82543gc
OBJ_DIR := $(BIN_DIR)/obj/drivers/net/82543gc
MODULE_DIR := $(BIN_DIR)/modules
TARGET_ELF := $(MODULE_DIR)/e1000_82543gc.elf
TARGET_KO := $(MODULE_DIR)/e1000_82543gc.ko

SOURCES := e1000_82543gc.c
OBJECTS := $(SOURCES:%.c=$(OBJ_DIR)/%.o)
DEPS := $(OBJECTS:.o=.d)

CPPFLAGS += -I$(E1000_GC_DIR)/include
MOD_CFLAGS := $(KERNEL_CFLAGS) -ffreestanding -fno-pic

.PHONY: all module clean
all: module

module: $(TARGET_ELF) $(TARGET_KO)

$(OBJ_DIR)/%.o: $(E1000_GC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(MOD_CFLAGS) -c $< -o $@

$(TARGET_ELF): $(OBJECTS)
	@mkdir -p $(@D)
	$(LD) -r -o $@ $(OBJECTS)
	@echo "e1000_82543gc module -> $@"

$(TARGET_KO): $(TARGET_ELF)
	@cp $< $@
	@echo "e1000_82543gc module -> $@"

-include $(DEPS)
