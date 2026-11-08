.DEFAULT_GOAL := all
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
LIBPURECRYPT := $(LIB_DIR)/libpurecrypt.a
CRYPT_DIR := $(ROOT_DIR)/libxcrypt
ACCOUNT_OBJECTS := $(OBJ_DIR)/accounts/accounts.o $(OBJ_DIR)/accounts/accounts_core.o $(OBJ_DIR)/accounts/install_accounts.o
LINKER_SCRIPT := $(ROOT_DIR)/linker-userspace.ld

PLAIN_SOURCES := \
	init/main.c \
	nano/main.c \
	nano/editor.c \
	system/main.c \
	system/commands.c \
	system/filesystem.c \
	system/system.c
LUA_SOURCES := \
	lua/lapi.c \
	lua/lcode.c \
	lua/lctype.c \
	lua/ldebug.c \
	lua/ldo.c \
	lua/ldump.c \
	lua/lfunc.c \
	lua/lgc.c \
	lua/linit-purec.c \
	lua/llex.c \
	lua/lmem.c \
	lua/lobject.c \
	lua/lopcodes.c \
	lua/lparser.c \
	lua/lstate.c \
	lua/lstring.c \
	lua/ltable.c \
	lua/ltm.c \
	lua/lundump.c \
	lua/lvm.c \
	lua/lzio.c \
	lua/lauxlib.c \
	lua/lbaselib.c \
	lua/lcorolib.c \
	lua/ldblib.c \
	lua/liolib.c \
	lua/loslib.c \
	lua/lstrlib.c \
	lua/ltablib.c \
	lua/lutf8lib.c \
	lua/loadlib.c \
	lua/lua.c \
	lua/purec_lua_readline.c
