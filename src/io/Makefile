ROOT_DIR ?= $(abspath ../..)
include $(ROOT_DIR)/mk/toolchain.mk

OBJ_DIR := $(BIN_DIR)/obj/io
IO_OBJECT := $(BIN_DIR)/io/io.o
IO_MAP := $(BIN_DIR)/io/io.map

SOURCES := \
	storage/ata_pio.c \
	storage/ahci.c \
	storage/block_device.c \
	storage/storage_probe.c \
	io_sched.c \
	swap.c

OBJECTS := $(SOURCES:%.c=$(OBJ_DIR)/%.o)
DEPS := $(OBJECTS:.o=.d)

CPPFLAGS += -I$(ROOT_DIR)/src

.PHONY: all clean
all: $(IO_OBJECT)

$(OBJ_DIR)/%.o: $(ROOT_DIR)/src/io/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(KERNEL_CFLAGS) -c $< -o $@

$(IO_OBJECT): $(OBJECTS)
	@mkdir -p $(@D)
	$(LD) -r -Map=$(IO_MAP) -o $@ $(OBJECTS)
	@echo "io subsystem -> $@"

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)/io

-include $(DEPS)
