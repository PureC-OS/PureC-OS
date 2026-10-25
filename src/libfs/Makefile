ROOT_DIR ?= $(abspath ../..)
include $(ROOT_DIR)/mk/toolchain.mk

OBJ_DIR := $(BIN_DIR)/obj/libfs
LIB_DIR := $(BIN_DIR)/lib
SOURCES := file.c dir.c
OBJECTS := $(SOURCES:%.c=$(OBJ_DIR)/%.o)
TARGET := $(LIB_DIR)/libpurefs.a

.PHONY: all
all: $(TARGET)

-include $(OBJECTS:.o=.d)

$(TARGET): $(OBJECTS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

$(OBJ_DIR)/%.o: $(ROOT_DIR)/src/libfs/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(USER_CFLAGS) -c $< -o $@