GENERAL_SOURCES := \
	installer/main.c \
	installer/progress.c \
	installer/setup.c \
	installer/ui.c \
	installer/view.c \
	accounts/accounts.c \
	accounts/accounts_core.c \
	terminal/main.c \
	terminal/shell.c \
	terminal/path.c \
	terminal/environment.c \
	terminal/window.c \
	nano/window.c \
	gui_demo/main.c \
	game/snake/main.c \
	game/tetris/main.c \
	game/puzzle15/puzzle15.c \
	game/2048/game2048.c \
	files/main.c \
	files/app.c \
	files/model.c \
	files/path.c \
	files/view.c \
  	settings/main.c \
  	settings/core/model.c \
  	settings/core/personalization.c \
  	settings/core/display.c \
  	settings/core/app.c \
  	settings/pages/audio.c \
  	settings/pages/wifi.c \
  	settings/pages/appearance.c \
  	settings/pages/display.c \
	login/main.c \
	disks/main.c \
	devmgr/main.c \
	logview/main.c \
  	imgview/main.c \
  	fputest/main.c \
  	gltest/main.c \
  	cd/cd.c \
	help/help.c \
	ls/ls.c \
	cat/cat.c \
	touch/touch.c \
	mkdir/mkdir.c \
	rm/rm.c \
	mkfs/mkfs.c \
	pwd/pwd.c \
	clear/clear.c \
 	echo/echo.c \
 	battery/battery.c \
 	burn/main.c \
 	eat/main.c \
 	cpuinfo/main.c \
 	raminfo/main.c \
 	hddcheck/main.c \
	reboot/reboot.c \
	poweroff/poweroff.c \
	shutdown/shutdown.c \
	halt/halt.c
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
LUA_OBJECTS := $(LUA_SOURCES:%.c=$(OBJ_DIR)/%.o)
TARGETS := \
	$(PROGRAM_DIR)/init \
	$(PROGRAM_DIR)/installer \
	$(PROGRAM_DIR)/snake \
	$(PROGRAM_DIR)/tetris \
	$(PROGRAM_DIR)/puzzle15 \
	$(PROGRAM_DIR)/2048 \
	$(PROGRAM_DIR)/terminal \
	$(PROGRAM_DIR)/nano \
	$(PROGRAM_DIR)/system \
	$(PROGRAM_DIR)/gui-demo \
 	$(PROGRAM_DIR)/files \
 	$(PROGRAM_DIR)/settings \
	$(PROGRAM_DIR)/login \
 	$(PROGRAM_DIR)/monitor \
	$(PROGRAM_DIR)/disks \
	$(PROGRAM_DIR)/devmgr \
	$(PROGRAM_DIR)/logview \
 	$(PROGRAM_DIR)/hexedit \
  	$(PROGRAM_DIR)/imgview \
  	$(PROGRAM_DIR)/hello \
  	$(PROGRAM_DIR)/forktest \
  	$(PROGRAM_DIR)/sh \
  	$(PROGRAM_DIR)/box \
  	$(PROGRAM_DIR)/proctest \
  	$(PROGRAM_DIR)/thrtest \
  	$(PROGRAM_DIR)/journalctl \
  	$(PROGRAM_DIR)/httpget \
  	$(PROGRAM_DIR)/httpsget \
  	$(PROGRAM_DIR)/fputest \
  	$(PROGRAM_DIR)/gltest \
	$(PROGRAM_DIR)/cd \
	$(PROGRAM_DIR)/help \
	$(PROGRAM_DIR)/ls \
	$(PROGRAM_DIR)/cat \
	$(PROGRAM_DIR)/touch \
	$(PROGRAM_DIR)/mkdir \
	$(PROGRAM_DIR)/rm \
	$(PROGRAM_DIR)/mkfs \
	$(PROGRAM_DIR)/pwd \
	$(PROGRAM_DIR)/clear \
 	$(PROGRAM_DIR)/echo \
 	$(PROGRAM_DIR)/battery \
 	$(PROGRAM_DIR)/burn \
 	$(PROGRAM_DIR)/eat \
 	$(PROGRAM_DIR)/cpuinfo \
 	$(PROGRAM_DIR)/raminfo \
 	$(PROGRAM_DIR)/hddcheck \
	$(PROGRAM_DIR)/reboot \
	$(PROGRAM_DIR)/poweroff \
	$(PROGRAM_DIR)/shutdown \
	$(PROGRAM_DIR)/halt \
	$(PROGRAM_DIR)/lua
	
TCC_PRESENT := $(wildcard $(ROOT_DIR)/tcc/libtcc.c)
ifneq ($(TCC_PRESENT),)
TARGETS += $(PROGRAM_DIR)/tcc
endif

VIM_DIR := $(ROOT_DIR)/vim/src
VIM_PRESENT := $(wildcard $(VIM_DIR)/vim.h)
ifneq ($(VIM_PRESENT),)
VIM_SOURCES := alloc arabic arglist autocmd beval blob blowfish buffer change \
	charset cindent clientserver clipboard cmdexpand cmdhist crypt crypt_zip \
	debugger dict diff digraph drawline drawscreen edit eval evalbuffer \
	evalfunc evalvars evalwindow ex_cmds ex_cmds2 ex_docmd ex_eval ex_getln \
	fileio filepath findfile float fold fuzzy getchar gc hardcopy \
	hardcopy_postscript hashtab help highlight if_cscope indent insexpand json \
	linematch list locale logfile main map mark match mbyte memfile memline \
	menu message misc1 misc2 mouse move normal ops option optionstr os_unix \
	popupmenu popupwin profiler pty quickfix regexp register screen scriptfile \
	search session sha256 sign spell spellfile spellsuggest strings syntax \
	tabpanel tag term terminal testing textformat textobject textprop time \
	tuple typval ui undo usercmd userfunc version vim9class vim9cmds \
	vim9compile vim9execute vim9expr vim9generics vim9instr vim9script \
	vim9type viminfo window bufwrite
