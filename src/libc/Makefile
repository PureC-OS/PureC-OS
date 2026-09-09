ROOT_DIR ?= $(abspath ../..)
include $(ROOT_DIR)/mk/toolchain.mk

OBJ_DIR := $(BIN_DIR)/obj/libc
LIB_DIR := $(BIN_DIR)/lib
OBJECTS := $(OBJ_DIR)/runtime.o $(OBJ_DIR)/stdlib.o $(OBJ_DIR)/string.o $(OBJ_DIR)/ctype.o
TARGET := $(LIB_DIR)/libpurec.a

.PHONY: all
all: $(TARGET)

-include $(OBJECTS:.o=.d)

$(TARGET): $(OBJECTS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

$(OBJ_DIR)/%.o: $(ROOT_DIR)/src/libc/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(USER_CFLAGS) -mgeneral-regs-only -c $< -o $@
