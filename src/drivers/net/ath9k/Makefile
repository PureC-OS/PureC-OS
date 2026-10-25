ROOT_DIR ?= $(abspath ../../../..)
include $(ROOT_DIR)/mk/toolchain.mk

ATH9K_DIR := $(ROOT_DIR)/src/drivers/net/ath9k
DOT11_DIR := $(ROOT_DIR)/src/net/802.11
OBJ_DIR := $(BIN_DIR)/obj/drivers/net/ath9k
MODULE_DIR := $(BIN_DIR)/modules
TARGET_ELF := $(MODULE_DIR)/ar9285.elf
TARGET_KO := $(MODULE_DIR)/ar9285.ko
TARGET_SO := $(MODULE_DIR)/ar9285.so

SOURCES := ar9285.c
DOT11_B_NAMES := b_rates b_chan b_plcp b_mgmt
OBJECTS := $(SOURCES:%.c=$(OBJ_DIR)/%.o) $(OBJ_DIR)/dot11_assoc.o \
	$(DOT11_B_NAMES:%=$(OBJ_DIR)/dot11_b_%.o)
SO_OBJ_DIR := $(OBJ_DIR)/pic
SO_OBJECTS := $(SOURCES:%.c=$(SO_OBJ_DIR)/%.o) $(SO_OBJ_DIR)/dot11_assoc.o \
	$(DOT11_B_NAMES:%=$(SO_OBJ_DIR)/dot11_b_%.o)
DEPS := $(OBJECTS:.o=.d)
SO_DEPS := $(SO_OBJECTS:.o=.d)

CPPFLAGS += -I$(ATH9K_DIR)/include
MOD_CFLAGS := $(KERNEL_CFLAGS) -ffreestanding -fno-pic

.PHONY: all module clean
all: module

module: $(TARGET_ELF) $(TARGET_KO) $(TARGET_SO)

$(OBJ_DIR)/%.o: $(ATH9K_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(MOD_CFLAGS) -c $< -o $@

$(SO_OBJ_DIR)/%.o: $(ATH9K_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(KMOD_SO_CFLAGS) -c $< -o $@

$(OBJ_DIR)/dot11_assoc.o: $(DOT11_DIR)/assoc.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(MOD_CFLAGS) -c $< -o $@

$(SO_OBJ_DIR)/dot11_assoc.o: $(DOT11_DIR)/assoc.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(KMOD_SO_CFLAGS) -c $< -o $@

$(OBJ_DIR)/dot11_b_%.o: $(DOT11_DIR)/b/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(MOD_CFLAGS) -c $< -o $@

$(SO_OBJ_DIR)/dot11_b_%.o: $(DOT11_DIR)/b/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(KMOD_SO_CFLAGS) -c $< -o $@

$(TARGET_ELF): $(OBJECTS)
	@mkdir -p $(@D)
	$(LD) -r -o $@ $(OBJECTS)
	@echo "ar9285 module -> $@"

$(TARGET_KO): $(TARGET_ELF)
	@cp $< $@
	@echo "ar9285 module -> $@"

$(TARGET_SO): $(SO_OBJECTS) $(KMOD_SO_LDSCRIPT)
	@mkdir -p $(@D)
	$(LD) $(KMOD_SO_LINK) -soname ar9285.so -o $@ $(SO_OBJECTS)
	@echo "ar9285 module -> $@"

-include $(DEPS) $(SO_DEPS)