VIM_OBJECTS := $(VIM_SOURCES:%=$(OBJ_DIR)/vim/%.o) $(OBJ_DIR)/vim/pathdef.o \
	$(OBJ_DIR)/vim/purec_compat.o
VIM_CFLAGS := -I$(VIM_DIR) -I$(VIM_DIR)/proto -I$(ROOT_DIR)/mk/vim \
	-I$(ROOT_DIR)/src/libc/include/hosted -DHAVE_CONFIG_H -w
TARGETS += $(PROGRAM_DIR)/vim
-include $(VIM_OBJECTS:.o=.d)
endif

.NOTPARALLEL:
.PHONY: all hexedit
all: $(TARGETS)
hexedit: $(PROGRAM_DIR)/hexedit

-include $(PLAIN_OBJECTS:.o=.d) $(GENERAL_OBJECTS:.o=.d) $(GENERAL_CPP_OBJECTS:.o=.d) $(LUA_OBJECTS:.o=.d) $(OBJ_DIR)/tcc/tcc.d

$(GENERAL_OBJECTS): PRIVATE_CFLAGS :=
$(GENERAL_CPP_OBJECTS): PRIVATE_CXXFLAGS := -std=c++20 -fno-exceptions -fno-rtti
$(LUA_OBJECTS): PRIVATE_CFLAGS := -DLUA_ANSI -I$(ROOT_DIR)/src/libc/include/hosted -include $(ROOT_DIR)/src/programs/lua/purec_lua_readline.h

$(LIBPUREC):
	$(MAKE) -C $(ROOT_DIR)/src/libc

$(LIB_DIR)/libpurec.so $(DYN_LD_STUB):
	$(MAKE) -C $(ROOT_DIR)/src/libc

ifdef PUREC_DYNAMIC
TARGETS += $(PROGRAM_DIR)/echo-dyn
PIE_LD_SCRIPT := $(ROOT_DIR)/linker-userspace-pie.ld

$(PROGRAM_DIR)/echo-dyn: $(OBJ_DIR)/echo/echo-pic.o $(LIB_DIR)/libpurec.so $(DYN_LD_STUB) $(PIE_LD_SCRIPT)
	@mkdir -p $(@D)
	$(LD) $(DYN_LINK_FLAGS) -o $@ $(OBJ_DIR)/echo/echo-pic.o $(DYN_LD_STUB) $(LIB_DIR)/libpurec.so

$(OBJ_DIR)/echo/echo-pic.o: $(ROOT_DIR)/src/programs/echo/echo.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(USER_SO_CFLAGS) -c $< -o $@
endif

$(LIBPUREGUI) $(LIBPGUIW):
	$(MAKE) -C $(ROOT_DIR)/src/libgui

$(LIBPUREFS):
	$(MAKE) -C $(ROOT_DIR)/src/libfs

$(OBJ_DIR)/accounts/install_accounts.o: $(ROOT_DIR)/src/fs/install_accounts.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(USER_CFLAGS) -c $< -o $@

$(LIBPURECRYPT):
	$(MAKE) -C $(CRYPT_DIR)
	@mkdir -p $(LIB_DIR)
	cp $(CRYPT_DIR)/build/libpurecrypt.a $@

$(OBJ_DIR)/%.o: $(ROOT_DIR)/src/programs/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(CRYPT_DIR)/include -I$(ROOT_DIR)/src/programs/settings/include $(DEPFLAGS) $(USER_CFLAGS) $(PRIVATE_CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(ROOT_DIR)/src/programs/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) -I$(ROOT_DIR)/src/programs/settings/include -I$(ROOT_DIR)/src/programs/hexedit/include $(DEPFLAGS) $(USER_CFLAGS) $(PRIVATE_CXXFLAGS) -c $< -o $@

