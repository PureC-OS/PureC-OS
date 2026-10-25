ROOT_DIR ?= $(abspath ../../../..)
include $(ROOT_DIR)/mk/toolchain.mk

DRIVER_DIR := $(ROOT_DIR)/src/drivers/input/i2c_hid_touchpad
OBJ_DIR := $(BIN_DIR)/obj/drivers/input/i2c_hid_touchpad
MODULE_DIR := $(BIN_DIR)/modules
TARGET_ELF := $(MODULE_DIR)/i2c_hid_touchpad.elf
TARGET_KO := $(MODULE_DIR)/i2c_hid_touchpad.ko
TARGET_SO := $(MODULE_DIR)/i2c_hid_touchpad.so
SOURCES := i2c_hid_touchpad.c
OBJECTS := $(SOURCES:%.c=$(OBJ_DIR)/%.o)
SO_OBJ_DIR := $(OBJ_DIR)/pic
SO_OBJECTS := $(SOURCES:%.c=$(SO_OBJ_DIR)/%.o)
DEPS := $(OBJECTS:.o=.d)
SO_DEPS := $(SO_OBJECTS:.o=.d)
MOD_CFLAGS := $(KERNEL_CFLAGS) -ffreestanding -fno-pic

.PHONY: all module
all: module
module: $(TARGET_ELF) $(TARGET_KO) $(TARGET_SO)

$(OBJ_DIR)/%.o: $(DRIVER_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(MOD_CFLAGS) -c $< -o $@
$(SO_OBJ_DIR)/%.o: $(DRIVER_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(KMOD_SO_CFLAGS) -c $< -o $@
$(TARGET_ELF): $(OBJECTS)
	@mkdir -p $(@D)
	$(LD) -r -o $@ $(OBJECTS)
$(TARGET_KO): $(TARGET_ELF)
	@cp $< $@
$(TARGET_SO): $(SO_OBJECTS) $(KMOD_SO_LDSCRIPT)
	@mkdir -p $(@D)
	$(LD) $(KMOD_SO_LINK) -soname i2c_hid_touchpad.so -o $@ $(SO_OBJECTS)

-include $(DEPS) $(SO_DEPS)
