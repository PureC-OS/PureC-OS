ROOT_DIR ?= $(abspath ../..)
include $(ROOT_DIR)/mk/toolchain.mk

OBJ_DIR := $(BIN_DIR)/obj/programs
PROGRAM_DIR := $(BIN_DIR)/programs
LIB_DIR := $(BIN_DIR)/lib
LIBPUREC := $(LIB_DIR)/libpurec.a
LIBPUREGUI := $(LIB_DIR)/libpuregui.a
LIBPGUIW := $(LIB_DIR)/libpguiw.a
LIBPUREFS := $(LIB_DIR)/libpurefs.a
LIBPUREAUDIO := $(LIB_DIR)/libpureaudio.a
LINKER_SCRIPT := $(ROOT_DIR)/linker-userspace.ld

PLAIN_SOURCES := \
	init/main.c \
	game/snake/main.c \
	nano/main.c \
	nano/editor.c \
	system/main.c \
	system/commands.c \
	system/filesystem.c \
	system/system.c
GENERAL_SOURCES := \
	installer/main.c \
	installer/progress.c \
	terminal/main.c \
	terminal/shell.c \
	terminal/path.c \
	terminal/environment.c \
	terminal/window.c \
	nano/window.c \
	gui_demo/main.c \
	game/tetris/main.c \
	files/main.c \
	files/app.c \
	files/model.c \
	files/path.c \
	files/view.c \
	settings/main.c \
	settings/core/model.c \
	settings/core/app.c \
	settings/pages/audio.c \
	settings/pages/wifi.c \
	monitor/main.c \
	disks/main.c \
	logview/main.c \
	imgview/main.c
GENERAL_CPP_SOURCES := \
	hexedit/src/main/main.cpp \
	hexedit/src/buffer/buffer.cpp \
	hexedit/src/tree/tree.cpp \
	hexedit/src/view/view.cpp \
	hexedit/src/input/input.cpp \
	hexedit/src/editor/editor.cpp \
	hexedit/src/window/window.cpp
PLAIN_OBJECTS := $(PLAIN_SOURCES:%.c=$(OBJ_DIR)/%.o)
GENERAL_OBJECTS := $(GENERAL_SOURCES:%.c=$(OBJ_DIR)/%.o)
GENERAL_CPP_OBJECTS := $(GENERAL_CPP_SOURCES:%.cpp=$(OBJ_DIR)/%.o)
TARGETS := \
	$(PROGRAM_DIR)/init \
	$(PROGRAM_DIR)/installer \
	$(PROGRAM_DIR)/snake \
	$(PROGRAM_DIR)/tetris \
	$(PROGRAM_DIR)/terminal \
	$(PROGRAM_DIR)/nano \
	$(PROGRAM_DIR)/system \
	$(PROGRAM_DIR)/gui-demo \
	$(PROGRAM_DIR)/files \
	$(PROGRAM_DIR)/settings \
	$(PROGRAM_DIR)/monitor \
	$(PROGRAM_DIR)/disks \
	$(PROGRAM_DIR)/logview \
 	$(PROGRAM_DIR)/hexedit \
 	$(PROGRAM_DIR)/imgview \
 	$(PROGRAM_DIR)/hello

.NOTPARALLEL:
.PHONY: all hexedit
all: $(TARGETS)
hexedit: $(PROGRAM_DIR)/hexedit

-include $(PLAIN_OBJECTS:.o=.d) $(GENERAL_OBJECTS:.o=.d) $(GENERAL_CPP_OBJECTS:.o=.d)

$(GENERAL_OBJECTS): PRIVATE_CFLAGS := -mgeneral-regs-only
$(GENERAL_CPP_OBJECTS): PRIVATE_CXXFLAGS := -mgeneral-regs-only -std=c++20 -fno-exceptions -fno-rtti

$(LIBPUREC):
	$(MAKE) -C $(ROOT_DIR)/src/libc