$(PROGRAM_DIR)/init: $(OBJ_DIR)/init/main.o $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/installer: $(OBJ_DIR)/installer/main.o $(OBJ_DIR)/installer/progress.o $(OBJ_DIR)/installer/setup.o $(OBJ_DIR)/installer/ui.o $(OBJ_DIR)/installer/view.o $(ACCOUNT_OBJECTS) $(LIBPGUIW) $(LIBPUREGUI) $(LIBPUREFS) $(LIBPURECRYPT) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/snake: $(OBJ_DIR)/game/snake/main.o $(LIBPGUIW) $(LIBPUREGUI) $(LIBPUREAUDIO) $(LIBPUREC) $(LINKER_SCRIPT)
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

$(PROGRAM_DIR)/settings: $(OBJ_DIR)/settings/main.o $(OBJ_DIR)/settings/core/model.o $(OBJ_DIR)/settings/core/personalization.o $(OBJ_DIR)/settings/core/display.o $(OBJ_DIR)/settings/core/app.o $(OBJ_DIR)/settings/pages/audio.o $(OBJ_DIR)/settings/pages/wifi.o $(OBJ_DIR)/settings/pages/appearance.o $(OBJ_DIR)/settings/pages/display.o $(LIBPGUIW) $(LIBPUREGUI) $(LIBPUREAUDIO) $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

MONITOR_OBJECTS := $(OBJ_DIR)/monitor/main.o $(OBJ_DIR)/monitor/model.o $(OBJ_DIR)/monitor/ui.o

$(OBJ_DIR)/monitor/%.o: $(ROOT_DIR)/src/programs/monitor/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(ROOT_DIR)/src/libc/include/hosted $(DEPFLAGS) $(USER_CFLAGS) -c $< -o $@

$(PROGRAM_DIR)/monitor: $(MONITOR_OBJECTS) $(LIB_DIR)/crt0.o $(LIBPGUIW) $(LIBPUREGUI) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(LIB_DIR)/crt0.o $(MONITOR_OBJECTS) $(LIBPGUIW) $(LIBPUREGUI) $(LIBPUREC)

-include $(MONITOR_OBJECTS:.o=.d)

$(PROGRAM_DIR)/login: $(OBJ_DIR)/login/main.o $(OBJ_DIR)/accounts/accounts.o $(OBJ_DIR)/accounts/accounts_core.o $(LIBPGUIW) $(LIBPUREGUI) $(LIBPURECRYPT) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/disks: $(OBJ_DIR)/disks/main.o $(LIBPGUIW) $(LIBPUREGUI) $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/devmgr: $(OBJ_DIR)/devmgr/main.o $(LIBPGUIW) $(LIBPUREGUI) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/logview: $(OBJ_DIR)/logview/main.o $(LIBPGUIW) $(LIBPUREGUI) $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/tetris: $(OBJ_DIR)/game/tetris/main.o $(LIBPGUIW) $(LIBPUREGUI) $(LIBPUREAUDIO) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/puzzle15: $(OBJ_DIR)/game/puzzle15/puzzle15.o $(LIBPGUIW) $(LIBPUREGUI) $(LIBPUREAUDIO) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/2048: $(OBJ_DIR)/game/2048/game2048.o $(LIBPGUIW) $(LIBPUREGUI) $(LIBPUREAUDIO) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)



