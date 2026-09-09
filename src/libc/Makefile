ROOT_DIR ?= $(abspath ../..)
include $(ROOT_DIR)/mk/toolchain.mk

OBJ_DIR := $(BIN_DIR)/obj/libc
LIB_DIR := $(BIN_DIR)/lib
OBJECTS := $(OBJ_DIR)/runtime.o $(OBJ_DIR)/stdlib.o $(OBJ_DIR)/string.o $(OBJ_DIR)/ctype.o $(OBJ_DIR)/stdio.o
TARGET := $(LIB_DIR)/libpurec.a
CRT0 := $(LIB_DIR)/crt0.o

.PHONY: all
all: $(TARGET) $(CRT0)

-include $(OBJECTS:.o=.d)

$(TARGET): $(OBJECTS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

$(OBJ_DIR)/%.o: $(ROOT_DIR)/src/libc/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(USER_CFLAGS) -mgeneral-regs-only -c $< -o $@

# Hosted entry point, deliberately NOT archived into libpurec.a (native
# programs define their own _start). Ported programs link it explicitly.
$(CRT0): $(ROOT_DIR)/src/libc/crt0.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(USER_CFLAGS) -mgeneral-regs-only -c $< -o $@
