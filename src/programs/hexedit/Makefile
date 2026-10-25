ROOT_DIR ?= $(abspath ../../..)
include $(ROOT_DIR)/mk/toolchain.mk
BIN_DIR ?= $(ROOT_DIR)/bin
OBJ_DIR := $(BIN_DIR)/obj/programs/hexedit
PROGRAM := $(BIN_DIR)/programs/hexedit
LIB_DIR := $(BIN_DIR)/lib
LIBPUREC := $(LIB_DIR)/libpurec.a
LIBPUREGUI := $(LIB_DIR)/libpuregui.a
LIBPUREFS := $(LIB_DIR)/libpurefs.a
LINKER := $(ROOT_DIR)/linker-userspace.ld
SOURCES := src/main/main.cpp src/buffer/buffer.cpp src/tree/tree.cpp src/view/view.cpp src/input/input.cpp src/editor/editor.cpp src/window/window.cpp
OBJECTS := $(SOURCES:%.cpp=$(OBJ_DIR)/%.o)
CPPFLAGS += -I$(ROOT_DIR)/src -I$(ROOT_DIR)/src/programs/hexedit/include
DEPFLAGS := -MMD -MP
USER_CXXFLAGS := $(USER_CFLAGS) -std=c++20 -fno-exceptions -fno-rtti -mgeneral-regs-only

.PHONY: all clean
all: $(PROGRAM)

$(PROGRAM): $(OBJECTS) $(LIBPUREC) $(LIBPUREGUI) $(LIBPUREFS) $(LINKER)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER) -o $@ $(filter-out $(LINKER),$^)

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(DEPFLAGS) $(USER_CXXFLAGS) -c $< -o $@

$(LIBPUREC):
	$(MAKE) -C $(ROOT_DIR)/src/libc

$(LIBPUREGUI):
	$(MAKE) -C $(ROOT_DIR)/src/libgui

$(LIBPUREFS):
	$(MAKE) -C $(ROOT_DIR)/src/libfs

clean:
	rm -rf $(OBJ_DIR) $(PROGRAM)

-include $(OBJECTS:.o=.d)