$(PROGRAM_DIR)/hexedit: $(GENERAL_CPP_OBJECTS) $(LIBPUREGUI) $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/imgview: $(OBJ_DIR)/imgview/main.o $(LIBPGUIW) $(LIBPUREGUI) $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/fputest: $(OBJ_DIR)/fputest/main.o $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/gltest: $(OBJ_DIR)/gltest/main.o $(LIBPGUIW) $(LIBPUREGUI) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/burn: $(OBJ_DIR)/burn/main.o $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/eat: $(OBJ_DIR)/eat/main.o $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/cd: $(OBJ_DIR)/cd/cd.o $(OBJ_DIR)/terminal/path.o $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/help: $(OBJ_DIR)/help/help.o $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/ls: $(OBJ_DIR)/ls/ls.o $(OBJ_DIR)/terminal/path.o $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/cat: $(OBJ_DIR)/cat/cat.o $(OBJ_DIR)/terminal/path.o $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/touch: $(OBJ_DIR)/touch/touch.o $(OBJ_DIR)/terminal/path.o $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/mkdir: $(OBJ_DIR)/mkdir/mkdir.o $(OBJ_DIR)/terminal/path.o $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/rm: $(OBJ_DIR)/rm/rm.o $(OBJ_DIR)/terminal/path.o $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/mkfs: $(OBJ_DIR)/mkfs/mkfs.o $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/pwd: $(OBJ_DIR)/pwd/pwd.o $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/clear: $(OBJ_DIR)/clear/clear.o $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/echo: $(OBJ_DIR)/echo/echo.o $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/battery: $(OBJ_DIR)/battery/battery.o $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/cpuinfo: $(OBJ_DIR)/cpuinfo/main.o $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/raminfo: $(OBJ_DIR)/raminfo/main.o $(LIBPGUIW) $(LIBPUREGUI) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/hddcheck: $(OBJ_DIR)/hddcheck/main.o $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/reboot: $(OBJ_DIR)/reboot/reboot.o $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/poweroff: $(OBJ_DIR)/poweroff/poweroff.o $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/shutdown: $(OBJ_DIR)/shutdown/shutdown.o $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)

$(PROGRAM_DIR)/halt: $(OBJ_DIR)/halt/halt.o $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(filter-out $(LINKER_SCRIPT),$^)




ifneq ($(TCC_PRESENT),)
TCC_PATCH_MARK := TCC_TARGET_PUREC
$(OBJ_DIR)/tcc/.patched: $(ROOT_DIR)/mk/tcc/purec-target.patch
	@mkdir -p $(@D)
	if ! grep -q "$(TCC_PATCH_MARK)" $(ROOT_DIR)/tcc/tccelf.c 2>/dev/null; then \
		patch -N --forward -p1 -d $(ROOT_DIR)/tcc < $(ROOT_DIR)/mk/tcc/purec-target.patch; \
	fi
	@touch $@

TCC_STATIC_MARK := TCC_PUREC_STATIC_DEFAULT
$(OBJ_DIR)/tcc/.patched-static: $(ROOT_DIR)/mk/tcc/purec-static.patch $(OBJ_DIR)/tcc/.patched
	@mkdir -p $(@D)
	if ! grep -qF "$(TCC_STATIC_MARK)" $(ROOT_DIR)/tcc/libtcc.c 2>/dev/null; then \
		patch --forward -p1 -d $(ROOT_DIR)/tcc < $(ROOT_DIR)/mk/tcc/purec-static.patch; \
	fi
	@touch $@

$(OBJ_DIR)/tcc/tcc.o: $(ROOT_DIR)/tcc/tcc.c $(ROOT_DIR)/mk/tcc/config.h $(OBJ_DIR)/tcc/.patched $(OBJ_DIR)/tcc/.patched-static
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(ROOT_DIR)/tcc -I$(ROOT_DIR)/mk/tcc -I$(ROOT_DIR)/src/libc/include/hosted $(DEPFLAGS) $(USER_CFLAGS) -DTCC_TARGET_X86_64 -DTCC_TARGET_PUREC -c $< -o $@

$(PROGRAM_DIR)/tcc: $(OBJ_DIR)/tcc/tcc.o $(LIB_DIR)/crt0.o $(LIBPUREC) $(LIB_DIR)/tcc/libtcc1.a $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(LIB_DIR)/crt0.o $(OBJ_DIR)/tcc/tcc.o $(LIBPUREC)

