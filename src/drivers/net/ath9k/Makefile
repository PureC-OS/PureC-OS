ROOT_DIR ?= $(abspath ../../../..)
include $(ROOT_DIR)/mk/toolchain.mk

ATH9K_DIR := $(ROOT_DIR)/src/drivers/net/ath9k
OBJ_DIR := $(BIN_DIR)/obj/drivers/net/ath9k
MODULE_DIR := $(BIN_DIR)/modules
TARGET_ELF := $(MODULE_DIR)/ar9285.elf
TARGET_KO := $(MODULE_DIR)/ar9285.ko

SOURCES := ar9285.c
OBJECTS := $(SOURCES:%.c=$(OBJ_DIR)/%.o)
DEPS := $(OBJECTS:.o=.d)

CPPFLAGS += -I$(ATH9K_DIR)/include
MOD_CFLAGS := $(KERNEL_CFLAGS) -ffreestanding -fno-pic

.PHONY: all module clean
all: module

module: $(TARGET_ELF) $(TARGET_KO)

$(OBJ_DIR)/%.o: $(ATH9K_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(MOD_CFLAGS) -c $< -o $@

$(TARGET_ELF): $(OBJECTS)
	@mkdir -p $(@D)
	$(LD) -r -o $@ $(OBJECTS)
	@echo "ar9285 module -> $@"

$(TARGET_KO): $(TARGET_ELF)
	@cp $< $@
	@echo "ar9285 module -> $@"

-include $(DEPS)
