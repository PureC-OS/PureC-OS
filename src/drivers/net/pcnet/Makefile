ROOT_DIR ?= $(abspath ../../../..)
include $(ROOT_DIR)/mk/toolchain.mk

PCNET_DIR := $(ROOT_DIR)/src/drivers/net/pcnet
OBJ_DIR := $(BIN_DIR)/obj/drivers/net/pcnet
MODULE_DIR := $(BIN_DIR)/modules
TARGET_ELF := $(MODULE_DIR)/pcnet_am79c970a.elf
TARGET_KO := $(MODULE_DIR)/pcnet_am79c970a.ko

SOURCES := pcnet_am79c970a.c
OBJECTS := $(SOURCES:%.c=$(OBJ_DIR)/%.o)
DEPS := $(OBJECTS:.o=.d)

CPPFLAGS += -I$(PCNET_DIR)/include
MOD_CFLAGS := $(KERNEL_CFLAGS) -ffreestanding -fno-pic

.PHONY: all module clean
all: module

module: $(TARGET_ELF) $(TARGET_KO)

$(OBJ_DIR)/%.o: $(PCNET_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(MOD_CFLAGS) -c $< -o $@

$(TARGET_ELF): $(OBJECTS)
	@mkdir -p $(@D)
	$(LD) -r -o $@ $(OBJECTS)
	@echo "pcnet_am79c970a module -> $@"

$(TARGET_KO): $(TARGET_ELF)
	@cp $< $@
	@echo "pcnet_am79c970a module -> $@"

-include $(DEPS)