TCCRT_DIR := $(ROOT_DIR)/tcc/lib
TCCRT_OBJ_DIR := $(OBJ_DIR)/tcc/rt
TCCRT_LIB := $(LIB_DIR)/tcc/libtcc1.a
TCCRT_OBJECTS := $(TCCRT_OBJ_DIR)/libtcc1.o $(TCCRT_OBJ_DIR)/alloca.o \
	$(TCCRT_OBJ_DIR)/alloca-bt.o $(TCCRT_OBJ_DIR)/stdatomic.o \
	$(TCCRT_OBJ_DIR)/atomic.o $(TCCRT_OBJ_DIR)/builtin.o \
	$(TCCRT_OBJ_DIR)/va_list.o

-include $(TCCRT_OBJECTS:.o=.d)

$(TCCRT_OBJ_DIR)/%.o: $(TCCRT_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(ROOT_DIR)/tcc -I$(ROOT_DIR)/mk/tcc $(DEPFLAGS) $(USER_CFLAGS) -DTCC_TARGET_X86_64 -c $< -o $@

$(TCCRT_OBJ_DIR)/%.o: $(TCCRT_DIR)/%.S
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(ROOT_DIR)/tcc -I$(ROOT_DIR)/mk/tcc $(DEPFLAGS) $(USER_CFLAGS) -DTCC_TARGET_X86_64 -c $< -o $@

$(TCCRT_LIB): $(TCCRT_OBJECTS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^
endif

$(OBJ_DIR)/hello/main.o: $(ROOT_DIR)/src/programs/hello/main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(ROOT_DIR)/src/libc/include/hosted $(DEPFLAGS) $(USER_CFLAGS) -c $< -o $@

$(PROGRAM_DIR)/hello: $(OBJ_DIR)/hello/main.o $(LIB_DIR)/crt0.o $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(LIB_DIR)/crt0.o $(OBJ_DIR)/hello/main.o $(LIBPUREFS) $(LIBPUREC)

$(OBJ_DIR)/forktest/main.o: $(ROOT_DIR)/src/programs/forktest/main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(ROOT_DIR)/src/libc/include/hosted $(DEPFLAGS) $(USER_CFLAGS) -c $< -o $@

$(PROGRAM_DIR)/forktest: $(OBJ_DIR)/forktest/main.o $(LIB_DIR)/crt0.o $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(LIB_DIR)/crt0.o $(OBJ_DIR)/forktest/main.o $(LIBPUREFS) $(LIBPUREC)

$(OBJ_DIR)/sh/main.o: $(ROOT_DIR)/src/programs/sh/main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(ROOT_DIR)/src/libc/include/hosted $(DEPFLAGS) $(USER_CFLAGS) -c $< -o $@

$(PROGRAM_DIR)/sh: $(OBJ_DIR)/sh/main.o $(LIB_DIR)/crt0.o $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(LIB_DIR)/crt0.o $(OBJ_DIR)/sh/main.o $(LIBPUREFS) $(LIBPUREC)

$(OBJ_DIR)/box/main.o: $(ROOT_DIR)/src/programs/box/main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(ROOT_DIR)/src/libc/include/hosted $(DEPFLAGS) $(USER_CFLAGS) -c $< -o $@

$(PROGRAM_DIR)/box: $(OBJ_DIR)/box/main.o $(LIB_DIR)/crt0.o $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(LIB_DIR)/crt0.o $(OBJ_DIR)/box/main.o $(LIBPUREFS) $(LIBPUREC)

$(OBJ_DIR)/journalctl/main.o: $(ROOT_DIR)/src/programs/journalctl/main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(ROOT_DIR)/src/libc/include/hosted $(DEPFLAGS) $(USER_CFLAGS) -c $< -o $@

$(PROGRAM_DIR)/journalctl: $(OBJ_DIR)/journalctl/main.o $(LIB_DIR)/crt0.o $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(LIB_DIR)/crt0.o $(OBJ_DIR)/journalctl/main.o $(LIBPUREFS) $(LIBPUREC)

$(OBJ_DIR)/httpget/main.o: $(ROOT_DIR)/src/programs/httpget/main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(ROOT_DIR)/src/libc/include/hosted $(DEPFLAGS) $(USER_CFLAGS) -c $< -o $@

$(PROGRAM_DIR)/httpget: $(OBJ_DIR)/httpget/main.o $(LIB_DIR)/crt0.o $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(LIB_DIR)/crt0.o $(OBJ_DIR)/httpget/main.o $(LIBPUREFS) $(LIBPUREC)

LIBWOLF := $(LIB_DIR)/libwolf.a

$(LIBWOLF):
	$(MAKE) -C $(ROOT_DIR)/wolfssl lib

$(OBJ_DIR)/httpsget/main.o: $(ROOT_DIR)/src/programs/httpsget/main.c $(LIBWOLF)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(ROOT_DIR)/src/libc/include/hosted -I$(ROOT_DIR)/wolfssl -I$(ROOT_DIR)/wolfssl/purec -DWOLFSSL_USER_SETTINGS -DWOLF_PUREC_TLS $(DEPFLAGS) $(USER_CFLAGS) -c $< -o $@

$(PROGRAM_DIR)/httpsget: $(OBJ_DIR)/httpsget/main.o $(LIB_DIR)/crt0.o $(LIBWOLF) $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(LIB_DIR)/crt0.o $(OBJ_DIR)/httpsget/main.o $(LIBWOLF) $(LIBPUREFS) $(LIBPUREC)

CURL_DIR := $(ROOT_DIR)/curl
CURL_PRESENT := $(wildcard $(CURL_DIR)/lib/curl_setup.h)
ifneq ($(CURL_PRESENT),)
CURL_LIB_EXCLUDE := schannel sspi amigaos rustls mbedtls gtls vtls/apple openssl winapi multibyte \
	version_win32 gsasl krb5 spnego asyn-ares ngtcp2 quiche vquic-tls capsule vssh/libssh rtsp \
	win32_threads bearssl sectransp
CURL_LIB_ALL := $(sort $(shell cd $(CURL_DIR)/lib && $(MAKE) -s -f $(ROOT_DIR)/mk/curl/list.mk VAR=CSOURCES 2>/dev/null))
CURL_LIB_SOURCES := $(foreach f,$(filter %.c,$(CURL_LIB_ALL)),$(if $(strip $(foreach x,$(CURL_LIB_EXCLUDE),$(findstring $(x),$(f)))),,$(f)))
CURL_TOOL_SOURCES := $(filter %.c,$(shell cd $(CURL_DIR)/src && $(MAKE) -s -f $(ROOT_DIR)/mk/curl/list.mk VAR=CURL_CFILES 2>/dev/null))
CURL_OBJ_DIR := $(BIN_DIR)/obj/curl
CURL_OBJECTS := $(CURL_LIB_SOURCES:%.c=$(CURL_OBJ_DIR)/lib/%.o) $(CURL_TOOL_SOURCES:%.c=$(CURL_OBJ_DIR)/src/%.o)
CURL_CFLAGS := -I$(ROOT_DIR)/mk/curl -I$(CURL_DIR)/include -I$(CURL_DIR)/lib -I$(CURL_DIR)/src \
	-I$(ROOT_DIR)/src/libc/include/hosted -I$(ROOT_DIR)/wolfssl -I$(ROOT_DIR)/wolfssl/purec \
	-DWOLFSSL_USER_SETTINGS -DWOLF_PUREC_TLS -DHAVE_CONFIG_H -DBUILDING_LIBCURL -DCURL_STATICLIB \
	-include sys/select.h -w
ifneq ($(wildcard $(ROOT_DIR)/netsurf-libs/zlib/zlib.h),)
CURL_CFLAGS += -DHAVE_LIBZ -DHAVE_ZLIB_H -I$(ROOT_DIR)/netsurf-libs/zlib
CURL_ZLIB := $(LIB_DIR)/libz.a

$(LIB_DIR)/libz.a:
	$(MAKE) -C $(ROOT_DIR)/mk/netsurf zlib
endif
LIBGCC := $(shell $(CC) -print-libgcc-file-name)
TARGETS += $(PROGRAM_DIR)/curl
# "all: $(TARGETS)" above already expanded its list, so curl must be added to it here.
all: $(PROGRAM_DIR)/curl
-include $(CURL_OBJECTS:.o=.d)

$(CURL_OBJ_DIR)/lib/%.o: $(CURL_DIR)/lib/%.c $(LIBWOLF)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CURL_CFLAGS) $(DEPFLAGS) $(USER_CFLAGS) -c $< -o $@

$(CURL_OBJ_DIR)/src/%.o: $(CURL_DIR)/src/%.c $(LIBWOLF)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CURL_CFLAGS) $(DEPFLAGS) $(USER_CFLAGS) -c $< -o $@

