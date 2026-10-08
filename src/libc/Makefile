ROOT_DIR ?= $(abspath ../..)
include $(ROOT_DIR)/mk/toolchain.mk

OBJ_DIR := $(BIN_DIR)/obj/libc
LIB_DIR := $(BIN_DIR)/lib
OBJECTS := $(OBJ_DIR)/runtime.o $(OBJ_DIR)/stdlib.o $(OBJ_DIR)/string.o $(OBJ_DIR)/ctype.o $(OBJ_DIR)/stdio.o $(OBJ_DIR)/unistd.o $(OBJ_DIR)/time.o $(OBJ_DIR)/setjmp.o $(OBJ_DIR)/dlfcn.o $(OBJ_DIR)/math.o $(OBJ_DIR)/signal.o
SO_OBJ_DIR := $(OBJ_DIR)/pic
SO_OBJECTS := $(OBJECTS:$(OBJ_DIR)/%.o=$(SO_OBJ_DIR)/%.o)
TARGET := $(LIB_DIR)/libpurec.a
SO_TARGET := $(LIB_DIR)/libpurec.so
SO_LD_SCRIPT := $(ROOT_DIR)/src/libc/userspace-so.ld
CRT0 := $(LIB_DIR)/crt0.o

.PHONY: all
all: $(TARGET) $(SO_TARGET) $(CRT0)

-include $(OBJECTS:.o=.d)
-include $(SO_OBJECTS:.o=.d)

$(TARGET): $(OBJECTS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

$(SO_TARGET): $(SO_OBJECTS) $(SO_LD_SCRIPT)
	@mkdir -p $(@D)
	$(LD) $(SO_LINK_FLAGS) -soname libpurec.so -T $(SO_LD_SCRIPT) -o $@ $(SO_OBJECTS)

$(OBJ_DIR)/%.o: $(ROOT_DIR)/src/libc/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(USER_CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(ROOT_DIR)/src/libc/%.S
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(USER_CFLAGS) -c $< -o $@

$(SO_OBJ_DIR)/%.o: $(ROOT_DIR)/src/libc/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(USER_SO_CFLAGS) -c $< -o $@

$(SO_OBJ_DIR)/%.o: $(ROOT_DIR)/src/libc/%.S
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(USER_SO_CFLAGS) -c $< -o $@

# Hosted entry point, deliberately NOT archived into libpurec.a (native
# programs define their own _start). Ported programs link it explicitly.
$(CRT0): $(ROOT_DIR)/src/libc/crt0.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(USER_CFLAGS) -c $< -o $@