$(LIBPUREGUI) $(LIBPGUIW):
	$(MAKE) -C $(ROOT_DIR)/src/libgui

$(LIBPUREFS):
	$(MAKE) -C $(ROOT_DIR)/src/libfs

$(OBJ_DIR)/%.o: $(ROOT_DIR)/src/programs/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(ROOT_DIR)/src/programs/settings/include $(DEPFLAGS) $(USER_CFLAGS) $(PRIVATE_CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(ROOT_DIR)/src/programs/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) -I$(ROOT_DIR)/src/programs/settings/include -I$(ROOT_DIR)/src/programs/hexedit/include $(DEPFLAGS) $(USER_CFLAGS) $(PRIVATE_CXXFLAGS) -c $< -o $@

$(PROGRAM_DIR)/init: $(OBJ_DIR)/init/main.o $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/installer: $(OBJ_DIR)/installer/main.o $(OBJ_DIR)/installer/progress.o $(OBJ_DIR)/terminal/window.o $(LIBPUREGUI) $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/snake: $(OBJ_DIR)/game/snake/main.o $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/terminal: $(OBJ_DIR)/terminal/main.o $(OBJ_DIR)/terminal/shell.o $(OBJ_DIR)/terminal/path.o $(OBJ_DIR)/terminal/environment.o $(OBJ_DIR)/terminal/window.o $(LIBPUREGUI) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/nano: $(OBJ_DIR)/nano/main.o $(OBJ_DIR)/nano/editor.o $(OBJ_DIR)/nano/window.o $(LIBPUREGUI) $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/system: $(OBJ_DIR)/system/main.o $(OBJ_DIR)/system/commands.o $(OBJ_DIR)/system/filesystem.o $(OBJ_DIR)/system/system.o $(OBJ_DIR)/terminal/path.o $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/gui-demo: $(OBJ_DIR)/gui_demo/main.o $(LIBPGUIW) $(LIBPUREGUI) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/files: $(OBJ_DIR)/files/main.o $(OBJ_DIR)/files/app.o $(OBJ_DIR)/files/model.o $(OBJ_DIR)/files/path.o $(OBJ_DIR)/files/view.o $(LIBPGUIW) $(LIBPUREGUI) $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/settings: $(OBJ_DIR)/settings/main.o $(OBJ_DIR)/settings/core/model.o $(OBJ_DIR)/settings/core/app.o $(OBJ_DIR)/settings/pages/audio.o $(OBJ_DIR)/settings/pages/wifi.o $(LIBPGUIW) $(LIBPUREGUI) $(LIBPUREAUDIO) $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/monitor: $(OBJ_DIR)/monitor/main.o $(LIBPGUIW) $(LIBPUREGUI) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/disks: $(OBJ_DIR)/disks/main.o $(LIBPGUIW) $(LIBPUREGUI) $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/logview: $(OBJ_DIR)/logview/main.o $(LIBPGUIW) $(LIBPUREGUI) $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/tetris: $(OBJ_DIR)/game/tetris/main.o $(LIBPGUIW) $(LIBPUREGUI) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/hexedit: $(GENERAL_CPP_OBJECTS) $(LIBPUREGUI) $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/imgview: $(OBJ_DIR)/imgview/main.o $(LIBPGUIW) $(LIBPUREGUI) $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

# Hosted demo: standard main() + crt0 + hosted headers (TCC migration
# proving ground). Built with the same toolchain, linked against crt0.o.
$(OBJ_DIR)/hello/main.o: $(ROOT_DIR)/src/programs/hello/main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(ROOT_DIR)/src/libc/include/hosted $(DEPFLAGS) $(USER_CFLAGS) -mgeneral-regs-only -c $< -o $@

$(PROGRAM_DIR)/hello: $(OBJ_DIR)/hello/main.o $(LIB_DIR)/crt0.o $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(LIB_DIR)/crt0.o $(OBJ_DIR)/hello/main.o $(LIBPUREFS) $(LIBPUREC)