$(PROGRAM_DIR)/curl: $(CURL_OBJECTS) $(LIB_DIR)/crt0.o $(LIBWOLF) $(CURL_ZLIB) $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(LIB_DIR)/crt0.o $(CURL_OBJECTS) $(LIBWOLF) $(CURL_ZLIB) $(LIBPUREFS) $(LIBPUREC) $(LIBGCC)
endif

$(OBJ_DIR)/proctest/main.o: $(ROOT_DIR)/src/programs/proctest/main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(ROOT_DIR)/src/libc/include/hosted $(DEPFLAGS) $(USER_CFLAGS) -c $< -o $@

$(PROGRAM_DIR)/proctest: $(OBJ_DIR)/proctest/main.o $(LIB_DIR)/crt0.o $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(LIB_DIR)/crt0.o $(OBJ_DIR)/proctest/main.o $(LIBPUREFS) $(LIBPUREC)

$(OBJ_DIR)/thrtest/main.o: $(ROOT_DIR)/src/programs/thrtest/main.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(ROOT_DIR)/src/libc/include/hosted $(DEPFLAGS) $(USER_CFLAGS) -c $< -o $@

$(PROGRAM_DIR)/thrtest: $(OBJ_DIR)/thrtest/main.o $(LIB_DIR)/crt0.o $(LIBPUREFS) $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(LIB_DIR)/crt0.o $(OBJ_DIR)/thrtest/main.o $(LIBPUREFS) $(LIBPUREC)

