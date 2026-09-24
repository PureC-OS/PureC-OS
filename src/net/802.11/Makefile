ROOT_DIR ?= $(abspath ../../..)
include $(ROOT_DIR)/mk/toolchain.mk

DOT11_DIR := $(ROOT_DIR)/src/net/802.11
OBJ_DIR := $(BIN_DIR)/obj/kernel/net/802.11

SOURCES := assoc.c b/b_rates.c b/b_chan.c b/b_plcp.c b/b_mgmt.c
OBJECTS := $(SOURCES:%.c=$(OBJ_DIR)/%.o)
DEPS := $(OBJECTS:.o=.d)

CPPFLAGS += -I$(DOT11_DIR)/include

.PHONY: all clean
all: $(OBJECTS)

$(OBJ_DIR)/%.o: $(DOT11_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(KERNEL_CFLAGS) -ffreestanding -fno-pic -c $< -o $@

clean:
	rm -f $(OBJECTS) $(DEPS)

-include $(DEPS)