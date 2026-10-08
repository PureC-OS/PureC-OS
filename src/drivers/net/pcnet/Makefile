ROOT_DIR ?= $(abspath ../../../..)
include $(ROOT_DIR)/mk/toolchain.mk

PCNET_DIR := $(ROOT_DIR)/src/drivers/net/pcnet
OBJ_DIR := $(BIN_DIR)/obj/drivers/net/pcnet
MODULE_DIR := $(BIN_DIR)/modules
TARGET_ELF := $(MODULE_DIR)/pcnet_am79c970a.elf
TARGET_KO := $(MODULE_DIR)/pcnet_am79c970a.ko
TARGET_SO := $(MODULE_DIR)/pcnet_am79c970a.so

SOURCES := pcnet_am79c970a.c
OBJECTS := $(SOURCES:%.c=$(OBJ_DIR)/%.o)
SO_OBJ_DIR := $(OBJ_DIR)/pic
SO_OBJECTS := $(SOURCES:%.c=$(SO_OBJ_DIR)/%.o)
DEPS := $(OBJECTS:.o=.d)
SO_DEPS := $(SO_OBJECTS:.o=.d)

CPPFLAGS += -I$(PCNET_DIR)/include
MOD_CFLAGS := $(KERNEL_CFLAGS) -ffreestanding -fno-pic

.PHONY: all module clean
all: module

module: $(TARGET_ELF) $(TARGET_KO) $(TARGET_SO)

$(OBJ_DIR)/%.o: $(PCNET_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(MOD_CFLAGS) -c $< -o $@

$(SO_OBJ_DIR)/%.o: $(PCNET_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(KMOD_SO_CFLAGS) -c $< -o $@

$(TARGET_ELF): $(OBJECTS)
	@mkdir -p $(@D)
	$(LD) -r -o $@ $(OBJECTS)
	@echo "pcnet_am79c970a module -> $@"

$(TARGET_KO): $(TARGET_ELF)
	@cp $< $@
	@echo "pcnet_am79c970a module -> $@"

$(TARGET_SO): $(SO_OBJECTS) $(KMOD_SO_LDSCRIPT)
	@mkdir -p $(@D)
	$(LD) $(KMOD_SO_LINK) -soname pcnet_am79c970a.so -o $@ $(SO_OBJECTS)
	@echo "pcnet_am79c970a module -> $@"

-include $(DEPS) $(SO_DEPS)
