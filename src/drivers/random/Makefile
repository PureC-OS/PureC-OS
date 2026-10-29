ROOT_DIR ?= $(abspath ../../..)
include $(ROOT_DIR)/mk/toolchain.mk

CRYPT_DIR ?= $(ROOT_DIR)/libxcrypt
RANDOM_DIR := $(ROOT_DIR)/src/drivers/random
OBJ_DIR := $(BIN_DIR)/obj/drivers/random
MODULE_DIR := $(BIN_DIR)/modules
TARGET_SO := $(MODULE_DIR)/random.so

OBJECTS := $(OBJ_DIR)/random.o $(OBJ_DIR)/random_core.o $(OBJ_DIR)/sha512.o

CPPFLAGS += -I$(CRYPT_DIR)/include -I$(RANDOM_DIR)
MOD_CFLAGS := $(KMOD_SO_CFLAGS)

.PHONY: all module clean
all: module

module: $(TARGET_SO)

$(OBJ_DIR)/%.o: $(RANDOM_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(MOD_CFLAGS) -c $< -o $@

$(OBJ_DIR)/sha512.o: $(CRYPT_DIR)/src/sha512.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(MOD_CFLAGS) -c $< -o $@

$(TARGET_SO): $(OBJECTS) $(KMOD_SO_LDSCRIPT)
	@mkdir -p $(@D)
	$(LD) $(KMOD_SO_LINK) -soname random.so -o $@ $(OBJECTS)
	@echo "random module -> $@"

clean:
	rm -rf $(OBJ_DIR) $(TARGET_SO)

-include $(OBJECTS:.o=.d)