$(PROGRAM_DIR)/lua: $(LUA_OBJECTS) $(LIB_DIR)/crt0.o $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(LIB_DIR)/crt0.o $(LUA_OBJECTS) $(LIBPUREC)

ifneq ($(VIM_PRESENT),)
$(OBJ_DIR)/vim/pathdef.o: $(ROOT_DIR)/mk/vim/auto/pathdef.c $(ROOT_DIR)/mk/vim/auto/config.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(VIM_CFLAGS) $(DEPFLAGS) $(filter-out -g,$(USER_CFLAGS)) -c $< -o $@

$(OBJ_DIR)/vim/purec_compat.o: $(ROOT_DIR)/mk/vim/purec_compat.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(DEPFLAGS) $(USER_CFLAGS) -c $< -o $@

$(OBJ_DIR)/vim/%.o: $(VIM_DIR)/%.c $(ROOT_DIR)/mk/vim/auto/config.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(VIM_CFLAGS) $(DEPFLAGS) $(filter-out -g,$(USER_CFLAGS)) -c $< -o $@

$(PROGRAM_DIR)/vim: $(VIM_OBJECTS) $(LIB_DIR)/crt0.o $(LIBPUREC) $(LINKER_SCRIPT)
	@mkdir -p $(@D)
	$(LD) -T $(LINKER_SCRIPT) -o $@ $(LIB_DIR)/crt0.o $(VIM_OBJECTS) $(LIBPUREC)
endif
